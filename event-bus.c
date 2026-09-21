//
// librustyaxe/event-bus.c: Here we implement a way to hook various events by name
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// A module will register its interest in an event by calling event_on().
//
// The listener registry is thread-safe. Synchronous callbacks execute in
// the emitter's thread. Dispatched callbacks are handed to a caller-supplied
// dispatcher, allowing things such as GTK to marshal callbacks onto their
// own event-loop thread without making librustyaxe depend on GLib.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

#define EVENT_NOMATCH "NOMATCH"

typedef struct event_entry {
   event_listener_t *listeners;
   size_t listener_count;

   event_binary_listener_t *binary_listeners;
   size_t binary_listener_count;
#ifdef USE_PROFILING
   uint64_t emit_count;
#endif
} event_entry_t;

static dict *event_store = NULL;
static pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef USE_PROFILING
typedef struct {
   const char *event;
   uint64_t count;
} event_profile_row_t;

static time_t event_profile_last_dump;

static int event_profile_compare(const void *a, const void *b) {
   const event_profile_row_t *left = a;
   const event_profile_row_t *right = b;
   if (left->count < right->count) return 1;
   if (left->count > right->count) return -1;
   return strcmp(left->event, right->event);
}

static void event_profile_dump_locked(void) {
   const time_t current = time(NULL);
   if (event_profile_last_dump && current - event_profile_last_dump < 300) {
      return;
   }
   event_profile_last_dump = current;

   size_t capacity = event_store ? event_store->used : 0;
   event_profile_row_t *rows = capacity ? xcalloc(capacity, sizeof(*rows)) : NULL;
   size_t used = 0;
   int rank = 0;
   const char *key = NULL;
   dict_value_t value;
   val_type_t type;

   while (event_store && rows &&
          (rank = dict_enumerate_typed(event_store, rank, &key, &value, &type)) >= 0) {
      if (type == VAL_PTR && value.p) {
         event_entry_t *entry = value.p;
         if (entry->emit_count) {
            rows[used].event = key;
            rows[used].count = entry->emit_count;
            used++;
         }
      }
   }
   if (used > 1) {
      qsort(rows, used, sizeof(*rows), event_profile_compare);
   }
   Log(LOG_INFO, "profile", "Event dispatch counts (cumulative):");
   size_t limit = used < 32 ? used : 32;
   for (size_t i = 0; i < limit; i++) {
      Log(LOG_INFO, "profile", "  %s: %llu", rows[i].event,
         (unsigned long long)rows[i].count);
   }
   free(rows);
}
#endif

typedef struct queued_event {
   event_cb_t cb;
   void *user;

   char *event;
   char *data;

   rrconn_t *cptr;
} queued_event_t;

typedef struct queued_binary_event {
   event_binary_cb_t cb;
   void *user;

   char *event;
   void *data;
   size_t len;

   rrconn_t *cptr;
} queued_binary_event_t;

/*
 * Look up an event entry.
 *
 * Caller must hold event_lock.
 */
static event_entry_t *event_lookup_locked(const char *event) {
   if (!event_store || !event) {
      return NULL;
   }

   return dict_get_ptr(event_store, event, NULL);
}


/*
 * Look up or create an event entry.
 *
 * Caller must hold event_lock.
 */
static event_entry_t *event_get_or_create_locked(const char *event) {
   event_entry_t *entry;

   entry = event_lookup_locked(event);

   if (entry) {
      return entry;
   }

   entry = xcalloc(1, sizeof(*entry));

   if (dict_add_ptr(event_store, event, entry) != 0) {
      free(entry);
      return NULL;
   }

   return entry;
}


/*
 * Free an event entry and its listener arrays.
 *
 * Listener structures are stored directly in the arrays, so there are no
 * individual listener allocations to release.
 */
static void event_entry_free(event_entry_t *entry) {
   if (!entry) {
      return;
   }

   free(entry->listeners);
   free(entry->binary_listeners);
   free(entry);
}


/*
 * Remove an empty event entry.
 *
 * Caller must hold event_lock.
 */
static void event_remove_if_empty_locked(
      const char *event,
      event_entry_t *entry) {
   if (!event || !entry) {
      return;
   }

   if (entry->listener_count != 0 ||
       entry->binary_listener_count != 0) {
      return;
   }

   /*
    * dict_del() frees the dictionary key but VAL_PTR does not free the
    * pointed-to object, so free the entry ourselves.
    */
   dict_del(event_store, event);
   event_entry_free(entry);
}


/*
 * Run a queued text callback in the destination dispatcher/thread.
 */
static void event_dispatch_run(void *arg) {
   queued_event_t *queued = arg;

   if (!queued) {
      return;
   }

   queued->cb(
      queued->event,
      queued->data,
      queued->cptr,
      queued->user
   );

   free(queued->event);
   free(queued->data);
   free(queued);
}


