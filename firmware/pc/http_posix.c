#define _POSIX_C_SOURCE 200809L

#include "http_posix.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* waits for the socket to be ready, within the time that is left; 1 ready, 0 time is up */
static int wait_fd(int fd, short events, long deadline)
{
    for (;;) {
        long left = deadline - now_ms();
        struct pollfd p = {fd, events, 0};
        int r;
        if (left <= 0) {
            return 0;
        }
        r = poll(&p, 1, (int)left);
        if (r > 0) {
            return 1;
        }
        if (r < 0 && errno != EINTR) {
            return 0;
        }
    }
}

todo_http posix_perform(void *user, const char *method, const char *path, const char *body, size_t body_len, const todo_sink *sink)
{
    posix_conn *c = user;
    todo_http h = {0, false, false, false};
    struct sockaddr_in addr;
    char req[1024], head[4096];
    size_t head_len = 0, sent = 0, body_start, delivered_body = 0;
    long deadline = now_ms() + c->timeout_ms, clen = -1;
    int fd, n, status = 0;
    bool chunked = false;
    char *end = NULL, *p;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)c->port);
    if (inet_pton(AF_INET, c->host, &addr.sin_addr) != 1) {
        h.offline = true;
        return h;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        h.offline = true;
        return h;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    c->connections++;
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 && errno != EINPROGRESS) {
        close(fd);
        h.offline = true;
        return h;
    }
    if (!wait_fd(fd, POLLOUT, deadline)) {
        close(fd);
        h.offline = true;
        return h;
    }
    {
        int err = 0;
        socklen_t el = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
        if (err) {
            close(fd);
            h.offline = true;
            return h;
        }
    }
    n = snprintf(req, sizeof(req),
                 "%s %s HTTP/1.1\r\nHost: %s:%d\r\nAuthorization: Bearer %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                 method, path, c->host, c->port, c->token, body_len, body ? body : "");
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        close(fd);
        h.aborted = true;
        return h;
    }
    while (sent < (size_t)n) {
        ssize_t w;
        if (!wait_fd(fd, POLLOUT, deadline)) {
            close(fd);
            h.offline = true;
            return h;
        }
        w = send(fd, req + sent, (size_t)n - sent, MSG_NOSIGNAL);
        if (w < 0 && errno != EAGAIN && errno != EINTR) {
            close(fd);
            h.offline = true;
            return h;
        }
        if (w > 0) {
            sent += (size_t)w;
        }
    }
    /* the head */
    while (!end) {
        ssize_t r;
        if (head_len + 1 >= sizeof(head)) { /* a head of 4 KB without an end: not an answer */
            close(fd);
            h.aborted = true;
            return h;
        }
        if (!wait_fd(fd, POLLIN, deadline)) {
            close(fd);
            h.offline = true; /* nothing, in time */
            return h;
        }
        r = recv(fd, head + head_len, sizeof(head) - 1 - head_len, 0);
        if (r == 0) {
            close(fd);
            h.offline = true; /* closed with no answer */
            return h;
        }
        if (r < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            close(fd);
            h.offline = true;
            return h;
        }
        head_len += (size_t)r;
        head[head_len] = '\0';
        end = strstr(head, "\r\n\r\n");
    }
    body_start = (size_t)(end - head) + 4;
    if (sscanf(head, "HTTP/1.%*d %d", &status) != 1) {
        close(fd);
        h.aborted = true;
        return h;
    }
    h.status = status;
    *end = '\0';
    for (p = strstr(head, "\r\n"); p; p = strstr(p + 2, "\r\n")) {
        if (strncasecmp(p + 2, "content-length:", 15) == 0) {
            clen = strtol(p + 17, NULL, 10);
        } else if (strncasecmp(p + 2, "transfer-encoding:", 18) == 0) {
            chunked = true;
        }
    }
    if (chunked) {
        clen = -1; /* the repository refuses what it cannot bound */
    } else if (status == 204 || status == 304) {
        clen = 0;
    }
    if (!sink->head(sink->ctx, status, clen)) {
        close(fd);
        h.aborted = true;
        return h;
    }
    /* the body: what came with the head, then the rest */
    if (head_len > body_start) {
        size_t have = head_len - body_start;
        delivered_body += have;
        c->delivered += have;
        if (!sink->body(sink->ctx, head + body_start, have)) {
            close(fd);
            h.aborted = true;
            return h;
        }
    }
    while (clen >= 0 && (long)delivered_body < clen) {
        char buf[512];
        ssize_t r;
        if (!wait_fd(fd, POLLIN, deadline)) {
            close(fd);
            h.incomplete = true; /* the server went quiet half way */
            return h;
        }
        r = recv(fd, buf, sizeof(buf), 0);
        if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
            close(fd);
            h.incomplete = true;
            return h;
        }
        if (r > 0) {
            delivered_body += (size_t)r;
            c->delivered += (size_t)r;
            if (!sink->body(sink->ctx, buf, (size_t)r)) {
                close(fd);
                h.aborted = true;
                return h;
            }
        }
    }
    close(fd);
    return h;
}
