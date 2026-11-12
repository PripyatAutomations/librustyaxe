/* SPDX-License-Identifier: MIT Nonblocking native WebSocket client transport; single-threaded poll owner. DNS lookup is performed during connection
 * establishment.
 */
#include <librustyaxe/transport.wss.h>
#include <librustyaxe/tcp.dial.h>
#include <wslay/wslay.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/x509v3.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <pthread.h>

#define RR_WSS_HTTP_MAX 8192
#define RR_WSS_TX_MAX 65536
#define RR_WSS_FRAME_MAX 65536
enum {
   RESOLVING, CONNECTING, TLS_HANDSHAKE, HTTP_SEND, HTTP_READ, WEBSOCKET, DEAD
};
typedef struct native_wss {
   rr_transport_t *transport;
   int fd, resolver_fd, stage, hops, tls_want, write_want;
   SSL_CTX *ctx;
   SSL *ssl;
   wslay_event_context_ptr ws;
   char url[1024], host[256], path[768], origin[384], user_agent[384];
   char key[32], subprotocol[128], ca_file[1024], tls_server_name[256];
   char proxy_url[1024], proxy_user[256], proxy_pass[256];
   char tls_peer_spki_pin[65], tls_peer_spki_seen[65];
   char tls_cert_sha256[65], tls_peer_cn[256], tls_peer_sans[512];
   bool tls_trust_on_first_use, tls_peer_authenticated;
   unsigned port;
   bool tls, allow_cross_origin, dead, closing, error_sent;
   char http[RR_WSS_HTTP_MAX + 1];
   size_t http_len;
   uint8_t rx[RR_WSS_HTTP_MAX];
   size_t rx_len, rx_off;
   uint64_t rx_bytes, tx_bytes;
   uint8_t tx[RR_WSS_TX_MAX];
   size_t tx_len, tx_offset, ssl_retry_len;
   char reason[160];
} native_wss_t;

static void error(native_wss_t *c, const char *msg) {
   if (c->dead) {
      return;
   }
   snprintf(c->reason, sizeof(c->reason), "%s", msg);

   if (!c->error_sent) {
      c->error_sent = true;
      rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_ERROR, NULL, c->reason, strlen(c->reason));
   }
   c->dead = true;
}
static void reset_socket(native_wss_t *c) {
   if (c->ws) {
      wslay_event_context_free(c->ws);
      c->ws = NULL;
   }

   if (c->ssl) {
      SSL_free(c->ssl);
      c->ssl = NULL;
   }

   if (c->ctx) {
      SSL_CTX_free(c->ctx);
      c->ctx = NULL;
   }

   if (c->fd >= 0) {
      close(c->fd);
      c->fd = -1;
   }

   if (c->resolver_fd >= 0) {
      close(c->resolver_fd);
      c->resolver_fd = -1;
   }
   c->tx_len = c->tx_offset = c->ssl_retry_len = c->http_len = c->rx_len = c->rx_off = 0;
   c->tls_want = c->write_want = 0;
   c->tls_peer_authenticated = false;
   c->tls_peer_spki_seen[0] = '\0';
   c->tls_cert_sha256[0] = c->tls_peer_cn[0] = c->tls_peer_sans[0] = '\0';
}
static bool parse_url(native_wss_t *c, const char *url) {
   bool tls;
   const char *p;

   if (!strncmp(url, "wss://", 6)) {
      tls = true;
      p = url + 6;
   } else if (!strncmp(url, "ws://", 5)) {
      tls = false;
      p = url + 5;
   } else {
      return false;
   }
   const char *end = p + strcspn(p, "/?#");

   if (end == p || end - p >= (int)sizeof(c->origin)) {
      return false;
   }

   if (memchr(p, '@', (size_t)(end - p))) {
      return false;
   }
   const char *host = p, *portp = NULL;
   size_t hostlen;

   if (*p == '[') {
      host = p + 1;
      const char *bracket = memchr(host, ']', (size_t)(end - host));

      if (!bracket) {
         return false;
      }
      hostlen = (size_t)(bracket - host);

      if (bracket + 1 != end) {
         if (bracket[1] != ':') {
            return false;
         }
         portp = bracket + 2;
      }
   } else {
      const char *colon = memchr(p, ':', (size_t)(end - p));
      hostlen = colon ? (size_t)(colon - p) : (size_t)(end - p);

      if (colon) {
         portp = colon + 1;
      }
   }

   if (!hostlen || hostlen >= sizeof(c->host)) {
      return false;
   }
   memcpy(c->host, host, hostlen);
   c->host[hostlen] = 0;
   c->port = tls ? 443 : 80;

   if (portp) {
      unsigned long port = 0;

      if (portp == end) {
         return false;
      }

      for (const char *q = portp ; q < end ; ++q) {
         if (*q < '0' || *q > '9') {
            return false;
         }
         port = port * 10 + (unsigned)(*q - '0');

         if (port > 65535) {
            return false;
         }
      }

      if (!port) {
         return false;
      }
      c->port = (unsigned)port;
   }
   const char *path = end;

   if (*path == '?' || *path == '#') {
      return false;
   }

   if (!*path) {
      path = "/";
   }

   if (strlen(path) >= sizeof(c->path) || strlen(url) >= sizeof(c->url)) {
      return false;
   }

   for (const char *q = path ; *q ; ++q) {
      if (*q == '\r' || *q == '\n' || *q == '#') {
         return false;
      }
   }

   snprintf(c->origin, sizeof(c->origin), "%.*s", (int)(end - url), url);
   snprintf(c->path, sizeof(c->path), "%s", path);
   snprintf(c->url, sizeof(c->url), "%s", url);
   c->tls = tls;

   return true;
}

