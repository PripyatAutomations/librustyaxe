// Shared SOCKS5 TCP tunnel negotiation; no application or protocol state.
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <librustyaxe/socks.h>
#include <librustyaxe/util.time.h>

#ifdef USE_MONGOOSE

enum {
   SOCKS_OFF, SOCKS_GREETING, SOCKS_AUTH, SOCKS_CONNECT, SOCKS_READY, SOCKS_FAILED
};

bool rr_socks_init(rr_socks_t *s, const char *url, const char *user, const char *pass, const char *host, uint16_t port) {
   memset(s, 0, sizeof(*s));
   s->stage = SOCKS_FAILED;

   if (!url || !*url || !host || !*host || strlen(host) > 255 || !port) {
      return false;
   }
   const char *address;

   if (!strncmp(url, "socks5h://", 10)) {
      address = url + 10;
   } else if (!strncmp(url, "socks5://", 9)) {
      address = url + 9;
   } else {
      return false;
   }
   const char *begin = address, *end;
   bool ipv6 = *begin == '[';

   if (ipv6) {
      begin++;
      end = strchr(begin, ']');

      if (!end) {
         return false;
      }
   } else {
      end = begin + strcspn(begin, ":/");
   }
   size_t n = (size_t)(end - begin);
   char proxy_host[256];

   if (!n || n >= sizeof(proxy_host)) {
      return false;
   }
   memcpy(proxy_host, begin, n);
   proxy_host[n] = '\0';
   struct in6_addr ip6;

   if (ipv6) {
      if (inet_pton(AF_INET6, proxy_host, &ip6) != 1) {
         return false;
      }
   } else {
      for (const unsigned char *p = (const unsigned char *)proxy_host ; *p ; p++) {
         if (!isalnum(*p) && *p != '.' && *p != '-') {
            return false;
         }
      }
   }
   const char *suffix = end + ipv6;
   unsigned proxy_port = 1080;

   if (*suffix == ':') {
      suffix++;
      proxy_port = 0;

      if (!isdigit((unsigned char)*suffix)) {
         return false;
      }
      while (isdigit((unsigned char)*suffix)) {
         proxy_port = proxy_port * 10 + (unsigned)(*suffix++ - '0');

         if (proxy_port > 65535) {
            return false;
         }
      }

      if (!proxy_port) {
         return false;
      }
   }

   if (*suffix && strcmp(suffix, "/")) {
      return false;
   }

   if (inet_pton(AF_INET6, host, &ip6) != 1) {
      for (const unsigned char *p = (const unsigned char *)host ; *p ; p++) {
         if (!isalnum(*p) && *p != '.' && *p != '-' && *p != '_') {
            return false;
         }
      }
   }
   user = user ? user : "";
   pass = pass ? pass : "";

   if (strlen(user) > 255 || strlen(pass) > 255 || (*user && !*pass) || (!*user && *pass)) {
      return false;
   }
   snprintf(s->url, sizeof(s->url), "tcp://%s%s%s:%u", ipv6 ? "[" : "", proxy_host, ipv6 ? "]" : "", proxy_port);
   strcpy(s->host, host);
   strcpy(s->user, user);
   strcpy(s->pass, pass);
   s->port = port;
   s->stage = SOCKS_GREETING;
   s->deadline_ms = mono_ms() + 30000;

   return true;
}

static int fail(rr_socks_t *s, struct mg_connection *c, const char *error) {
   s->stage = SOCKS_FAILED;
   c->is_closing = 1;
   mg_error(c, "SOCKS5: %s", error);
   c->is_closing = 1;

   return -1;
}

