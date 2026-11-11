/* SPDX-License-Identifier: MIT
 * TCP connection establishment shared by native protocol transports.
 * Run from a resolver worker, never from the UI/audio event loop.
 */
#ifndef LIBRUSTYAXE_TCP_DIAL_H
#define LIBRUSTYAXE_TCP_DIAL_H
#include <stdint.h>
#include <stddef.h>
/* Returns a nonblocking connected descriptor or -1. Proxy errors NEVER
 * fall back to the destination. Both socks5 and socks5h resolve at proxy. */
int rr_tcp_dial_ex(const char *host, uint16_t port, const char *proxy_url,
                   const char *proxy_user, const char *proxy_pass,
                   char *reason, size_t reason_size);
int rr_tcp_dial(const char *host, uint16_t port, const char *proxy_url,
                const char *proxy_user, const char *proxy_pass);
#endif