/* Resolving in a detached worker avoids blocking the poll/audio thread. The worker owns its task and one socketpair end, never touches native_wss. File
 * descriptors are process-wide: transferring the integer is sufficient.
 */
typedef struct resolver_task {
   char host[256], proxy_url[1024], proxy_user[256], proxy_pass[256];
   unsigned port;
   int notify;
} resolver_task_t;
static void *resolve_worker(void *opaque) {
   resolver_task_t *task = opaque;
   /* DNS and SOCKS5 negotiation run off the core/audio thread. A configured
    * proxy may never fall back to direct dialing. */
   int fd = rr_tcp_dial(task->host, (uint16_t)task->port, task->proxy_url,
                        task->proxy_user, task->proxy_pass);

   if (send(task->notify, &fd, sizeof(fd), MSG_NOSIGNAL) != (ssize_t) sizeof(fd) && fd >= 0) {
      close(fd);
   }
   close(task->notify);
   free(task);

   return NULL;
}
static bool start(native_wss_t *c) {
   int pair[2];

   if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, pair) < 0) {
      return false;
   }
   resolver_task_t *task = calloc(1, sizeof(*task));

   if (!task) {
      close(pair[0]);
      close(pair[1]);

      return false;
   }
   snprintf(task->host, sizeof(task->host), "%s", c->host);
   task->port = c->port;
   snprintf(task->proxy_url, sizeof(task->proxy_url), "%s", c->proxy_url);
   snprintf(task->proxy_user, sizeof(task->proxy_user), "%s", c->proxy_user);
   snprintf(task->proxy_pass, sizeof(task->proxy_pass), "%s", c->proxy_pass);
   task->notify = pair[1];
   pthread_t thread;

   if (pthread_create(&thread, NULL, resolve_worker, task)) {
      close(pair[0]);
      close(pair[1]);
      free(task);

      return false;
   }
   pthread_detach(thread);
   c->resolver_fd = pair[0];
   c->stage = RESOLVING;

   return true;
}
static bool resolver_ready(native_wss_t *c, int timeout_ms) {
   struct pollfd pfd = {
      .fd = c->resolver_fd, .events = POLLIN
   };
   int ready = poll(&pfd, 1, timeout_ms);

   if (ready <= 0) {
      return false;
   }
   int fd = -1;
   ssize_t n = recv(c->resolver_fd, &fd, sizeof(fd), 0);
   close(c->resolver_fd);
   c->resolver_fd = -1;

   if (n != (ssize_t) sizeof(fd) || fd < 0) {
      error(c, "WebSocket DNS resolution / TCP connection failed");

      return false;
   }
   c->fd = fd;
   c->stage = CONNECTING;

   return true;
}
static bool setup_tls(native_wss_t *c) {
   c->ctx = SSL_CTX_new(TLS_client_method());

   if (!c->ctx || !SSL_CTX_set_default_verify_paths(c->ctx)) {
      return false;
   }

   if (c->ca_file[0] && !SSL_CTX_load_verify_locations(c->ctx, c->ca_file, NULL)) {
      return false;
   }
   /* Pinned identities are checked after TLS handshake. */
   SSL_CTX_set_verify(c->ctx, (c->tls_peer_spki_pin[0] || c->tls_trust_on_first_use) ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, NULL);
   c->ssl = SSL_new(c->ctx);

   if (!c->ssl || !SSL_set_fd(c->ssl, c->fd)) {
      return false;
   }
   const char *identity = c->tls_server_name[0] ? c->tls_server_name : c->host;

   if (!SSL_set_tlsext_host_name(c->ssl, identity)) {
      return false;
   }

   if (!(c->tls_peer_spki_pin[0] || c->tls_trust_on_first_use) && !SSL_set1_host(c->ssl, identity)) {
      return false;
   }
   SSL_set_mode(c->ssl, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_ENABLE_PARTIAL_WRITE);
   SSL_set_connect_state(c->ssl);
   c->tls_want = SSL_ERROR_WANT_WRITE;

   return true;
}
/* Collect peer identity for display before any application protocol traffic.
 * Never treat names or a fingerprint as trusted merely because they exist. */
