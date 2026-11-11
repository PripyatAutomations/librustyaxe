/* SPDX-License-Identifier: MIT
 * End-to-end native SOCKS5 + TLS stream test client. Driven by the Python
 * loopback proxy fixture; no public network access is required.
 */
#include <librustyaxe/transport.tcp.h>
#include <librustyaxe/transport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct result {
   unsigned opened, received, errors, closed;
   char error[512];
};
static void on_transport(rr_transport_t *t, enum rr_transport_event event,
   rr_transport_channel_t *channel, const uint8_t *data, size_t len, void *user) {
   (void)channel;
   struct result *state = user;
   switch (event) {
      case RR_TRANSPORT_EVENT_OPEN:
         state->opened++;
         if (!(rr_transport_capabilities(t) & RR_TRANSPORT_TLS_AUTHENTICATED))
            state->errors++;
         if (rr_transport_send_message(t,
            "NICK nonick\r\nUSER nonick 0 * :SOCKS TLS fixture\r\n",
            strlen("NICK nonick\r\nUSER nonick 0 * :SOCKS TLS fixture\r\n"))
               != RR_TRANSPORT_OK) state->errors++;
         break;
      case RR_TRANSPORT_EVENT_STREAM_DATA:
         if (len >= 4 && memmem(data, len, " 001 ", 5)) {
            state->received++;
            rr_transport_close(t);
         }
         break;
      case RR_TRANSPORT_EVENT_ERROR:
         state->errors++;
         snprintf(state->error, sizeof(state->error), "%.*s", (int)len, data);
         break;
      case RR_TRANSPORT_EVENT_CLOSE:
         state->closed++;
         break;
      default:
         break;
   }
}
int main(int argc, char **argv) {
   if (argc != 5) {
      fprintf(stderr, "Usage: test_transport_tcp PROXY_PORT CA_PEM auth|none ok|mismatch|rejected\n");
      return 2;
   }
   char proxy[128];
   snprintf(proxy, sizeof(proxy), "socks5h://127.0.0.1:%s", argv[1]);
   const int auth = strcmp(argv[3], "auth") == 0;
   const int negative = strcmp(argv[4], "ok") != 0;
   struct result result = {0};
   rr_tcp_client_options_t options = {
      .host = "target.invalid", .port = 6697, .tls = true,
      .tls_server_name = !strcmp(argv[4], "mismatch") ? "wrong.invalid" : NULL,
      .ca_file = argv[2], .proxy_url = proxy,
      .proxy_user = auth ? "user" : NULL, .proxy_pass = auth ? "secret" : NULL
   };
   rr_transport_t *transport = rr_transport_tcp_connect(&options, on_transport, &result);
   if (!transport) return 3;
   for (int i = 0; i < 800 && !result.closed; i++)
      rr_transport_poll(transport, 10);
   if (!result.closed) {
      rr_transport_close(transport);
      for (int i = 0; i < 100 && !result.closed; i++)
         rr_transport_poll(transport, 10);
   }
   fprintf(stderr, "native SOCKS TLS (%s): open=%u data=%u errors=%u close=%u: %s\n",
      argv[4], result.opened, result.received, result.errors,
      result.closed, result.error);
   int ok = negative ? (result.errors > 0 && !result.opened) :
      (result.opened == 1 && result.received == 1 &&
       result.errors == 0 && result.closed == 1);
   if (negative && !strcmp(argv[4], "mismatch") &&
       !strstr(result.error, "certificate verification"))
      ok = 0;
   rr_transport_tcp_release(transport);
   return ok ? 0 : 1;
}
