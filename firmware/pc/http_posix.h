#ifndef TODO_HTTP_POSIX_H
#define TODO_HTTP_POSIX_H

/* The transport of todo_netrepo over POSIX sockets: for the PC tests that run the repository against the real server. It
 * does what the board's transport must do: bound every wait, read the head, hand the sink the status and the
 * Content-Length (-1 when there is none or the body is chunked), and deliver the body in pieces. */

#include <stddef.h>

#include "todo/netrepo.h"

typedef struct {
    char host[40];     /* an IPv4 address */
    int port;
    char token[80];
    int timeout_ms;    /* the whole exchange */
    size_t delivered;  /* body bytes handed to the sink, over the life of the transport */
    int connections;
} posix_conn;

todo_http posix_perform(void *user, const char *method, const char *path, const char *body, size_t body_len, const todo_sink *sink);

#endif /* TODO_HTTP_POSIX_H */
