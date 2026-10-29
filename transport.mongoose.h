#ifndef RR_TRANSPORT_MONGOOSE_H
#define RR_TRANSPORT_MONGOOSE_H
#include <librustyaxe/transport.h>
/* Mongoose is deliberately opaque to consumers of this adapter.
 * The owning event loop delivers upgrade/close notifications below. */
rr_transport_t *rr_transport_mongoose_adopt(void *connection,
   rr_transport_callback_t callback, void *context);
void rr_transport_mongoose_open(rr_transport_t *transport);
void rr_transport_mongoose_closed(rr_transport_t *transport);
void rr_transport_mongoose_release(rr_transport_t *transport);
#endif