/*
 * Run a queued binary callback in the destination dispatcher/thread.
 */
static void event_dispatch_binary_run(void *arg) {
   queued_binary_event_t *queued = arg;

   if (!queued) {
      return;
   }

   queued->cb(
      queued->event,
      queued->data,
      queued->len,
      queued->cptr,
      queued->user
   );

   free(queued->event);
   free(queued->data);
   free(queued);
}


/*
 * Deliver one text event to one listener.
 */
static void event_fire_listener(
      const event_listener_t *listener,
      const char *event,
      rrconn_t *cptr,
      const char *data) {
   if (!listener || !listener->cb) {
      return;
   }

   if (!listener->dispatch) {
      Log(LOG_CRAZY, "event",
         "Firing event %s from cptr:<%p> with data:<%p> user:<%p>",
         event, cptr, data, listener->user);

      listener->cb(event, data, cptr, listener->user);
      return;
   }

   queued_event_t *queued = xcalloc(1, sizeof(*queued));

   queued->cb = listener->cb;
   queued->user = listener->user;
   queued->event = xstrdup(event);
   queued->data = data ? xstrdup(data) : NULL;
   queued->cptr = cptr;

   Log(LOG_CRAZY, "event",
      "Dispatching event %s from cptr:<%p> user:<%p>",
      event, cptr, listener->user);

   listener->dispatch(
      event_dispatch_run,
      queued,
      listener->dispatch_user
   );
}


/*
 * Deliver one binary event to one listener.
 */
static void event_fire_binary_listener(
      const event_binary_listener_t *listener,
      const char *event,
      rrconn_t *cptr,
      const void *data,
      size_t len) {
   if (!listener || !listener->cb) {
      return;
   }

   if (!listener->dispatch) {
      Log(LOG_CRAZY, "event",
         "Firing binary event %s from cptr:<%p> with data:<%p> "
         "len %zu user:<%p>",
         event, cptr, data, len, listener->user);

      listener->cb(
         event,
         data,
         len,
         cptr,
         listener->user
      );
      return;
   }

   queued_binary_event_t *queued =
      xcalloc(1, sizeof(*queued));

   queued->cb = listener->cb;
   queued->user = listener->user;
   queued->event = xstrdup(event);
   queued->len = len;
   queued->cptr = cptr;

   if (data && len) {
      queued->data = xmalloc(len);
      memcpy(queued->data, data, len);
   }

   Log(LOG_CRAZY, "event",
      "Dispatching binary event %s from cptr:<%p> "
      "len %zu user:<%p>",
      event, cptr, len, listener->user);

   listener->dispatch(
      event_dispatch_binary_run,
      queued,
      listener->dispatch_user
   );
}


/*
 * Snapshot the text listener array.
 *
 * This is what allows event_off() or event_on() to safely execute while
 * callbacks are running. The snapshot contains listener structures by
 * value, not pointers into the live registry.
 *
 * Caller must hold event_lock.
 */
static event_listener_t *event_snapshot_locked(
      event_entry_t *entry,
      size_t *count) {
   event_listener_t *snapshot;

   *count = 0;

   if (!entry || !entry->listener_count) {
      return NULL;
   }

   snapshot = xmalloc(
      sizeof(*snapshot) * entry->listener_count
   );

   memcpy(
      snapshot,
      entry->listeners,
      sizeof(*snapshot) * entry->listener_count
   );

   *count = entry->listener_count;

   return snapshot;
}


/*
 * Snapshot the binary listener array.
 *
 * Caller must hold event_lock.
 */
static event_binary_listener_t *event_binary_snapshot_locked(
      event_entry_t *entry,
      size_t *count) {
   event_binary_listener_t *snapshot;

   *count = 0;

   if (!entry || !entry->binary_listener_count) {
      return NULL;
   }

   snapshot = xmalloc(
      sizeof(*snapshot) * entry->binary_listener_count
   );

   memcpy(
      snapshot,
      entry->binary_listeners,
      sizeof(*snapshot) * entry->binary_listener_count
   );

   *count = entry->binary_listener_count;

   return snapshot;
}


void event_init(void) {
   pthread_mutex_lock(&event_lock);

   if (!event_store) {
      event_store = dict_new();
   }

   pthread_mutex_unlock(&event_lock);
}


/*
 * Subscribe to a synchronous text event.
 */
void event_on(
      const char *event,
      event_cb_t cb,
      void *user) {
   event_on_dispatch(event, cb, user, NULL, NULL);
}


/*
 * Subscribe to a text event using a dispatcher.
 */
