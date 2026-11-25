#ifndef NETSYNC_H
#define NETSYNC_H

/*
 * The board's side of the server: the clock (SNTP) and todo_netrepo on a task of its own, over esp_http_client. The UI uses
 * netsync_repo() as its repository and reads the time here; main tells it when the network is there and which server to talk to.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "todo/repo.h"

/* Creates the task (idle until there is a network and a server). */
esp_err_t netsync_start(void);

/* The repository for the UI. Valid after netsync_start. */
todo_repo *netsync_repo(void);

/* The network is up (an address was obtained) and the server is `server` ("host" or "host:port") with this token. Starts SNTP. */
void netsync_online(const char *server, const char *token);
/* The network is gone: requests are answered "offline" at once. */
void netsync_offline(void);

/* True and the epoch once the clock has been set by SNTP. */
bool netsync_time(int64_t *epoch);

#endif /* NETSYNC_H */
