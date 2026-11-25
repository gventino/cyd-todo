#ifndef NETSYNC_HTTP_TRANSPORT_H
#define NETSYNC_HTTP_TRANSPORT_H

#include "todo/netrepo.h"

typedef struct {
    char host[48];
    int port;
    char token[80];
    volatile bool online;
} http_target;

/* todo_transport.perform over esp_http_client: HTTP, no redirects, Connection: close, 15 s for the whole exchange. */
todo_http http_perform(void *user, const char *method, const char *path, const char *body, size_t body_len, const todo_sink *sink);

#endif /* NETSYNC_HTTP_TRANSPORT_H */