static bool verify_peer_public_key(native_wss_t *c) {
   X509 *peer = SSL_get1_peer_certificate(c->ssl);
   if (!peer) return false;
   unsigned char digest[EVP_MAX_MD_SIZE];
   unsigned int length = 0;
   int ok = X509_pubkey_digest(peer, EVP_sha256(), digest, &length);
   if (ok == 1 && length == 32) {
      for (unsigned i = 0; i < 32; ++i)
         snprintf(c->tls_peer_spki_seen + i * 2, 3, "%02x", digest[i]);
      c->tls_peer_spki_seen[64] = '\0';
   }
   length = 0;
   if (X509_digest(peer, EVP_sha256(), digest, &length) == 1 && length == 32) {
      for (unsigned i = 0; i < 32; ++i)
         snprintf(c->tls_cert_sha256 + i * 2, 3, "%02x", digest[i]);
      c->tls_cert_sha256[64] = '\0';
   }
   X509_NAME_get_text_by_NID(X509_get_subject_name(peer), NID_commonName,
      c->tls_peer_cn, (int)sizeof(c->tls_peer_cn));
   GENERAL_NAMES *names = X509_get_ext_d2i(peer, NID_subject_alt_name, NULL, NULL);
   if (names) {
      for (int i = 0; i < sk_GENERAL_NAME_num(names); ++i) {
         const GENERAL_NAME *name = sk_GENERAL_NAME_value(names, i);
         if (name->type != GEN_DNS) continue;
         const ASN1_STRING *asn = name->d.dNSName;
         const unsigned char *dns = ASN1_STRING_get0_data(asn);
         int n = ASN1_STRING_length(asn);
         if (!dns || n <= 0 || memchr(dns, '\0', (size_t)n) ||
             n > 250 || strlen(c->tls_peer_sans) + (size_t)n + 3 >= sizeof(c->tls_peer_sans))
            continue;
         size_t off = strlen(c->tls_peer_sans);
         if (off) { memcpy(c->tls_peer_sans + off, ", ", 2); off += 2; }
         memcpy(c->tls_peer_sans + off, dns, (size_t)n);
         c->tls_peer_sans[off + (size_t)n] = 0;
      }
      GENERAL_NAMES_free(names);
   }
   /* A pinned key authenticates the key, not its asserted DNS identity.
    * When the caller provides a verification name, enforce certificate SAN
    * matching even when the CA chain is replaced by explicit SPKI trust.
    * An empty override is reserved for an identity not yet learned; callers
    * must authenticate that identity before persisting trust. */
   if ((c->tls_peer_spki_pin[0] || c->tls_trust_on_first_use) &&
       c->tls_server_name[0]) {
      const char *name = c->tls_server_name;
      unsigned char ip[sizeof(struct in6_addr)];
      bool is_ip = inet_pton(AF_INET, name, ip) == 1 ||
                   inet_pton(AF_INET6, name, ip) == 1;
      int name_ok = is_ip ? X509_check_ip_asc(peer, name, 0) :
         X509_check_host(peer, name, 0,
                         X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS |
                         X509_CHECK_FLAG_NEVER_CHECK_SUBJECT, NULL);
      if (name_ok != 1) {
         X509_free(peer);
         error(c, "TLS certificate name does not match expected hostname");
         return false;
      }
   }
   X509_free(peer);
   if (!c->tls_peer_spki_seen[0]) return false;
   /* A pinned key, or an explicitly trusted connection, supersedes CA and
    * dial-address hostname checks. Node identity is independently validated
    * by the client against the server's authenticated domain announcement. */
   if (c->tls_peer_spki_pin[0] &&
       CRYPTO_memcmp(c->tls_peer_spki_pin, c->tls_peer_spki_seen, 64)) {
      char mismatch[128];
      snprintf(mismatch, sizeof(mismatch), "TLS_PIN_MISMATCH:%s", c->tls_peer_spki_seen);
      error(c, mismatch);
      return false;
   }
   if (!c->tls_peer_spki_pin[0] && c->tls_trust_on_first_use) {
      char challenge[128];
      snprintf(challenge, sizeof(challenge), "TLS_TRUST_REQUIRED:%s", c->tls_peer_spki_seen);
      error(c, challenge);
      return false;
   }
   if (!c->tls_peer_spki_pin[0] && SSL_get_verify_result(c->ssl) != X509_V_OK)
      return false;
   c->tls_peer_authenticated = true;
   return true;
}
static int read_bytes(native_wss_t *c, void *buf, size_t len) {
   int n = c->ssl ? SSL_read(c->ssl, buf, (int)len) : (int)recv(c->fd, buf, len, 0);

   if (n > 0) {
      c->rx_bytes += (uint64_t) n;

      return n;
   }

   if (c->ssl) {
      int e = SSL_get_error(c->ssl, n);

      if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
         return -2;
      }

      return -1;
   }

   if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      return -2;
   }

   return -1;
}
static int flush_tx(native_wss_t *c) {
   while (c->tx_offset < c->tx_len) {
      const uint8_t *p = c->tx + c->tx_offset;
      size_t left = c->tx_len - c->tx_offset;
      size_t attempt = c->ssl && c->ssl_retry_len ? c->ssl_retry_len : left;
      int n = c->ssl ? SSL_write(c->ssl, p, (int)attempt) : (int)send(c->fd, p, left, MSG_NOSIGNAL);

      if (n > 0) {
         c->ssl_retry_len = 0;
         c->write_want = 0;
         c->tx_offset += (size_t)n;
         c->tx_bytes += (uint64_t) n;
         continue;
      }

      if (c->ssl) {
         int e = SSL_get_error(c->ssl, n);

         if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            c->ssl_retry_len = attempt;
            c->write_want = e;

            return 0;
         }
      } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
         return 0;
      }

      return -1;
   }
   c->tx_len = c->tx_offset = 0;
   c->write_want = 0;

   return 1;
}
static bool queue_bytes(native_wss_t *c, const void *data, size_t len) {
   size_t pending = c->tx_len - c->tx_offset;

   if (len > sizeof(c->tx) - pending) {
      return false;
   }

   if (pending && c->tx_offset) {
      memmove(c->tx, c->tx + c->tx_offset, pending);
   }
   c->tx_len = pending;
   c->tx_offset = 0;
   memcpy(c->tx + c->tx_len, data, len);
   c->tx_len += len;

   return true;
}
static ssize_t ws_read(wslay_event_context_ptr ws, uint8_t *buf, size_t len, int flags, void *arg) {
   (void)flags;
   native_wss_t *c = arg;

   if (c->rx_off < c->rx_len) {
      size_t available = c->rx_len - c->rx_off;

      if (len > available) {
         len = available;
      }
      memcpy(buf, c->rx + c->rx_off, len);
      c->rx_off += len;

      if (c->rx_off == c->rx_len) {
         c->rx_off = c->rx_len = 0;
      }

      return (ssize_t) len;
   }
   int n = read_bytes(c, buf, len);

   if (n == -2) {
      wslay_event_set_error(ws, WSLAY_ERR_WOULDBLOCK);

      return -1;
   }

   if (n < 0) {
      wslay_event_set_error(ws, WSLAY_ERR_CALLBACK_FAILURE);

      return -1;
   }

   return n;
}
static ssize_t ws_write(wslay_event_context_ptr ws, const uint8_t *data, size_t len, int flags, void *arg) {
   (void)flags;
   native_wss_t *c = arg;

   if (!queue_bytes(c, data, len)) {
      wslay_event_set_error(ws, WSLAY_ERR_WOULDBLOCK);

      return -1;
   }

   return (ssize_t) len;
}
static int ws_mask(wslay_event_context_ptr ws, uint8_t *buf, size_t len, void *arg) {
   (void)ws;
   (void)arg;

   return RAND_bytes(buf, (int)len) == 1 ? 0 : -1;
}
static void ws_message(wslay_event_context_ptr ws, const struct wslay_event_on_msg_recv_arg *msg, void *arg) {
   (void)ws;
   native_wss_t *c = arg;

   if (msg->opcode == WSLAY_TEXT_FRAME || msg->opcode == WSLAY_BINARY_FRAME) {
      rr_transport_emit(c->transport, msg->opcode == WSLAY_TEXT_FRAME ? RR_TRANSPORT_EVENT_MESSAGE : RR_TRANSPORT_EVENT_BINARY_MESSAGE, NULL, msg->msg, msg->
         msg_length);
   } else if (msg->opcode == WSLAY_PONG) {
      rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_PONG, NULL, msg->msg, msg->msg_length);
   } else if (msg->opcode == WSLAY_CONNECTION_CLOSE) {
      c->closing = true;
   }
}
static bool begin_upgrade(native_wss_t *c) {
   uint8_t nonce[16];

   if (RAND_bytes(nonce, sizeof(nonce)) != 1) {
      return false;
   }
   EVP_EncodeBlock((unsigned char *)c->key, nonce, sizeof(nonce));
   char request[2300];
   int n = snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\n"
      "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n" "Sec-WebSocket-Key: %s\r\nUser-Agent: %s\r\n%s%s%s\r\n", c->path, c->host, c->port, c->key, c->
      user_agent, c->subprotocol[0] ? "Sec-WebSocket-Protocol: " : "", c->subprotocol, c->subprotocol[0] ? "\r\n" : "");

   if (n < 0 || n >= (int)sizeof(request)) {
      return false;
   }

   if (!queue_bytes(c, request, (size_t)n)) {
      return false;
   }
   c->stage = HTTP_SEND;

   return true;
}
static bool redirect(native_wss_t *c, const char *location) {
   if (c->hops >= 8) {
      return false;
   }
   char next[1024];

   if (!strncasecmp(location, "https://", 8)) {
      snprintf(next, sizeof(next), "wss://%s", location + 8);
   } else if (!strncasecmp(location, "http://", 7)) {
      snprintf(next, sizeof(next), "ws://%s", location + 7);
   } else if (!strncasecmp(location, "wss://", 6) || !strncasecmp(location, "ws://", 5)) {
      snprintf(next, sizeof(next), "%s", location);
   } else if (*location == '/') {
      snprintf(next, sizeof(next), "%s%s", c->origin, location);
   } else {
      return false;
   }

   if (!strcmp(next, c->url) || strlen(next) >= sizeof(c->url)) {
      return false;
   }
   char oldorigin[sizeof(c->origin)];
   snprintf(oldorigin, sizeof(oldorigin), "%s", c->origin);
   char oldpath[sizeof(c->path)];
   snprintf(oldpath, sizeof(oldpath), "%s", c->path);
   native_wss_t parsed = {
      0
   };

   if (!parse_url(&parsed, next)) {
      return false;
   }

   /* Redirects may upgrade WS to WSS, never downgrade an established
    * WSS connection to plaintext, even if cross-origin redirects are allowed. */
   if (c->tls && !parsed.tls) {
      return false;
   }
   if (!c->allow_cross_origin && strcasecmp(parsed.origin, oldorigin)) {
      return false;
   }

   if (parsed.path[0] == '/' && parsed.path[1] == '\0' && strcmp(oldpath, "/")) {
      /* Proxy redirects often point to the authority root: keep Kiwi route. */
      if (strlen(next) + strlen(oldpath) - 1 >= sizeof(next)) {
         return false;
      }
      strcat(next, oldpath + 1);

      if (!parse_url(&parsed, next)) {
         return false;
      }
   }
   reset_socket(c);
   snprintf(c->url, sizeof(c->url), "%s", parsed.url);
   snprintf(c->host, sizeof(c->host), "%s", parsed.host);
   snprintf(c->path, sizeof(c->path), "%s", parsed.path);
   snprintf(c->origin, sizeof(c->origin), "%s", parsed.origin);
   c->port = parsed.port;
   c->tls = parsed.tls;
   c->hops++;

   return start(c);
}
static int inspect_response(native_wss_t *c) {
   char *end = strstr(c->http, "\r\n\r\n");

   if (!end) {
      return 0;
   }
   int status = 0;

   if (sscanf(c->http, "HTTP/%*u.%*u %d", &status) != 1) {
      error(c, "Invalid HTTP upgrade response");

      return -1;
   }
   char location[1024] = "";
   char accept[128] = "";
   char negotiated[128] = "";
   bool upgrade = false, connection = false;
   const char *p = strstr(c->http, "\r\n");

   if (!p) {
      error(c, "Invalid HTTP header");

      return -1;
   }
   p += 2;
   while (p < end) {
      const char *e = strstr(p, "\r\n");

      if (!e || e > end) {
         break;
      }
      size_t len = (size_t)(e - p);

      if (len > 10 && !strncasecmp(p, "Location:", 9)) {
         const char *v = p + 9;
         while (v < e && isspace((unsigned char)*v)) {
            v++;
         }

         if ((size_t)(e - v) < sizeof(location)) {
            memcpy(location, v, (size_t)(e - v));
            location[e - v] = 0;
         }
      } else if (len > 21 && !strncasecmp(p, "Sec-WebSocket-Accept:", 21)) {
         const char *v = p + 21;
         while (v < e && isspace((unsigned char)*v)) {
            v++;
         }

         if ((size_t)(e - v) < sizeof(accept)) {
            memcpy(accept, v, (size_t)(e - v));
            accept[e - v] = 0;
         }
      } else if (len > 23 && !strncasecmp(p, "Sec-WebSocket-Protocol:", 23)) {
         const char *v = p + 23;
         while (v < e && isspace((unsigned char)*v)) {
            v++;
         }

         if ((size_t)(e - v) < sizeof(negotiated)) {
            memcpy(negotiated, v, (size_t)(e - v));
            negotiated[e - v] = 0;
         }
      } else if (len > 8 && !strncasecmp(p, "Upgrade:", 8)) {
         upgrade = true;
      } else if (len > 11 && !strncasecmp(p, "Connection:", 11)) {
         connection = true;
      }
      p = e + 2;
   }

   if (status == 307 && location[0]) {
      if (!redirect(c, location)) {
         error(c, "WebSocket HTTP 307 redirect rejected");

         return -1;
      }

      return 2;
   }

   if (status != 101) {
      char why[96];
      snprintf(why, sizeof(why), "WebSocket HTTP handshake rejected (%d)", status);
      error(c, why);

      return -1;
   }

   if (!upgrade || !connection) {
      error(c, "Incomplete WebSocket upgrade response");

      return -1;
   }

   if (c->subprotocol[0] && strcmp(negotiated, c->subprotocol)) {
      error(c, "WebSocket subprotocol negotiation failed");

      return -1;
   }
   char concat[128];
   snprintf(concat, sizeof(concat), "%s%s", c->key, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
   uint8_t digest[SHA_DIGEST_LENGTH];
   SHA1((unsigned char *)concat, strlen(concat), digest);
   char expected[32];
   EVP_EncodeBlock((unsigned char *)expected, digest, sizeof(digest));

   if (strcmp(accept, expected)) {
      error(c, "WebSocket accept signature mismatch");

      return -1;
   }
   size_t header_bytes = (size_t)(end - c->http) + 4;

   if (c->http_len > header_bytes) {
      size_t remain = c->http_len - header_bytes;
      memcpy(c->rx, c->http + header_bytes, remain);
      c->rx_len = remain;
      c->rx_off = 0;
   }
   struct wslay_event_callbacks callbacks = {
      .recv_callback = ws_read, .send_callback = ws_write,
      .genmask_callback = ws_mask, .on_msg_recv_callback = ws_message
   };

   if (wslay_event_context_client_init(&c->ws, &callbacks, c) != 0) {
      error(c, "Cannot create WebSocket framing context");

      return -1;
   }
   wslay_event_config_set_max_recv_msg_length(c->ws, RR_WSS_FRAME_MAX);
   c->stage = WEBSOCKET;
   rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_OPEN, NULL, NULL, 0);

   if (c->rx_len && !c->closing && wslay_event_recv(c->ws) < 0) {
      error(c, "Invalid frames following HTTP upgrade");

      return -1;
   }

   return 1;
}
static int send_frame(void *ctx, const uint8_t *data, size_t len, int opcode) {
   native_wss_t *c = ctx;

   if (!c->ws || c->dead || c->closing || len > RR_WSS_FRAME_MAX || c->tx_len - c->tx_offset + wslay_event_get_queued_msg_length(c->ws) + len > RR_WSS_TX_MAX) {
      return RR_TRANSPORT_BACKPRESSURE;
   }
   struct wslay_event_msg msg = {
      .opcode = (uint8_t) opcode, .msg = (const uint8_t *)data, .msg_length = len
   };

   return wslay_event_queue_msg(c->ws, &msg) == 0 ? RR_TRANSPORT_OK : RR_TRANSPORT_IO_ERROR;
}
static int send_text(void *ctx, const uint8_t *data, size_t len) {
   return send_frame(ctx, data, len, WSLAY_TEXT_FRAME);
}
static int send_binary(void *ctx, const uint8_t *data, size_t len) {
   return send_frame(ctx, data, len, WSLAY_BINARY_FRAME);
}
static size_t queued(void *ctx) {
   native_wss_t *c = ctx;

   return c->tx_len - c->tx_offset + (c->ws ? wslay_event_get_queued_msg_length(c->ws) : 0);
}
static uint32_t capabilities(void *ctx) {
   native_wss_t *c = ctx;

   return RR_TRANSPORT_RELIABLE_MESSAGES | RR_TRANSPORT_BINARY_MESSAGES |
      (c->tls ? RR_TRANSPORT_TLS_ENCRYPTED : 0) |
      (c->tls_peer_authenticated ? RR_TRANSPORT_TLS_AUTHENTICATED : 0);
}
static void close_transport(void *ctx) {
   ((native_wss_t *) ctx)->closing = true;
}
static int native_poll(void *ctx, int timeout_ms) {
   native_wss_t *c = ctx;

   if (c->dead || c->closing) {
      c->dead = true;
      goto finish;
   }

   if (c->stage == RESOLVING) {
      if (!resolver_ready(c, timeout_ms)) {
         if (c->dead) {
            goto finish;
         }

         return 0;
      }
   }
   short interest = POLLIN;

   if (c->stage == CONNECTING) {
      interest = POLLOUT;
   } else if (c->stage == TLS_HANDSHAKE) {
      interest = c->tls_want == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT;
   } else if (c->stage == HTTP_SEND) {
      interest = c->write_want == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT;
   } else if (c->stage == WEBSOCKET) {
      if (c->tx_len > c->tx_offset) {
         interest = c->write_want == SSL_ERROR_WANT_READ ? POLLIN : POLLIN | POLLOUT;
      } else if (c->ws && wslay_event_want_write(c->ws)) {
         interest = POLLIN | POLLOUT;
      }
   }
   bool buffered = (c->rx_len > c->rx_off || (c->ssl && SSL_pending(c->ssl) > 0));
   struct pollfd pfd = {
      .fd = c->fd, .events = interest
   };

   if (c->fd < 0) {
      error(c, "WebSocket has no socket");
      goto finish;
   }
   int ready = poll(&pfd, 1, buffered ? 0 : timeout_ms);

   if (ready < 0 && errno != EINTR) {
      error(c, "WebSocket poll failed");
      goto finish;
   }

   if (ready <= 0 && !buffered) {
      return 0;
   }

   if (pfd.revents & (POLLERR | POLLNVAL)) {
      if (c->stage != WEBSOCKET) {
         error(c, "WebSocket socket error");
         goto finish;
      }
   }

   /* POLLIN plus HUP may still contain a complete HTTP rejection or WebSocket message. Process the bytes before treating HUP as EOF. */
   if ((pfd.revents & POLLHUP) && !(pfd.revents & POLLIN) && c->stage != WEBSOCKET) {
      error(c, "WebSocket socket disconnected");
      goto finish;
   }

   if (c->stage == CONNECTING && (pfd.revents & POLLOUT)) {
      int status = 0;
      socklen_t size = sizeof(status);

      if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &status, &size) < 0 || status) {
         error(c, "WebSocket TCP connect failed");
         goto finish;
      }

      if (c->tls) {
         if (!setup_tls(c)) {
            error(c, "WebSocket TLS setup failed");
            goto finish;
         }
         c->stage = TLS_HANDSHAKE;
      } else if (!begin_upgrade(c)) {
         error(c, "WebSocket request failed");
         goto finish;
      }
   }

   if (c->stage == TLS_HANDSHAKE) {
      int n = SSL_do_handshake(c->ssl);

      if (n == 1) {
         if (!verify_peer_public_key(c) || !begin_upgrade(c)) {
            error(c, "WebSocket TLS peer verification/upgrade failed");
            goto finish;
         }
      } else {
         int e = SSL_get_error(c->ssl, n);
         c->tls_want = e;

         if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
            long verify = SSL_get_verify_result(c->ssl);
            /* Bound variable diagnostic text independently of the destination size. */
             char why[768];

            if (verify != X509_V_OK) {
               snprintf(why, sizeof(why), "WebSocket TLS verification failed for %.255s: %.240s (code %ld). Check trusted CA and certificate SAN", c->tls_server_name[0]
                  ? c->tls_server_name : c->host, X509_verify_cert_error_string(verify), verify);
            } else {
               unsigned long code = ERR_peek_last_error();
               const char *detail = code ? ERR_reason_error_string(code) : NULL;
               snprintf(why, sizeof(why), "WebSocket TLS handshake failed for %.255s (SSL error %d%s%.240s)", c->host, e, detail ? ": " : "", detail ? detail : "");
            }
            error(c, why);
            goto finish;
         }
      }
   }

   if (c->stage == HTTP_SEND && flush_tx(c) < 0) {
      error(c, "WebSocket HTTP request write failed");
      goto finish;
   }

   if (c->stage == HTTP_SEND && !c->tx_len) {
      c->stage = HTTP_READ;
   }

   if (c->stage == HTTP_READ && ((pfd.revents & POLLIN) || (c->ssl && SSL_pending(c->ssl) > 0))) {
      if (c->http_len >= RR_WSS_HTTP_MAX) {
         error(c, "WebSocket HTTP response too large");
         goto finish;
      }
      int n = read_bytes(c, c->http + c->http_len, RR_WSS_HTTP_MAX - c->http_len);

      if (n == -1) {
         error(c, "WebSocket HTTP response read failed");
         goto finish;
      }

      if (n > 0) {
         c->http_len += (size_t)n;
         c->http[c->http_len] = 0;
         int result = inspect_response(c);

         if (result == 2) {
            return 1;
         }

         if (result < 0) {
            goto finish;
         }
      }
   }

   if (c->stage == WEBSOCKET) {
      if (c->tx_len && flush_tx(c) < 0) {
         error(c, "WebSocket TCP send failed");
         goto finish;
      }

      if (c->ws && wslay_event_want_write(c->ws) && c->tx_len - c->tx_offset < RR_WSS_TX_MAX / 2) {
         if (wslay_event_send(c->ws) < 0) {
            error(c, "WebSocket framing send failed");
            goto finish;
         }

         if (flush_tx(c) < 0) {
            error(c, "WebSocket TCP send failed");
            goto finish;
         }
      }

      if ((pfd.revents & POLLIN) || c->rx_len > c->rx_off || (c->ssl && SSL_pending(c->ssl) > 0)) {
         if (wslay_event_recv(c->ws) < 0) {
            error(c, "WebSocket receive failed");
            goto finish;
         }
      }

      if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) || c->closing || !wslay_event_get_read_enabled(c->ws)) {
         c->dead = true;
      }
   }
