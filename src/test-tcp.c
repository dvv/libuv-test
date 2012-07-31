#include "tcp.h"

#define DELAY 1

#define MIMICK_HTTP 1

static void client_on_event(void *self, enum event_t ev, int status, uv_buf_t *buf)
{
  if (ev == EVT_CLI_DATA) {
    DEBUGF("CDATA %p %*s", self, buf->len, buf->base);
#if MIMICK_HTTP
    char *b = "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nHello\n";
    client_write(self, b, strlen(b));
    client_finish(self);
#else
    client_write(self, buf->base, buf->len);
#endif
  } else if (ev == EVT_CLI_END) {
    DEBUGF("CEND %p", self);
  } else if (ev == EVT_CLI_DRAIN) {
    DEBUGF("CDRAIN %p", self);
  } else if (ev == EVT_CLI_OPEN) {
    DEBUGF("COPEN %p", self);
  } else if (ev == EVT_CLI_CLOSE) {
    DEBUGF("CCLOSE %p", self);
  } else if (ev == EVT_ERROR) {
    DEBUGF("ERROR %p %d", self, status);
  } else if (ev == EVT_LISTEN) {
    DEBUGF("LISTEN %p", self);
  }
}

int main()
{
  uv_loop_t *loop = uv_default_loop();

  // N.B. let libuv catch EPIPE
  signal(SIGPIPE, SIG_IGN);

  server_t *server = tcp_server_init(
      8080, "0.0.0.0", 1024,
      client_on_event
    );

  // block in the main loop
  uv_run(loop);
}