void event_on_dispatch(
      const char *event,
      event_cb_t cb,
      void *user,
      event_dispatch_t dispatch,
      void *dispatch_user) {
   event_entry_t *entry;

   if (!event || !cb) {
      return;
   }

   pthread_mutex_lock(&event_lock);

   if (!event_store) {
      pthread_mutex_unlock(&event_lock);
      return;
   }

   entry = event_get_or_create_locked(event);

   if (!entry) {
      pthread_mutex_unlock(&event_lock);
      return;
   }

   entry->listeners = xrealloc(
      entry->listeners,
      sizeof(*entry->listeners) *
         (entry->listener_count + 1)
   );

   event_listener_t *listener =
      &entry->listeners[entry->listener_count++];

   memset(listener, 0, sizeof(*listener));

   listener->cb = cb;
   listener->user = user;
   listener->dispatch = dispatch;
   listener->dispatch_user = dispatch_user;

   pthread_mutex_unlock(&event_lock);
}


/*
 * Subscribe to a synchronous binary event.
 */
void event_on_binary(
      const char *event,
      event_binary_cb_t cb,
      void *user) {
   event_on_binary_dispatch(
      event,
      cb,
      user,
      NULL,
      NULL
   );
}


/*
 * Compatibility alias.
 */
void event_register_binary(
      const char *event,
      event_binary_cb_t cb,
      void *user) {
   event_on_binary(event, cb, user);
}


/*
 * Subscribe to a binary event using a dispatcher.
 */
void event_on_binary_dispatch(
      const char *event,
      event_binary_cb_t cb,
      void *user,
      event_dispatch_t dispatch,
      void *dispatch_user) {
   event_entry_t *entry;

   if (!event || !cb) {
      return;
   }

   pthread_mutex_lock(&event_lock);

   if (!event_store) {
      pthread_mutex_unlock(&event_lock);
      return;
   }

   entry = event_get_or_create_locked(event);

   if (!entry) {
      pthread_mutex_unlock(&event_lock);
      return;
   }

   entry->binary_listeners = xrealloc(
      entry->binary_listeners,
      sizeof(*entry->binary_listeners) *
         (entry->binary_listener_count + 1)
   );

   event_binary_listener_t *listener =
      &entry->binary_listeners[
         entry->binary_listener_count++
      ];

   memset(listener, 0, sizeof(*listener));

   listener->cb = cb;
   listener->user = user;
   listener->dispatch = dispatch;
   listener->dispatch_user = dispatch_user;

   pthread_mutex_unlock(&event_lock);
}


void event_emit(
      const char *event,
      rrconn_t *cptr,
      const char *data) {
   event_listener_t *snapshot = NULL;
   size_t count = 0;

   if (!event) {
      return;
   }

   Log(LOG_CRAZY, "event",
      "send event %s: %s",
      event, data ? data : "(null)");

   pthread_mutex_lock(&event_lock);

   if (event_store) {
      event_entry_t *entry =
         event_lookup_locked(event);

#ifdef USE_PROFILING
      if (entry) {
         entry->emit_count++;
         event_profile_dump_locked();
      }
#endif

      snapshot = event_snapshot_locked(
         entry,
         &count
      );
   }

   pthread_mutex_unlock(&event_lock);

   if (count) {
      for (size_t i = 0 ; i < count ; i++) {
         event_fire_listener(
            &snapshot[i],
            event,
            cptr,
            data
         );
      }

      Log(LOG_CRAZY, "event.match",
         "Event %s from cptr:<%p> hit %zu times",
         event, cptr, count);

      free(snapshot);
      return;
   }

   free(snapshot);

   /*
    * No normal listener matched. Snapshot NOMATCH separately.
    */
   pthread_mutex_lock(&event_lock);

   if (event_store) {
      event_entry_t *nomatch =
         event_lookup_locked(EVENT_NOMATCH);

      snapshot = event_snapshot_locked(
         nomatch,
         &count
      );
   }

   pthread_mutex_unlock(&event_lock);

   if (count) {
      if (cptr && cptr->chatname) {
         Log(LOG_CRAZY, "event.nomatch",
            "Event %s from cptr:<%p> didn't match; "
            "firing NOMATCH",
            event, cptr);
      } else {
         Log(LOG_CRAZY, "event.nomatch",
            "Event %s didn't match; firing NOMATCH",
            event);
      }

      for (size_t i = 0 ; i < count ; i++) {
         event_fire_listener(
            &snapshot[i],
            event,
            cptr,
            data
         );
      }
   } else {
      Log(LOG_CRAZY, "event.nomatch",
         "Event %s from cptr:<%p> didn't match anything. "
         "data: |%s|",
         event,
         cptr,
         data ? data : "(null)");
   }

   free(snapshot);
}