static bool connect_request(rr_socks_t *s, struct mg_connection *c) {
   unsigned char packet[262] = {
      5, 1, 0, 3
   };
   struct in_addr ipv4;
   struct in6_addr ipv6;
   size_t n;

   if (inet_pton(AF_INET, s->host, &ipv4) == 1) {
      packet[3] = 1;
      memcpy(packet + 4, &ipv4, 4);
      n = 8;
   } else if (inet_pton(AF_INET6, s->host, &ipv6) == 1) {
      packet[3] = 4;
      memcpy(packet + 4, &ipv6, 16);
      n = 20;
   } else {
      packet[4] = (unsigned char)strlen(s->host);
      memcpy(packet + 5, s->host, packet[4]);
      n = 5 + packet[4];
   }
   packet[n++] = (unsigned char)(s->port >> 8);
   packet[n++] = (unsigned char)s->port;
   s->stage = SOCKS_CONNECT;

   if (!mg_send(c, packet, n)) {
      fail(s, c, "cannot queue CONNECT request");

      return false;
   }

   return true;
}

int rr_socks_event(rr_socks_t *s, struct mg_connection *c, int event) {
   if (event == MG_EV_ERROR || event == MG_EV_CLOSE || c->is_closing) {
      s->stage = SOCKS_FAILED;
      c->is_closing = 1;

      return -1;
   }

   if (s->stage == SOCKS_FAILED) {
      c->is_closing = 1;

      return -1;
   }

   if (s->stage == SOCKS_READY) {
      return 0;
   }

   if (event == MG_EV_POLL && mono_ms() >= s->deadline_ms) {
      return fail(s, c, "handshake timed out");
   }

   if (event == MG_EV_CONNECT) {
      unsigned char greeting[] = {
         5, 1, *s->user ? 2 : 0
      };

      if (!mg_send(c, greeting, sizeof(greeting))) {
         return fail(s, c, "cannot queue greeting");
      }
   }

   if (event != MG_EV_READ) {
      return 0;
   }
   while (!c->is_closing) {
      unsigned char *p = c->recv.buf;
      size_t n = c->recv.len;

      if (s->stage == SOCKS_GREETING) {
         if (n < 2) {
            return 0;
         }

         if (p[0] != 5 || p[1] != (*s->user ? 2 : 0)) {
            return fail(s, c, "authentication method rejected");
         }
         mg_iobuf_del(&c->recv, 0, 2);

         if (*s->user) {
            unsigned char auth[513];
            size_t u = strlen(s->user), v = strlen(s->pass);
            auth[0] = 1;
            auth[1] = (unsigned char)u;
            memcpy(auth + 2, s->user, u);
            auth[2 + u] = (unsigned char)v;
            memcpy(auth + 3 + u, s->pass, v);
            s->stage = SOCKS_AUTH;

            if (!mg_send(c, auth, 3 + u + v)) {
               return fail(s, c, "cannot queue authentication");
            }
         } else {
            if (!connect_request(s, c)) {
               return -1;
            }
         }
      } else if (s->stage == SOCKS_AUTH) {
         if (n < 2) {
            return 0;
         }

         if (p[0] != 1 || p[1] != 0) {
            return fail(s, c, "authentication failed");
         }
         mg_iobuf_del(&c->recv, 0, 2);

         if (!connect_request(s, c)) {
            return -1;
         }
      } else if (s->stage == SOCKS_CONNECT) {
         if (n < 4) {
            return 0;
         }

         if (p[0] != 5 || p[2] != 0) {
            return fail(s, c, "invalid CONNECT reply");
         }

         if (p[1] != 0) {
            static const char *errors[] = {
               "success", "proxy failure", "connection forbidden", "network unreachable",
               "host unreachable", "connection refused", "TTL expired", "command unsupported", "address unsupported"
            };

            return fail(s, c, p[1] < sizeof(errors) / sizeof(errors[0]) ? errors[p[1]] : "CONNECT rejected");
         }
         size_t length;

         if (p[3] == 1) {
            length = 10;
         } else if (p[3] == 4) {
            length = 22;
         } else if (p[3] == 3) {
            if (n < 5) {
               return 0;
            }

            if (!p[4]) {
               return fail(s, c, "empty reply hostname");
            }
            length = 7 + p[4];
         } else {
            return fail(s, c, "invalid reply address type");
         }

         if (n < length) {
            return 0;
         }
         mg_iobuf_del(&c->recv, 0, length);
         s->stage = SOCKS_READY;

         return 1;
      } else {
         return 0;
      }
   }
   return -1;
}
#endif
