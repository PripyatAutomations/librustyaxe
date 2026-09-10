//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__librustyaxe_event_bus_h)
#define	__librustyaxe_event_bus_h

typedef void (*event_cb_t)(const char *event, const char *data, rrconn_t *cptr, void *user);

// Binary payload event callback: data points at the raw binary
// payload, len is its length. Data is owned by the emitter and is only
// valid for the duration of the callback.
typedef void (*event_binary_cb_t)(const char *event, const void *data, size_t len, rrconn_t *cptr, void *user);

typedef struct event_listener {
   event_cb_t cb;
   void *user;
} event_listener_t;

typedef struct event_binary_listener {
   event_binary_cb_t cb;
   void *user;
} event_binary_listener_t;

extern void event_init(void);
extern void event_on(const char *event, event_cb_t cb, void *user);

// Binary events: separate listener list per event so a name can have
// both text and binary listeners. Listeners registered with
// event_on_binary() are ONLY fired by event_emit_binary().
extern void event_on_binary(const char *event, event_binary_cb_t cb, void *user);
extern void event_register_binary(const char *event, event_binary_cb_t cb, void *user);  // alias of event_on_binary()
extern void event_emit_binary(const char *event, rrconn_t *cptr, const void *data, size_t len);

extern void event_emit(const char *event, rrconn_t *cptr, const char *data);
extern void event_emit_dict(const char *event, rrconn_t *cptr, dict *data);
extern void event_off(const char *event, event_cb_t cb, void *user);
extern void event_shutdown(void);

#endif // #defined __librustyaxe_event_bus_h
