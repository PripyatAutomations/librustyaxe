/* SPDX-License-Identifier: MIT
 * Native TCP/TLS byte stream for IRC and other line protocols.
 * Uses the same resolver-worker dialer and SOCKS5 policy as native WSS.
 */
#ifndef LIBRUSTYAXE_TRANSPORT_TCP_H
#define LIBRUSTYAXE_TRANSPORT_TCP_H
#include <librustyaxe/transport.h>
#include <stdint.h>
typedef struct rr_tcp_client_options {
   const char *host;
   uint16_t port;
   bool tls;
   const char *tls_server_name; /* override verification name, if configured */
   const char *ca_file;
   const char *proxy_url;       /* socks5:// or socks5h://; fail closed */
   const char *proxy_user;
   const char *proxy_pass;
} rr_tcp_client_options_t;
/* Stream chunks arrive via RR_TRANSPORT_EVENT_STREAM_DATA; send_message()
 * sends unframed bytes. Poll from the core thread, release after CLOSE. */
rr_transport_t *rr_transport_tcp_connect(const rr_tcp_client_options_t *options,
                                         rr_transport_callback_t callback, void *context);
void rr_transport_tcp_release(rr_transport_t *transport);
#endif
