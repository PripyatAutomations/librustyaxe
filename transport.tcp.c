/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <librustyaxe/transport.tcp.h>
#include <librustyaxe/tcp.dial.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TCP_TX_MAX 65536
enum { TCP_RESOLVE, TCP_HANDSHAKE, TCP_READY };
typedef struct {
   rr_transport_t *transport;
   int fd, notify, stage, tls_want;
   SSL_CTX *ctx;
   SSL *ssl;
   bool tls, tls_verified, dead, closing, error_sent;
   char host[256], tls_name[256], ca_file[1024];
   char proxy_url[1024], proxy_user[256], proxy_pass[256];
   uint16_t port;
   unsigned char tx[TCP_TX_MAX];
   size_t tx_len, tx_off, ssl_retry;
} tcp_client_t;
typedef struct {
   char host[256], proxy_url[1024], proxy_user[256], proxy_pass[256];
   uint16_t port;
   int notify;
} tcp_task_t;
static void *resolve_worker(void *opaque) {
   tcp_task_t *task = opaque;
   int fd = rr_tcp_dial(task->host, task->port, task->proxy_url,
                        task->proxy_user, task->proxy_pass);
   if (send(task->notify, &fd, sizeof(fd), MSG_NOSIGNAL) != (ssize_t)sizeof(fd) && fd >= 0)
      close(fd);
   close(task->notify);
   free(task);
   return NULL;
}
static bool resolve_start(tcp_client_t *c) {
   int pair[2];
   if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, pair) < 0) return false;
   tcp_task_t *task = calloc(1, sizeof(*task));
   if (!task) { close(pair[0]); close(pair[1]); return false; }
   snprintf(task->host, sizeof(task->host), "%s", c->host);
   snprintf(task->proxy_url, sizeof(task->proxy_url), "%s", c->proxy_url);
   snprintf(task->proxy_user, sizeof(task->proxy_user), "%s", c->proxy_user);
   snprintf(task->proxy_pass, sizeof(task->proxy_pass), "%s", c->proxy_pass);
   task->port = c->port;
   task->notify = pair[1];
   pthread_t tid;
   if (pthread_create(&tid, NULL, resolve_worker, task)) {
      close(pair[0]); close(pair[1]); free(task); return false;
   }
   pthread_detach(tid);
   c->notify = pair[0];
   c->stage = TCP_RESOLVE;
   return true;
}
static void fail(tcp_client_t *c, const char *reason) {
   if (!c->error_sent) {
      c->error_sent = true;
      rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_ERROR, NULL, reason, strlen(reason));
   }
   c->dead = true;
}
static void cleanup(tcp_client_t *c) {
   if (c->ssl) { SSL_free(c->ssl); c->ssl = NULL; }
   if (c->ctx) { SSL_CTX_free(c->ctx); c->ctx = NULL; }
   if (c->fd >= 0) { close(c->fd); c->fd = -1; }
   if (c->notify >= 0) { close(c->notify); c->notify = -1; }
}
static void tls_handshake_failure(tcp_client_t *c, int ssl_error) {
   char reason[512];
   const char *name = c->tls_name[0] ? c->tls_name : c->host;
   long verify = SSL_get_verify_result(c->ssl);
   if (verify != X509_V_OK) {
      snprintf(reason, sizeof(reason),
         "TCP TLS handshake failed for %s:%u (verify name %s): certificate verification: %s",
         c->host, (unsigned)c->port, name, X509_verify_cert_error_string(verify));
   } else {
      unsigned long code = ERR_peek_last_error();
      char detail[256];
      if (code) ERR_error_string_n(code, detail, sizeof(detail));
      else snprintf(detail, sizeof(detail), "SSL error %d (peer closed or rejected handshake)", ssl_error);
      snprintf(reason, sizeof(reason), "TCP TLS handshake failed for %s:%u: %s",
         c->host, (unsigned)c->port, detail);
   }
   fail(c, reason);
}
static bool tls_start(tcp_client_t *c) {
   c->ctx = SSL_CTX_new(TLS_client_method());
   if (!c->ctx || !SSL_CTX_set_default_verify_paths(c->ctx) ||
       (c->ca_file[0] && !SSL_CTX_load_verify_locations(c->ctx, c->ca_file, NULL))) return false;
   SSL_CTX_set_verify(c->ctx, SSL_VERIFY_PEER, NULL);
   c->ssl = SSL_new(c->ctx);
   if (!c->ssl || !SSL_set_fd(c->ssl, c->fd)) return false;
   const char *name = c->tls_name[0] ? c->tls_name : c->host;
   struct in_addr ip4;
   struct in6_addr ip6;
   if (inet_pton(AF_INET, name, &ip4) == 1 || inet_pton(AF_INET6, name, &ip6) == 1) {
      if (!X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(c->ssl), name)) return false;
   } else if (!SSL_set1_host(c->ssl, name)) return false;
   if (inet_pton(AF_INET, name, &ip4) != 1 && inet_pton(AF_INET6, name, &ip6) != 1)
      if (!SSL_set_tlsext_host_name(c->ssl, name)) return false;
   SSL_set_mode(c->ssl, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_ENABLE_PARTIAL_WRITE);
   SSL_set_connect_state(c->ssl);
   c->tls_want = SSL_ERROR_WANT_WRITE;
   c->stage = TCP_HANDSHAKE;
   return true;
}
static uint32_t caps(void *ctx) {
   tcp_client_t *c = ctx;
   return RR_TRANSPORT_RELIABLE_MESSAGES | RR_TRANSPORT_ORDERED_STREAMS |
      (c->tls ? RR_TRANSPORT_TLS_ENCRYPTED : 0) |
      (c->tls_verified ? RR_TRANSPORT_TLS_AUTHENTICATED : 0);
}
static int queue(void *ctx, const uint8_t *data, size_t len) {
   tcp_client_t *c = ctx;
   if (c->closing || c->dead || len > TCP_TX_MAX - (c->tx_len - c->tx_off))
      return RR_TRANSPORT_BACKPRESSURE;
   if (c->tx_off) {
      memmove(c->tx, c->tx + c->tx_off, c->tx_len - c->tx_off);
      c->tx_len -= c->tx_off;
      c->tx_off = 0;
   }
   if (len) memcpy(c->tx + c->tx_len, data, len);
   c->tx_len += len;
   return RR_TRANSPORT_OK;
}
static size_t queued(void *ctx) {
   tcp_client_t *c = ctx;
   return c->tx_len - c->tx_off;
}
static void closing(void *ctx) { ((tcp_client_t *)ctx)->closing = true; }
static int poll_tcp(void *ctx, int timeout_ms) {
   tcp_client_t *c = ctx;
   if (c->dead || c->closing) goto done;
   if (c->stage == TCP_RESOLVE) {
      struct pollfd p = { .fd = c->notify, .events = POLLIN };
      if (poll(&p, 1, timeout_ms) <= 0) return 0;
      int fd = -1;
      ssize_t n = recv(c->notify, &fd, sizeof(fd), 0);
      close(c->notify); c->notify = -1;
      if (n != sizeof(fd) || fd < 0) {
         fail(c, "TCP dial or SOCKS5 negotiation failed");
         goto done;
      }
      c->fd = fd;
      if (c->tls) {
         if (!tls_start(c)) { fail(c, "TCP TLS initialization failed"); goto done; }
      } else {
         c->stage = TCP_READY;
         rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_OPEN, NULL, NULL, 0);
      }
   }
   if (c->stage == TCP_HANDSHAKE) {
      struct pollfd p = { .fd = c->fd,
         .events = c->tls_want == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT };
      if (poll(&p, 1, timeout_ms) <= 0) return 0;
      int result = SSL_do_handshake(c->ssl);
      if (result == 1) {
         X509 *certificate = SSL_get1_peer_certificate(c->ssl);
         bool verified = certificate && SSL_get_verify_result(c->ssl) == X509_V_OK;
         X509_free(certificate);
         if (!verified) {
            fail(c, "TCP TLS peer certificate verification failed"); goto done;
         }
         c->tls_verified = true;
         c->stage = TCP_READY;
         rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_OPEN, NULL, NULL, 0);
      } else {
         c->tls_want = SSL_get_error(c->ssl, result);
         if (c->tls_want != SSL_ERROR_WANT_READ && c->tls_want != SSL_ERROR_WANT_WRITE) {
            tls_handshake_failure(c, c->tls_want); goto done;
         }
         return 0;
      }
   }
   if (c->stage == TCP_READY) {
      struct pollfd p = { .fd = c->fd, .events = POLLIN };
      if (c->tx_off < c->tx_len) p.events |= POLLOUT;
      if (poll(&p, 1, timeout_ms) < 0) {
         if (errno != EINTR) fail(c, "TCP poll failed");
         goto done;
      }
      if ((p.revents & (POLLERR | POLLNVAL)) != 0) {
         fail(c, "TCP socket error"); goto done;
      }
      if ((p.revents & POLLOUT) && c->tx_off < c->tx_len) {
         const unsigned char *buf = c->tx + c->tx_off;
         size_t len = c->tx_len - c->tx_off;
         int n = c->ssl ? SSL_write(c->ssl, buf, (int)(c->ssl_retry ? c->ssl_retry : len)) :
            (int)send(c->fd, buf, len, MSG_NOSIGNAL);
         if (n > 0) {
            c->ssl_retry = 0;
            c->tx_off += (size_t)n;
            if (c->tx_off == c->tx_len) c->tx_off = c->tx_len = 0;
         } else if (c->ssl) {
            int err = SSL_get_error(c->ssl, n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
               c->ssl_retry = c->ssl_retry ? c->ssl_retry : len;
            else fail(c, "TCP TLS write failed");
         } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            fail(c, "TCP write failed");
         }
      }
      if (!c->dead && ((p.revents & POLLIN) || (c->ssl && SSL_pending(c->ssl) > 0))) {
         unsigned char bytes[8192];
         int n = c->ssl ? SSL_read(c->ssl, bytes, sizeof(bytes)) :
            (int)recv(c->fd, bytes, sizeof(bytes), 0);
         if (n > 0) rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_STREAM_DATA, NULL, bytes, (size_t)n);
         else if (n == 0) c->dead = true;
         else if (c->ssl) {
            int err = SSL_get_error(c->ssl, n);
            if (err == SSL_ERROR_ZERO_RETURN) c->dead = true;
            else if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
               fail(c, "TCP TLS read failed");
         } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            fail(c, "TCP read failed");
      }
      if (p.revents & POLLHUP) c->dead = true;
   }
