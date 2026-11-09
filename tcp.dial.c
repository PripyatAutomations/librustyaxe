/* SPDX-License-Identifier: MIT
 * Blocking resolver-worker TCP dialer; SOCKS5 replies are fully consumed
 * before returning the connected socket to the protocol transport.
 */
#define _POSIX_C_SOURCE 200809L
#include "tcp.dial.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static bool io_wait(int fd, short events) {
   struct pollfd p = { .fd = fd, .events = events };
   int n;
   do { n = poll(&p, 1, 5000); } while (n < 0 && errno == EINTR);
   return n > 0 && (p.revents & events) && !(p.revents & (POLLNVAL | POLLERR));
}
static bool write_all(int fd, const void *data, size_t len) {
   const unsigned char *p = data;
   while (len) {
      if (!io_wait(fd, POLLOUT)) return false;
      ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return false;
      p += n; len -= (size_t)n;
   }
   return true;
}
static bool read_all(int fd, void *data, size_t len) {
   unsigned char *p = data;
   while (len) {
      if (!io_wait(fd, POLLIN)) return false;
      ssize_t n = recv(fd, p, len, 0);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return false;
      p += n; len -= (size_t)n;
   }
   return true;
}
static int connect_host(const char *host, unsigned port) {
   char portbuf[12];
   snprintf(portbuf, sizeof(portbuf), "%u", port);
   struct addrinfo hint = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
   struct addrinfo *list = NULL;
   if (getaddrinfo(host, portbuf, &hint, &list)) return -1;
   int fd = -1;
   for (struct addrinfo *a = list; a; a = a->ai_next) {
      fd = socket(a->ai_family, a->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, a->ai_protocol);
      if (fd < 0) continue;
      int rc = connect(fd, a->ai_addr, a->ai_addrlen);
      if (rc < 0 && errno == EINPROGRESS && io_wait(fd, POLLOUT)) {
         int error = -1;
         socklen_t size = sizeof(error);
         if (!getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) && !error) rc = 0;
      }
      if (!rc) break;
      close(fd); fd = -1;
   }
   freeaddrinfo(list);
   return fd;
}
static bool proxy_address(const char *url, char host[256], unsigned *port) {
   const char *p;
   if (!strncmp(url, "socks5h://", 10)) p = url + 10;
   else if (!strncmp(url, "socks5://", 9)) p = url + 9;
   else return false;
   const char *end;
   if (*p == '[') {
      const char *close = strchr(++p, ']');
      if (!close) return false;
      end = close;
      if (end[1] != ':' && end[1] != '\0' && strcmp(end + 1, "/")) return false;
   } else end = p + strcspn(p, ":/");
   size_t len = (size_t)(end - p);
   if (!len || len >= 256) return false;
   memcpy(host, p, len); host[len] = '\0';
   const char *rest = end;
   if (p[-1] == '[') rest++; /* skip closing bracket */
   *port = 1080;
   if (*rest == ':') {
      unsigned value = 0;
      rest++;
      if (*rest < '0' || *rest > '9') return false;
      while (*rest >= '0' && *rest <= '9') {
         value = value * 10 + (unsigned)(*rest++ - '0');
         if (value > 65535) return false;
      }
      if (!value) return false;
      *port = value;
   }
   if (*rest && strcmp(rest, "/")) return false;
   /* Only DNS names, numeric IPv4 or IPv6 literals are permitted. */
   for (const unsigned char *s = (const unsigned char *)host; *s; ++s)
      if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
            (*s >= '0' && *s <= '9') || *s == '.' || *s == '-' || *s == ':'))
         return false;
   return true;
}
static bool negotiate(int fd, const char *host, uint16_t port, const char *user, const char *pass) {
   size_t ul = strlen(user), pl = strlen(pass), hl = strlen(host);
   if (hl == 0 || hl > 255 || ul > 255 || pl > 255 || (!!ul != !!pl)) return false;
   unsigned char greeting[] = {5, 1, ul ? 2 : 0}, response[4];
   if (!write_all(fd, greeting, sizeof(greeting)) || !read_all(fd, response, 2) ||
       response[0] != 5 || response[1] != greeting[2]) return false;
   if (ul) {
      unsigned char auth[513];
      auth[0] = 1; auth[1] = (unsigned char)ul;
      memcpy(auth+2, user, ul); auth[2+ul] = (unsigned char)pl;
      memcpy(auth+3+ul, pass, pl);
      if (!write_all(fd, auth, 3+ul+pl) || !read_all(fd, response, 2) ||
          response[0] != 1 || response[1] != 0) return false;
   }
   unsigned char packet[262] = {5, 1, 0, 3};
   struct in_addr ip4;
   struct in6_addr ip6;
   size_t n;
   if (inet_pton(AF_INET, host, &ip4) == 1) {
      packet[3] = 1; memcpy(packet+4, &ip4, 4); n = 8;
   } else if (inet_pton(AF_INET6, host, &ip6) == 1) {
      packet[3] = 4; memcpy(packet+4, &ip6, 16); n = 20;
   } else {
      packet[4] = (unsigned char)hl; memcpy(packet+5, host, hl); n = 5+hl;
   }
   packet[n++] = (unsigned char)(port >> 8);
   packet[n++] = (unsigned char)port;
   if (!write_all(fd, packet, n) || !read_all(fd, response, 4) ||
       response[0] != 5 || response[1] != 0 || response[2] != 0) return false;
   size_t remainder;
   if (response[3] == 1) remainder = 6;
   else if (response[3] == 4) remainder = 18;
   else if (response[3] == 3) {
      unsigned char length;
      if (!read_all(fd, &length, 1) || !length) return false;
      remainder = length + 2;
   } else return false;
   unsigned char discard[257];
   return read_all(fd, discard, remainder);
}
int rr_tcp_dial(const char *host, uint16_t port, const char *proxy_url,
                const char *proxy_user, const char *proxy_pass) {
   if (!host || !*host || !port) return -1;
   const bool proxied = proxy_url && *proxy_url;
   char proxy_host[256];
   unsigned proxy_port = 0;
   if (proxied && !proxy_address(proxy_url, proxy_host, &proxy_port)) return -1;
   int fd = connect_host(proxied ? proxy_host : host, proxied ? proxy_port : port);
   if (fd < 0) return -1;
   if (proxied && !negotiate(fd, host, port, proxy_user ? proxy_user : "",
                            proxy_pass ? proxy_pass : "")) {
      close(fd);
      return -1;
   }
   return fd;
}
