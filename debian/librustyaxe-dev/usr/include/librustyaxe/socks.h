//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef LIBRUSTYAXE_SOCKS_H
#define LIBRUSTYAXE_SOCKS_H
#include <stdbool.h>
#include <stdint.h>
/* RustyAxe's SDK features must not come from an application's build_config.h. */
#include <librustyaxe/build_config.h>
#if defined(USE_MONGOOSE) || LIBRUSTYAXE_HAVE_MONGOOSE
/* Opaque legacy transport handle, never expose Mongoose's private API. */
struct mg_connection;

typedef struct {
   char url[1024];
   char host[256], user[256], pass[256];
   unsigned port, stage;
   long long deadline_ms;
} rr_socks_t;

/* A nonempty SOCKS5 URL is required. Host is unbracketed, port must be nonzero. SOCKS5/SOCKS5H both resolve destination names at the proxy. False is fatal:
 * callers must never fall back to a direct destination connection. */
bool rr_socks_init(rr_socks_t *, const char *url, const char *user, const char *pass, const char *host, uint16_t port);
/* 0 negotiating, 1 tunnel just opened, -1 failed. Failures close the socket;
 * do not deliver application data until this returns 1. The deadline includes proxy resolution/connect time. Uses the shared monotonic clock. */
int rr_socks_event(rr_socks_t *, struct mg_connection *, int event);
#endif
#endif
