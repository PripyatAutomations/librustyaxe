#ifndef RR_TRANSPORT_H
#define RR_TRANSPORT_H
/* Reusable transport ABI: no GLib, Mongoose or POSIX types in this interface.
 * Drivers may target MCU sockets, WebSocket, QUIC or WebTransport. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rr_transport rr_transport_t;
typedef struct rr_transport_channel rr_transport_channel_t;

enum rr_transport_capability {
   RR_TRANSPORT_RELIABLE_MESSAGES = 1u << 0,
   RR_TRANSPORT_ORDERED_STREAMS   = 1u << 1,
   RR_TRANSPORT_DATAGRAMS         = 1u << 2,
   RR_TRANSPORT_TLS_AUTHENTICATED = 1u << 3,
   RR_TRANSPORT_PEER_SPKI         = 1u << 4,
   RR_TRANSPORT_MULTIPLEX         = 1u << 5,
   RR_TRANSPORT_BINARY_MESSAGES  = 1u << 6
};

enum rr_transport_state {
   RR_TRANSPORT_CONNECTING,
   RR_TRANSPORT_OPEN,
   RR_TRANSPORT_CLOSING,
   RR_TRANSPORT_CLOSED
};

enum rr_transport_event {
   RR_TRANSPORT_EVENT_OPEN,
   RR_TRANSPORT_EVENT_MESSAGE,
   RR_TRANSPORT_EVENT_DATAGRAM,
   RR_TRANSPORT_EVENT_STREAM_DATA,
   RR_TRANSPORT_EVENT_CLOSE,
   RR_TRANSPORT_EVENT_ERROR,
   RR_TRANSPORT_EVENT_PONG,
   RR_TRANSPORT_EVENT_BINARY_MESSAGE
};

enum rr_transport_status {
   RR_TRANSPORT_OK = 0,
   RR_TRANSPORT_UNSUPPORTED = -1,
   RR_TRANSPORT_INVALID = -2,
   RR_TRANSPORT_CLOSED_ERROR = -3,
   RR_TRANSPORT_BACKPRESSURE = -4,
   RR_TRANSPORT_IO_ERROR = -5
};

/* Borrowed data and peer identity are valid only during their callback.
 * A driver must not fire callbacks after close has completed. */
typedef void (*rr_transport_callback_t)(rr_transport_t *transport,
   enum rr_transport_event event, rr_transport_channel_t *channel,
   const uint8_t *data, size_t len, void *context);

typedef struct rr_transport_driver {
   uint32_t abi_version;
   uint32_t capabilities;
   /* Drivers with route-specific authentication may narrow capabilities
    * after establishment; static capabilities are the upper bound. */
   uint32_t (*connection_capabilities)(void *driver);
   /* driver-owned connection context is handed in at creation */
   int (*send_message)(void *driver, const uint8_t *data, size_t len);
   int (*send_binary_message)(void *driver, const uint8_t *data, size_t len);
   size_t (*queued_bytes)(void *driver);
   int (*send_datagram)(void *driver, const uint8_t *data, size_t len);
   int (*open_stream)(void *driver, uint64_t *stream_id);
   int (*send_stream)(void *driver, uint64_t stream_id,
                      const uint8_t *data, size_t len, bool fin);
   /* Returns the hash of the authenticated TLS peer SPKI, not an advertised
    * or self-reported identity. Exactly 32 raw SHA-256 bytes. */
   bool (*peer_spki_sha256)(void *driver, uint8_t output[32]);
   /* Begin closing; driver emits CLOSE after its final I/O callback. */
   void (*close)(void *driver);
} rr_transport_driver_t;

#define RR_TRANSPORT_ABI_VERSION 2u

rr_transport_t *rr_transport_new(const rr_transport_driver_t *ops,
   void *driver, rr_transport_callback_t callback, void *context);
uint32_t rr_transport_capabilities(const rr_transport_t *transport);
enum rr_transport_state rr_transport_state(const rr_transport_t *transport);
int rr_transport_send_message(rr_transport_t *transport, const void *data, size_t len);
int rr_transport_send_binary_message(rr_transport_t *transport, const void *data, size_t len);
size_t rr_transport_queued_bytes(rr_transport_t *transport);
int rr_transport_send_datagram(rr_transport_t *transport, const void *data, size_t len);
int rr_transport_open_stream(rr_transport_t *transport, uint64_t *stream_id);
int rr_transport_send_stream(rr_transport_t *transport, uint64_t stream_id,
   const void *data, size_t len, bool fin);
bool rr_transport_peer_spki_sha256(rr_transport_t *transport, uint8_t output[32]);
/* Called by driver on its event loop. Events are not dispatched across threads. */
void rr_transport_emit(rr_transport_t *transport, enum rr_transport_event event,
   rr_transport_channel_t *channel, const void *data, size_t len);
void rr_transport_close(rr_transport_t *transport);
/* Free only after CLOSE. If still live, starts close and retains ownership
 * until the caller receives CLOSE and calls free again. */
void rr_transport_free(rr_transport_t *transport);
void *rr_transport_driver_context(rr_transport_t *transport);

#ifdef __cplusplus
}
#endif
#endif
