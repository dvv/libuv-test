#ifndef _UTCP_H
#define _UTCP_H

#include <stddef.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "uv.h"

/* events */
enum event_t {
  EVT_ERROR = 1,
  EVT_LISTEN,
  EVT_CLI_OPEN,
  EVT_CLI_DATA,
  EVT_CLI_DRAIN,
  EVT_CLI_END,
  EVT_CLI_CLOSE,
  EVT_REQ_OPEN,
  EVT_REQ_DATA,
  EVT_REQ_END,
  EVT_REQ_CLOSE,
  EVT_MAX
};

#ifdef DEBUG
# define DEBUGF(fmt, params...) fprintf(stderr, fmt "\n", params)
#else
# define DEBUGF(fmt, params...) do {} while (0)
#endif

typedef struct server_s server_t;
typedef struct client_s client_t;

typedef void (*callback_t)(int status);
typedef void (*event_cb)(void *self, enum event_t ev, int status, uv_buf_t *buf);
typedef void (*read_cb)(client_t *self, ssize_t nread, uv_buf_t buf);

struct client_s {
  server_t *server;
  uv_tcp_t handle;
  int closed : 1;
};

struct timeouts_s {
  uint64_t first_message, stale_client, keepalive;
};

struct server_s {
  uv_tcp_t handle;
  int closed : 1;
  event_cb on_event;
  read_cb on_read;
  // options
  size_t sizeof_client;
  struct timeouts_s timeouts;
};

server_t *tcp_server_init(
    int port,
    const char *host,
    int backlog_size,
    event_cb on_event
  );

void client_write(client_t *client, void *data, size_t len);
void client_finish(client_t *client);

#endif
