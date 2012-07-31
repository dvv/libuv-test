#include <assert.h>

#include "tcp.h"

void *zalloc(size_t size)
{
  void *p = malloc(size);
  memset(p, 0, size);
  return p;
}

/******************************************************************************/
/* UV request pool
/******************************************************************************/

typedef struct req_s {
  union uv_any_req uv_req;
  void *ref;
} req_t;

static uv_req_t *req_alloc(void *ref)
{
  req_t *req = malloc(sizeof(*req));
  req->ref = ref;
  return (uv_req_t *)req;
}

static void req_free(uv_req_t *req)
{
  free(req);
}

/******************************************************************************/
/* client pool
/******************************************************************************/

static client_t *client_alloc(size_t size)
{
  client_t *client = zalloc(size);
  DEBUGF("CALLOC %p", client);
  assert(client);
  return client;
}

static void client_free(client_t *client) {
  DEBUGF("CFREE %p", client);
  assert(client);
  free(client);
}

/******************************************************************************/
/* client methods
/******************************************************************************/

void client_close(client_t *client);

// async: close is done
static void client_after_close(uv_handle_t *handle)
{
  client_t *self = handle->data;
  assert(self);

  // fire 'close' event
  self->server->on_event(self, EVT_CLI_CLOSE,
      uv_last_error(handle->loop).code, NULL);

  // cleanup
  client_free(self);
}

// close the client
void client_close(client_t *self)
{
  assert(self);
  // sanity check
  if (self->closed) return;

  self->closed = 1;

  // close the handle
  uv_close((uv_handle_t *)&self->handle, client_after_close);
}

// async: shutdown is done
static void client_after_shutdown(uv_shutdown_t *rq, int status)
{
  client_t *self = rq->data;
  req_free((uv_req_t *)rq);
  assert(self);

  // fire 'shut' event
  self->server->on_event(self, EVT_CLI_END,
      uv_last_error(self->handle.loop).code, NULL);

  // close the handle
  client_close(self);
}

// shutdown and close the client
void client_finish(client_t *self)
{
  assert(self);
  // sanity check
  if (self->closed) return;

  // shutdown UV stream, then close this client
  uv_shutdown_t *rq = (uv_shutdown_t *)req_alloc(NULL);
  rq->data = self;
  if (uv_shutdown(rq, (uv_stream_t *)&self->handle, client_after_shutdown)) {
    // on error still call callback with error status
    client_after_shutdown(rq, -1);
  }
}

// async: write is done
static void client_after_write(uv_write_t *rq, int status)
{
  client_t *self = rq->data;
  assert(self);

  // free write request
  req_free((uv_req_t *)rq);

  // write failed? report error
  if (status) {
    self->server->on_event(self, EVT_ERROR,
        uv_last_error(self->handle.loop).code, NULL);
  }
}

// raw write to client stream
void client_write(client_t *self, void *data, size_t len)
{
  assert(self);
  if (self->closed) return;

  DEBUGF("WRITE %p %*s", self, len, data);

  uv_write_t *rq = (uv_write_t *)req_alloc(NULL);
  rq->data = self;
  uv_buf_t buf = uv_buf_init(data, len);
  if (uv_write(rq, (uv_stream_t *)&self->handle,
      &buf, 1, client_after_write))
  {
    // on error still call callback with error status
    client_after_write(rq, -1);
  }
}

/******************************************************************************/
/* server connection reader
/******************************************************************************/

static void client_on_read(uv_stream_t *handle, ssize_t nread, uv_buf_t buf)
{
  client_t *self = handle->data;

  // read something?
  if (nread >= 0) {
    // feed read data
    buf.len = nread;
    self->server->on_event(self, EVT_CLI_DATA, 0, &buf);
  // report read errors
  } else {
    uv_err_t err = uv_last_error(handle->loop);
    // N.B. must close stream on read error, or libuv assertion fails
    client_close(self);
    if (err.code != UV_EOF && err.code != UV_ECONNRESET) {
      self->server->on_event(self, EVT_ERROR, err.code, NULL);
    }
  }

  // always free buffer
  free(buf.base);
}
/******************************************************************************/
/* server connection handler
/******************************************************************************/

static uv_buf_t buf_alloc(uv_handle_t *handle, size_t size)
{
  return uv_buf_init(malloc(size), size);
}

static void server_on_connection(uv_stream_t *handle, int status)
{
  server_t *server = handle->data;

  // allocate client
  client_t *client = client_alloc(server->sizeof_client);

  // setup client
  client->server = server;

  // accept client
  // TODO: report errors
  uv_tcp_init(server->handle.loop, &client->handle);
  client->handle.data = client;
  // TODO: EMFILE trick!
  // https://github.com/joyent/libuv/blob/master/src/unix/ev/ev.3#L1812-1816
  if (uv_accept((uv_stream_t *)&server->handle, (uv_stream_t *)&client->handle)) {
    // accept failed? report error
    client->server->on_event(server, EVT_ERROR, uv_last_error(uv_default_loop()).code, NULL);
    exit(-2);
  }

  // fire 'open' event
  client->server->on_event(client, EVT_CLI_OPEN, 0, NULL);

  // start reading client
  uv_read_start((uv_stream_t *)&client->handle, buf_alloc, client_on_read);
}

/******************************************************************************/
/* TCP server
/******************************************************************************/

server_t *tcp_server_init(
    int port,
    const char *host,
    int backlog_size,
    event_cb on_event
  )
{
  // allocate server
  server_t *server = zalloc(sizeof(*server));

  // setup server
  server->handle.data = server;
  server->on_event = on_event;
  server->sizeof_client = sizeof(client_t);
  // configure timeouts, let a client be inactive for a whole one day
  server->timeouts.first_message = 86400 * 1000;
  server->timeouts.stale_client  = 86400 * 1000;
  server->timeouts.keepalive     = 86400 * 1000;

  // setup listener
  uv_tcp_init(uv_default_loop(), &server->handle);
  struct sockaddr_in address = uv_ip4_addr(host, port);
  // bind
  if (uv_tcp_bind(&server->handle, address)) {
    // bind failed? report error
    on_event(server, EVT_ERROR, uv_last_error(uv_default_loop()).code, NULL);
  } else {
    // listen
    if (uv_listen((uv_stream_t *)&server->handle,
        backlog_size, server_on_connection))
    {
      // listen failed? report error
      on_event(server, EVT_ERROR, uv_last_error(uv_default_loop()).code, NULL);
    } else {
      // fire 'listen' event
      on_event(server, EVT_LISTEN, 0, NULL);
    }
  }

  return server;
}