void event_emit_binary(
      const char *event,
      rrconn_t *cptr,
      const void *data,
      size_t len) {
   event_binary_listener_t *snapshot = NULL;
   size_t count = 0;

   if (!event) {
      return;
   }

   Log(LOG_CRAZY, "event",
      "send binary event %s: %zu bytes",
      event, len);

   pthread_mutex_lock(&event_lock);

   if (event_store) {
      event_entry_t *entry =
         event_lookup_locked(event);

#ifdef USE_PROFILING
      if (entry) {
         entry->emit_count++;
         event_profile_dump_locked();
      }
#endif

      snapshot = event_binary_snapshot_locked(
         entry,
         &count
      );
   }

   pthread_mutex_unlock(&event_lock);

   for (size_t i = 0 ; i < count ; i++) {
      event_fire_binary_listener(
         &snapshot[i],
         event,
         cptr,
         data,
         len
      );
   }

   free(snapshot);

   if (!count) {
      Log(LOG_CRAZY, "event.nomatch",
         "Binary event %s from cptr:<%p> "
         "didn't match anything (%zu bytes)",
         event, cptr, len);
   } else {
      Log(LOG_CRAZY, "event.match",
         "Binary event %s from cptr:<%p> hit %zu times",
         event, cptr, count);
   }
}


void event_emit_dict(
      const char *event,
      rrconn_t *cptr,
      dict *data) {
   const char *jp = NULL;

   if (data) {
      jp = dict2json(data);
   }

   if (jp) {
      event_emit(event, cptr, jp);
      free((void *)jp);
   }
}


/*
 * Remove matching text listeners.
 *
 * As with the old implementation:
 *
 *    cb == NULL    matches any callback
 *    user == NULL  matches any user pointer
 */
void event_off(
      const char *event,
      event_cb_t cb,
      void *user) {
   event_entry_t *entry;

   if (!event) {
      return;
   }

   pthread_mutex_lock(&event_lock);

   if (!event_store) {
      pthread_mutex_unlock(&event_lock);
      return;
   }

   entry = event_lookup_locked(event);

   if (!entry) {
      pthread_mutex_unlock(&event_lock);
      return;
   }

   for (size_t i = 0 ; i < entry->listener_count ; ) {
      event_listener_t *listener =
         &entry->listeners[i];

      if ((!cb || listener->cb == cb) &&
          (!user || listener->user == user)) {
         memmove(
            &entry->listeners[i],
            &entry->listeners[i + 1],
            (entry->listener_count - i - 1) *
               sizeof(*entry->listeners)
         );

         entry->listener_count--;
         continue;
      }

      i++;
   }

   if (!entry->listener_count) {
      free(entry->listeners);
      entry->listeners = NULL;
   }

   event_remove_if_empty_locked(event, entry);

   pthread_mutex_unlock(&event_lock);
}


/*
 * Remove matching binary listeners.
 */
void event_off_binary(
      const char *event,
      event_binary_cb_t cb,
      void *user) {
   event_entry_t *entry;

   if (!event) {
      return;
   }

   pthread_mutex_lock(&event_lock);

   if (!event_store) {
      pthread_mutex_unlock(&event_lock);
      return;
   }

   entry = event_lookup_locked(event);

   if (!entry) {
      pthread_mutex_unlock(&event_lock);
      return;
   }

   for (size_t i = 0 ;
        i < entry->binary_listener_count ; ) {
      event_binary_listener_t *listener =
         &entry->binary_listeners[i];

      if ((!cb || listener->cb == cb) &&
          (!user || listener->user == user)) {
         memmove(
            &entry->binary_listeners[i],
            &entry->binary_listeners[i + 1],
            (entry->binary_listener_count - i - 1) *
               sizeof(*entry->binary_listeners)
         );

         entry->binary_listener_count--;
         continue;
      }

      i++;
   }

   if (!entry->binary_listener_count) {
      free(entry->binary_listeners);
      entry->binary_listeners = NULL;
   }

   event_remove_if_empty_locked(event, entry);

   pthread_mutex_unlock(&event_lock);
}


/*
 * Optional cleanup.
 *
 * event_store contains VAL_PTR values, so dict_free() deliberately does
 * not free the event_entry_t objects. Enumerate and release them first.
 *
 * Do not call event_shutdown() while dispatched callbacks remain queued
 * if their user/dispatch_user objects are being destroyed at the same
 * time. The queued callback itself owns its copied event payload, but
 * user pointers remain borrowed.
 */
void event_shutdown(void) {
   pthread_mutex_lock(&event_lock);

   if (!event_store) {
      pthread_mutex_unlock(&event_lock);
      return;
   }

   const char *key;
   dict_value_t val;
   val_type_t type;
   int rank = 0;

   while ((rank = dict_enumerate_typed(
              event_store,
              rank,
              &key,
              &val,
              &type)) >= 0) {
      if (type == VAL_PTR) {
         event_entry_free(val.p);
      }
   }

   dict_free(event_store);
   event_store = NULL;

   pthread_mutex_unlock(&event_lock);
}