done:
   if (c->dead || c->closing) {
      cleanup(c);
      rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_CLOSE, NULL, NULL, 0);
      return RR_TRANSPORT_CLOSED_ERROR;
   }
   return 1;
}
static const rr_transport_driver_t ops = {
   .abi_version = RR_TRANSPORT_ABI_VERSION,
   .capabilities = RR_TRANSPORT_RELIABLE_MESSAGES | RR_TRANSPORT_ORDERED_STREAMS |
      RR_TRANSPORT_TLS_ENCRYPTED | RR_TRANSPORT_TLS_AUTHENTICATED,
   .connection_capabilities = caps, .send_message = queue, .queued_bytes = queued,
   .poll = poll_tcp, .close = closing
};
rr_transport_t *rr_transport_tcp_connect(const rr_tcp_client_options_t *o,
                                          rr_transport_callback_t cb, void *user) {
   if (!o || !o->host || !*o->host || !o->port) return NULL;
   tcp_client_t *c = calloc(1, sizeof(*c));
   if (!c) return NULL;
   c->fd = c->notify = -1;
   const char *name = o->tls_server_name ? o->tls_server_name : "";
   const char *ca = o->ca_file ? o->ca_file : "";
   const char *proxy = o->proxy_url ? o->proxy_url : "";
   const char *proxy_user = o->proxy_user ? o->proxy_user : "";
   const char *proxy_pass = o->proxy_pass ? o->proxy_pass : "";
   if (strlen(o->host) >= sizeof(c->host) || strlen(name) >= sizeof(c->tls_name) ||
       strlen(ca) >= sizeof(c->ca_file) || strlen(proxy) >= sizeof(c->proxy_url) ||
       strlen(proxy_user) >= sizeof(c->proxy_user) ||
       strlen(proxy_pass) >= sizeof(c->proxy_pass)) { free(c); return NULL; }
   snprintf(c->host, sizeof(c->host), "%s", o->host);
   snprintf(c->tls_name, sizeof(c->tls_name), "%s", name);
   snprintf(c->ca_file, sizeof(c->ca_file), "%s", ca);
   snprintf(c->proxy_url, sizeof(c->proxy_url), "%s", proxy);
   snprintf(c->proxy_user, sizeof(c->proxy_user), "%s", proxy_user);
   snprintf(c->proxy_pass, sizeof(c->proxy_pass), "%s", proxy_pass);
   c->tls = o->tls; c->port = o->port;
   if (!resolve_start(c)) { cleanup(c); free(c); return NULL; }
   c->transport = rr_transport_new(&ops, c, cb, user);
   if (!c->transport) { cleanup(c); free(c); return NULL; }
   return c->transport;
}
void rr_transport_tcp_release(rr_transport_t *transport) {
   if (!transport || rr_transport_state(transport) != RR_TRANSPORT_CLOSED) return;
   tcp_client_t *c = rr_transport_driver_context(transport);
   cleanup(c); free(c);
   rr_transport_free(transport);
}
