/* SPDX-License-Identifier: MIT
 * Native RFC6455 WebSocket client, built over POSIX/OpenSSL/wslay.
 * Implements rr_transport.h; the application never owns socket/TLS state.
 */
#ifndef RR_TRANSPORT_WSS_H
#define RR_TRANSPORT_WSS_H
#include <librustyaxe/transport.h>
#include <stdbool.h>

typedef struct rr_wss_client_options {
   const char *user_agent;    /* optional, no CR/LF allowed */
   bool allow_cross_origin_redirect; /* default false, max eight 307 hops */
} rr_wss_client_options_t;

/* Connection and callbacks run on the calling thread. No background loop.
 * Call rr_transport_poll(t, 0) regularly; the driver performs no blocking I/O
 * after name resolution. Handles both ws:// and wss:// with verified TLS.
 * Close events are the point at which rr_transport_free() becomes legal.
 * Driver state is retained until the application calls rr_transport_wss_release()
 * after receiving CLOSE.
 */
rr_transport_t *rr_transport_wss_connect(const char *url,
    const rr_wss_client_options_t *options, rr_transport_callback_t cb, void *user);
void rr_transport_wss_release(rr_transport_t *transport);
#endif
