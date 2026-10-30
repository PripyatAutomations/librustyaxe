//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librustyaxe/transport.mongoose.h>
#include <librustyaxe/core.h>
#include "build_config.h"
#ifdef USE_MONGOOSE
#include <ext/libmongoose/mongoose.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

struct mongoose_transport {
   struct mg_connection *socket; /* borrowed from the owning mg_mgr */
};
static uint32_t capabilities(void *ctx) {
   struct mongoose_transport *m = ctx;
   /* A successful WS upgrade on a TLS socket establishes encryption.
    * is_tls alone never proves the identity of the remote peer. */
   uint32_t flags = RR_TRANSPORT_RELIABLE_MESSAGES | RR_TRANSPORT_BINARY_MESSAGES;

   if (m && m->socket && m->socket->is_tls && m->socket->is_websocket && !m->socket->is_closing) {
      flags |= RR_TRANSPORT_TLS_ENCRYPTED;
   }

   return flags;
}
static int send_frame(void *ctx, const uint8_t *data, size_t len, int opcode) {
   struct mongoose_transport *m = ctx;
   struct mg_connection *c = m->socket;

   if (!c || c->is_closing || c->is_draining || !c->is_websocket) {
      Log(LOG_WARN, "transport.mongoose",
          "WebSocket send blocked: socket=%p closing=%d draining=%d websocket=%d queued=%zu payload=%zu",
          (void *)c, c ? (int)c->is_closing : -1, c ? (int)c->is_draining : -1,
          c ? (int)c->is_websocket : -1, c ? c->send.len : 0, len);
      return RR_TRANSPORT_CLOSED_ERROR;
   }

   if ((!data && len) || len > SIZE_MAX - 14) {
      return RR_TRANSPORT_INVALID;
   }
   size_t overhead = 2 + (len < 126 ? 0 : len <= 65535 ? 2 : 8) + (c->is_client ? 4 : 0);

   if (c->send.len > SIZE_MAX - len - overhead) {
      Log(LOG_WARN, "transport.mongoose",
          "WebSocket frame size overflow: socket=%p queued=%zu payload=%zu overhead=%zu limit=%zu",
          (void *)c, c->send.len, len, overhead, (size_t)SIZE_MAX);
      return RR_TRANSPORT_BACKPRESSURE;
   }

   /* Mongoose versions differ in how mg_ws_send reports queued bytes.
    * Verify the actual buffer growth instead of interpreting its return
    * value as the size of this frame. In particular, preexisting queued
    * frames must not turn a successful send into a false failure. */
   size_t before = c->send.len;
   size_t result = mg_ws_send(c, data, len, opcode);
   if (c->send.len >= before && c->send.len - before == len + overhead)
      return RR_TRANSPORT_OK;
   Log(LOG_WARN, "transport.mongoose",
       "WebSocket send did not queue frame: socket=%p return=%zu queued-before=%zu queued-after=%zu payload=%zu overhead=%zu closing=%d draining=%d websocket=%d",
       (void *)c, result, before, c->send.len, len, overhead,
       (int)c->is_closing, (int)c->is_draining, (int)c->is_websocket);
   return RR_TRANSPORT_IO_ERROR;
}
static int send_text(void *ctx, const uint8_t *data, size_t len) {
   return send_frame(ctx, data, len, WEBSOCKET_OP_TEXT);
}
static int send_binary(void *ctx, const uint8_t *data, size_t len) {
   return send_frame(ctx, data, len, WEBSOCKET_OP_BINARY);
}
static size_t queued(void *ctx) {
   struct mongoose_transport *m = ctx;

   return m->socket ? m->socket->send.len : 0;
}
static void closing(void *ctx) {
   struct mongoose_transport *m = ctx;

   if (m->socket) {
      m->socket->is_closing = 1;
   }
}

static const rr_transport_driver_t mongoose_ops = {
   .abi_version = RR_TRANSPORT_ABI_VERSION,
   .capabilities = RR_TRANSPORT_RELIABLE_MESSAGES | RR_TRANSPORT_BINARY_MESSAGES | RR_TRANSPORT_TLS_ENCRYPTED,
   .connection_capabilities = capabilities,
   .send_message = send_text, .send_binary_message = send_binary,
   .queued_bytes = queued, .close = closing
};
rr_transport_t *rr_transport_mongoose_adopt(void *connection, rr_transport_callback_t callback, void *context) {
   if (!connection) {
      return NULL;
   }
   struct mongoose_transport *m = calloc(1, sizeof(*m));

   if (!m) {
      return NULL;
   }
   m->socket = connection;
   rr_transport_t *t = rr_transport_new(&mongoose_ops, m, callback, context);

   if (!t) {
      free(m);
   }

   return t;
}
void rr_transport_mongoose_open(rr_transport_t *t) {
   if (t) {
      rr_transport_emit(t, RR_TRANSPORT_EVENT_OPEN, NULL, NULL, 0);
   }
}
void rr_transport_mongoose_closed(rr_transport_t *t) {
   if (!t) {
      return;
   }
   struct mongoose_transport *m = rr_transport_driver_context(t);

   if (m) {
      m->socket = NULL;
   }
   rr_transport_emit(t, RR_TRANSPORT_EVENT_CLOSE, NULL, NULL, 0);
}
void rr_transport_mongoose_release(rr_transport_t *t) {
   if (!t || rr_transport_state(t) != RR_TRANSPORT_CLOSED) {
      return;
   }
   void *m = rr_transport_driver_context(t);
   rr_transport_free(t);
   free(m);
}
#else
rr_transport_t *rr_transport_mongoose_adopt(void *connection, rr_transport_callback_t callback, void *context) {
   (void)connection;
   (void)callback;
   (void)context;

   return NULL;
}
void rr_transport_mongoose_open(rr_transport_t *transport) {
   (void)transport;
}
void rr_transport_mongoose_closed(rr_transport_t *transport) {
   (void)transport;
}
void rr_transport_mongoose_release(rr_transport_t *transport) {
   (void)transport;
}
#endif