finish:

   if (c->dead) {
      reset_socket(c);
      rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_CLOSE, NULL, NULL, 0);
   }

   return c->dead ? RR_TRANSPORT_CLOSED_ERROR : 1;
}

static const rr_transport_driver_t ops = {
   .abi_version = RR_TRANSPORT_ABI_VERSION,
   .capabilities = RR_TRANSPORT_RELIABLE_MESSAGES | RR_TRANSPORT_BINARY_MESSAGES | RR_TRANSPORT_TLS_ENCRYPTED | RR_TRANSPORT_TLS_AUTHENTICATED,
   .connection_capabilities = capabilities,
   .send_message = send_text, .send_binary_message = send_binary,
   .queued_bytes = queued, .poll = native_poll, .close = close_transport
};
rr_transport_t *rr_transport_wss_connect_proxy(const char *url, const rr_wss_client_options_proxy_t *proxy_options, rr_transport_callback_t cb, void *user) {
   const rr_wss_client_options_secure_t *options = proxy_options ? &proxy_options->secure : NULL;
   if (!url) {
      return NULL;
   }
   native_wss_t *c = calloc(1, sizeof(*c));

   if (!c) {
      return NULL;
   }
   c->fd = -1;
   c->resolver_fd = -1;
   c->allow_cross_origin = options && options->allow_cross_origin_redirect;
   const char *ua = options && options->user_agent ? options->user_agent : "RustyRig/1.0";
   const char *proto = options && options->subprotocol ? options->subprotocol : "";
   const char *ca = options && options->ca_file ? options->ca_file : "";
   const char *tls_name = options && options->tls_server_name ? options->tls_server_name : "";
   const char *pin = options && options->tls_peer_spki_sha256 ? options->tls_peer_spki_sha256 : "";
   const char *proxy_url = proxy_options && proxy_options->proxy_url ? proxy_options->proxy_url : "";
   const char *proxy_user = proxy_options && proxy_options->proxy_user ? proxy_options->proxy_user : "";
   const char *proxy_pass = proxy_options && proxy_options->proxy_pass ? proxy_options->proxy_pass : "";

   if (strlen(ua) >= sizeof(c->user_agent) || strpbrk(ua, "\r\n") ||
      strlen(proto) >= sizeof(c->subprotocol) || strpbrk(proto, "\r\n") ||
      strlen(ca) >= sizeof(c->ca_file) || strlen(tls_name) >= sizeof(c->tls_server_name) || strpbrk(tls_name, "\r\n") ||
       strlen(proxy_url) >= sizeof(c->proxy_url) || strlen(proxy_user) >= sizeof(c->proxy_user) ||
       strlen(proxy_pass) >= sizeof(c->proxy_pass) ||
       (pin[0] && (strlen(pin) != 64 || strspn(pin, "0123456789abcdef") != 64)) || !parse_url(c, url)) {
      free(c);

      return NULL;
   }
   snprintf(c->user_agent, sizeof(c->user_agent), "%s", ua);
   snprintf(c->subprotocol, sizeof(c->subprotocol), "%s", proto);
   snprintf(c->ca_file, sizeof(c->ca_file), "%s", ca);
   snprintf(c->tls_server_name, sizeof(c->tls_server_name), "%s", tls_name);
   snprintf(c->tls_peer_spki_pin, sizeof(c->tls_peer_spki_pin), "%s", pin);
   snprintf(c->proxy_url, sizeof(c->proxy_url), "%s", proxy_url);
   snprintf(c->proxy_user, sizeof(c->proxy_user), "%s", proxy_user);
   snprintf(c->proxy_pass, sizeof(c->proxy_pass), "%s", proxy_pass);
   c->tls_trust_on_first_use = options && options->tls_trust_on_first_use;
   if (!c->tls && (c->tls_peer_spki_pin[0] || c->tls_trust_on_first_use)) {
      free(c);
      return NULL;
   }

   if (!start(c)) {
      reset_socket(c);
      free(c);

      return NULL;
   }
   c->transport = rr_transport_new(&ops, c, cb, user);

   if (!c->transport) {
      reset_socket(c);
      free(c);

      return NULL;
   }

   return c->transport;
}
rr_transport_t *rr_transport_wss_connect_secure(const char *url, const rr_wss_client_options_secure_t *options, rr_transport_callback_t cb, void *user) {
   rr_wss_client_options_proxy_t extended = {0};
   if (options) extended.secure = *options;
   return rr_transport_wss_connect_proxy(url, &extended, cb, user);
}
const char *rr_transport_wss_peer_spki_sha256(const rr_transport_t *transport) {
   native_wss_t *c = transport ? rr_transport_driver_context((rr_transport_t *) transport) : NULL;
   return c && c->tls_peer_spki_seen[0] ? c->tls_peer_spki_seen : NULL;
}
const char *rr_transport_wss_peer_cert_sha256(const rr_transport_t *t) {
   native_wss_t *c = t ? rr_transport_driver_context((rr_transport_t *)t) : NULL;
   return c && c->tls_cert_sha256[0] ? c->tls_cert_sha256 : NULL;
}
const char *rr_transport_wss_peer_common_name(const rr_transport_t *t) {
   native_wss_t *c = t ? rr_transport_driver_context((rr_transport_t *)t) : NULL;
   return c && c->tls_peer_cn[0] ? c->tls_peer_cn : NULL;
}
const char *rr_transport_wss_peer_dns_names(const rr_transport_t *t) {
   native_wss_t *c = t ? rr_transport_driver_context((rr_transport_t *)t) : NULL;
   return c && c->tls_peer_sans[0] ? c->tls_peer_sans : NULL;
}
rr_transport_t *rr_transport_wss_connect_ex(const char *url, const rr_wss_client_options_ex_t *options, rr_transport_callback_t cb, void *user) {
   rr_wss_client_options_secure_t secure = {
      .user_agent = options ? options->user_agent : NULL,
      .allow_cross_origin_redirect = options && options->allow_cross_origin_redirect,
      .subprotocol = options ? options->subprotocol : NULL,
      .ca_file = options ? options->ca_file : NULL,
      .tls_server_name = options ? options->tls_server_name : NULL
   };
   return rr_transport_wss_connect_secure(url, &secure, cb, user);
}
rr_transport_t *rr_transport_wss_connect(const char *url, const rr_wss_client_options_t *options, rr_transport_callback_t cb, void *user) {
   rr_wss_client_options_ex_t extended = {
      .user_agent = options ? options->user_agent : NULL,
      .allow_cross_origin_redirect = options && options->allow_cross_origin_redirect
   };

   return rr_transport_wss_connect_ex(url, &extended, cb, user);
}
void rr_transport_wss_traffic(rr_transport_t *t, uint64_t *rx, uint64_t *tx) {
   native_wss_t *c = t ? rr_transport_driver_context(t) : NULL;

   if (rx) {
      *rx = c ? c->rx_bytes : 0;
   }

   if (tx) {
      *tx = c ? c->tx_bytes : 0;
   }
}
void rr_transport_wss_release(rr_transport_t *t) {
   if (!t || rr_transport_state(t) != RR_TRANSPORT_CLOSED) {
      return;
   }
   native_wss_t *c = rr_transport_driver_context(t);
   reset_socket(c);
   free(c);
   rr_transport_free(t);
}
