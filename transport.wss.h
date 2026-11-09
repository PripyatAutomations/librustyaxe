/* SPDX-License-Identifier: MIT Native RFC6455 WebSocket client, built over POSIX/OpenSSL/wslay. Implements rr_transport.h; the application never owns
 * socket/TLS state.
 */
#ifndef RR_TRANSPORT_WSS_H
#define RR_TRANSPORT_WSS_H
#include <librustyaxe/transport.h>
#include <stdbool.h>

typedef struct rr_wss_client_options {
   const char *user_agent;      /* optional, no CR/LF allowed */
   bool allow_cross_origin_redirect;   /* default false, max eight 307 hops */
} rr_wss_client_options_t;

/* Extended options use a new symbol so preexisting binaries retain the exact original rr_wss_client_options_t layout. */
typedef struct rr_wss_client_options_ex {
   const char *user_agent;
   bool allow_cross_origin_redirect;
   const char *subprotocol;     /* exact requested/required RFC6455 subprotocol */
   const char *ca_file;         /* optional PEM trust bundle */
   const char *tls_server_name; /* verification name, independent of dialed URL host */
} rr_wss_client_options_ex_t;
/* Separate symbol/struct: do not change the ABI of connect_ex(). */
typedef struct rr_wss_client_options_secure {
   const char *user_agent;
   bool allow_cross_origin_redirect;
   const char *subprotocol;
   const char *ca_file;
   const char *tls_server_name;
   const char *tls_peer_spki_sha256; /* pinned SPKI SHA256, lowercase hex */
   bool tls_trust_on_first_use; /* explicit opt-in; persist first key before application traffic */
} rr_wss_client_options_secure_t;

/* Extended secure client options with a SOCKS5/SOCKS5H TCP tunnel.
 * The proxy MUST succeed before TLS or HTTP traffic begins; never bypass it.
 * Separate API to preserve the existing secure options ABI. */
typedef struct rr_wss_client_options_proxy {
   rr_wss_client_options_secure_t secure;
   const char *proxy_url;
   const char *proxy_user;
   const char *proxy_pass;
} rr_wss_client_options_proxy_t;
rr_transport_t *rr_transport_wss_connect_proxy(const char *url, const rr_wss_client_options_proxy_t *options, rr_transport_callback_t cb, void *user);

/* Connection and callbacks run on the calling thread. No background loop. Call rr_transport_poll(t, 0) regularly; the driver performs no blocking I/O after
 * name resolution. Handles both ws:// and wss:// with verified TLS. Close events are the point at which rr_transport_free() becomes legal. Driver state is
 * retained until the application calls rr_transport_wss_release() after receiving CLOSE.
 */
rr_transport_t *rr_transport_wss_connect(const char *url, const rr_wss_client_options_t *options, rr_transport_callback_t cb, void *user);
rr_transport_t *rr_transport_wss_connect_ex(const char *url, const rr_wss_client_options_ex_t *options, rr_transport_callback_t cb, void *user);
rr_transport_t *rr_transport_wss_connect_secure(const char *url, const rr_wss_client_options_secure_t *options, rr_transport_callback_t cb, void *user);
/* Returns the peer SPKI SHA256 hex after a successful handshake, or NULL. */
const char *rr_transport_wss_peer_spki_sha256(const rr_transport_t *transport);
/* Observed peer details for a failed-closed trust prompt. Borrowed until release. */
const char *rr_transport_wss_peer_cert_sha256(const rr_transport_t *transport);
const char *rr_transport_wss_peer_common_name(const rr_transport_t *transport);
const char *rr_transport_wss_peer_dns_names(const rr_transport_t *transport);
void rr_transport_wss_traffic(rr_transport_t *transport, uint64_t *rx, uint64_t *tx);
void rr_transport_wss_release(rr_transport_t *transport);
#endif
