//
// event-bus.c: Here we implement a way to hook various events by name
//
// A module will register it's interest in an event by calling event_on()

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

#define	EVENT_NOMATCH "NOMATCH"

static kv_store_t *event_store = NULL;

static void event_fire_list(kv_list_t *list, const char *event, rrconn_t *cptr, const char *data) {
   if (!list) {
      return;
   }

   for (size_t i = 0 ; i < list->count ; i++) {
      event_listener_t *l = ( (void **)list->ptr )[i];

      Log(LOG_CRAZY, "event", "Firing event %s from cptr:<%p> with data:<%p> user:%s", event, cptr, data, l->user);

      l->cb(event, data, cptr, l->user);
   }
}

void event_init(void) {
   if (!event_store) {
      event_store = kv_create(65536, KV_BST);
   }
}

/* subscribe */
void event_on(const char *event, event_cb_t cb, void *user) {
   if (!event_store || !event) {
      return;
   }

   kv_list_t *list = kv_lookup(event_store, event);

   if (!list) {
      list = calloc( 1, sizeof(*list) );

      // XXX: Make this more graceful
      if (!list) {
         abort();
      }
      list->type = KV_ARRAY;
      kv_insert(event_store, event, list);
   }

   event_listener_t *l = calloc( 1, sizeof(*l) );

   // XXX: make this more graceful
   if (!l) {
      abort();
   }
   l->cb = cb;
   l->user = user;

   list->ptr = realloc( list->ptr, sizeof(void*) * (list->count + 1) );

   // XXX: make this more graceful
   if (!list->ptr) {
      abort();
   }
   ( (void**)list->ptr )[list->count++] = l;
}

void event_emit(const char *event, rrconn_t *cptr, const char *data) {
   if (!event_store || !event) {
      return;
   }

   kv_list_t *list = kv_lookup(event_store, event);
   int evt_hits = 0;

   if (list) {
      evt_hits = list->count;
      event_fire_list(list, event, cptr, data);
   }

   if (evt_hits == 0) {
      kv_list_t *nomatch = kv_lookup(event_store, EVENT_NOMATCH);

      if (nomatch) {
         Log(LOG_DEBUG, "event", "Event %s from cptr:<%p> didn't match; firing NOMATCH", event, cptr);

         event_fire_list(nomatch, event, cptr, data);
      } else {
         Log(LOG_DEBUG, "event", "Event %s from cptr:<%p> didn't match anything. data: |%s|", event, cptr, data);
      }
   } else {
      Log(LOG_CRAZY, "event", "Event %s from cptr:<%p> hit %d times", event, cptr, evt_hits);
   }
}

void event_emit_dict(const char *event, rrconn_t *cptr, dict *data) {
   const char *jp = NULL;

   if (data) {
      jp = dict2json(data);
   }
   event_emit(event, cptr, jp);

   if (jp) {
      free( (void *)jp );
   }
}

/* unsubscribe */
void event_off(const char *event, event_cb_t cb, void *user) {
   if (!event_store || !event) {
      return;
   }
   kv_list_t *list = kv_lookup(event_store, event);

   if (!list) {
      return;
   }

   for (size_t i = 0 ; i < list->count ; ) {
      event_listener_t *l = ( (void**)list->ptr )[i];

      if ( (!cb || l->cb == cb) && (!user || l->user == user) ) {
         free(l);
         memmove( &( (void**)list->ptr )[i], &( (void**)list->ptr )[i + 1], (list->count - i - 1) * sizeof(void*) );
         list->count--;
         continue;
      }
      i++;
   }

   if (list->count == 0) {
      kv_remove(event_store, event);
      free(list->ptr);
      free(list);
   }
}

/* optional cleanup */
void event_shutdown(void) {
   if (!event_store) {
      return;
   }
   kv_destroy(event_store);
   event_store = NULL;
}
