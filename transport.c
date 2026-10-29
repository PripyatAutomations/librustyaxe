//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "transport.h"
#include <stdlib.h>

struct rr_transport {
   const rr_transport_driver_t *ops;
   void *driver;
   rr_transport_callback_t callback;
   void *context;
   enum rr_transport_state state;
};

rr_transport_t *rr_transport_new(const rr_transport_driver_t *ops, void *driver, rr_transport_callback_t callback, void *context) {
   if (!ops || ops->abi_version != RR_TRANSPORT_ABI_VERSION) {
      return NULL;
   }
   rr_transport_t *t = calloc(1, sizeof(*t));

   if (!t) {
      return NULL;
   }
   t->ops = ops;
   t->driver = driver;
   t->callback = callback;
   t->context = context;
   t->state = RR_TRANSPORT_CONNECTING;

   return t;
}
uint32_t rr_transport_capabilities(const rr_transport_t *t) {
   return t ? (t->ops->connection_capabilities ? t->ops->connection_capabilities(t->driver) & t->ops->capabilities : t->ops->capabilities) : 0;
}
enum rr_transport_state rr_transport_state(const rr_transport_t *t) {
   return t ? t->state : RR_TRANSPORT_CLOSED;
}
static int check(const rr_transport_t *t, uint32_t capability, const void *data, size_t len) {
   if (!t || (!data && len)) {
      return RR_TRANSPORT_INVALID;
   }

   if (t->state != RR_TRANSPORT_OPEN) {
      return RR_TRANSPORT_CLOSED_ERROR;
   }

   return (rr_transport_capabilities(t) & capability) ? RR_TRANSPORT_OK : RR_TRANSPORT_UNSUPPORTED;
}
int rr_transport_send_message(rr_transport_t *t, const void *data, size_t len) {
   int r = check(t, RR_TRANSPORT_RELIABLE_MESSAGES, data, len);

   return r ? r : t->ops->send_message ? t->ops->send_message(t->driver, data, len) : RR_TRANSPORT_UNSUPPORTED;
}
int rr_transport_send_binary_message(rr_transport_t *t, const void *data, size_t len) {
   int r = check(t, RR_TRANSPORT_BINARY_MESSAGES, data, len);

   return r ? r : t->ops->send_binary_message ? t->ops->send_binary_message(t->driver, data, len) : RR_TRANSPORT_UNSUPPORTED;
}
size_t rr_transport_queued_bytes(rr_transport_t *t) {
   return t && t->ops->queued_bytes ? t->ops->queued_bytes(t->driver) : 0;
}

/* Reactor-backed drivers deliver callbacks from their registered watchers;
 * poll is reserved for manually driven adapters. */
int rr_transport_poll(rr_transport_t *t, int timeout_ms) {
   if (!t || timeout_ms < 0) {
      return RR_TRANSPORT_INVALID;
   }

   if (t->state == RR_TRANSPORT_CLOSED) {
      return RR_TRANSPORT_CLOSED_ERROR;
   }

   return t->ops->poll ? t->ops->poll(t->driver, timeout_ms) : RR_TRANSPORT_UNSUPPORTED;
}
int rr_transport_send_datagram(rr_transport_t *t, const void *data, size_t len) {
   int r = check(t, RR_TRANSPORT_DATAGRAMS, data, len);

   return r ? r : t->ops->send_datagram ? t->ops->send_datagram(t->driver, data, len) : RR_TRANSPORT_UNSUPPORTED;
}
int rr_transport_open_stream(rr_transport_t *t, uint64_t *id) {
   int r = check(t, RR_TRANSPORT_ORDERED_STREAMS, NULL, 0);

   if (r) {
      return r;
   }

   if (!id) {
      return RR_TRANSPORT_INVALID;
   }

   return t->ops->open_stream ? t->ops->open_stream(t->driver, id) : RR_TRANSPORT_UNSUPPORTED;
}
int rr_transport_send_stream(rr_transport_t *t, uint64_t id, const void *data, size_t len, bool fin) {
   int r = check(t, RR_TRANSPORT_ORDERED_STREAMS, data, len);

   return r ? r : t->ops->send_stream ? t->ops->send_stream(t->driver, id, data, len, fin) : RR_TRANSPORT_UNSUPPORTED;
}
bool rr_transport_peer_spki_sha256(rr_transport_t *t, uint8_t output[32]) {
   return t && output && t->state == RR_TRANSPORT_OPEN &&
          (rr_transport_capabilities(t) & RR_TRANSPORT_TLS_AUTHENTICATED) &&
          (rr_transport_capabilities(t) & RR_TRANSPORT_PEER_SPKI) && t->ops->peer_spki_sha256 && t->ops->peer_spki_sha256(t->driver, output);
}
void rr_transport_emit(rr_transport_t *t, enum rr_transport_event event, rr_transport_channel_t *channel, const void *data, size_t len) {
   if (!t || t->state == RR_TRANSPORT_CLOSED) {
      return;
   }

   if (event == RR_TRANSPORT_EVENT_OPEN) {
      if (t->state != RR_TRANSPORT_CONNECTING) {
         return;
      }
      t->state = RR_TRANSPORT_OPEN;
   } else if (event == RR_TRANSPORT_EVENT_CLOSE) {
      t->state = RR_TRANSPORT_CLOSED;
   } else if (t->state != RR_TRANSPORT_OPEN && event != RR_TRANSPORT_EVENT_ERROR) {
      return;
   }

   if (t->callback) {
      t->callback(t, event, channel, data, len, t->context);
   }
}
void rr_transport_close(rr_transport_t *t) {
   if (!t || t->state == RR_TRANSPORT_CLOSED || t->state == RR_TRANSPORT_CLOSING) {
      return;
   }
   t->state = RR_TRANSPORT_CLOSING;

   if (t->ops->close) {
      t->ops->close(t->driver);
   } else {
      rr_transport_emit(t, RR_TRANSPORT_EVENT_CLOSE, NULL, NULL, 0);
   }
}
void rr_transport_free(rr_transport_t *t) {
   if (!t) {
      return;
   }

   if (t->state != RR_TRANSPORT_CLOSED) {
      rr_transport_close(t);

      return;
   }
   free(t);
}
void *rr_transport_driver_context(rr_transport_t *t) {
   return t ? t->driver : NULL;
}
