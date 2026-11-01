#include "log-queue.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
typedef struct {
   logpriority_t level;
   struct timespec submitted;
   char subsystem[64];
   char message[1024];
} ra_log_record_t;
static _Thread_local bool delivery_active;
static _Thread_local struct timespec delivery_timestamp;
bool ra_log_queue_delivery_time(struct timespec *timestamp) {
   if (!timestamp || !delivery_active) return false;
   *timestamp = delivery_timestamp;
   return true;
}
struct ra_log_queue {
   pthread_mutex_t lock;
   size_t capacity, head, tail, length;
   _Atomic uint64_t dropped, contention_dropped, full_dropped;
   ra_log_record_t records[];
};
ra_log_queue_t *ra_log_queue_new(size_t capacity) {
   if (!capacity || capacity > (SIZE_MAX - sizeof(ra_log_queue_t)) / sizeof(ra_log_record_t))
      return NULL;
   ra_log_queue_t *q = calloc(1, sizeof(*q) + capacity * sizeof(ra_log_record_t));
   if (!q) return NULL;
   if (pthread_mutex_init(&q->lock, NULL)) { free(q); return NULL; }
   q->capacity = capacity;
   return q;
}
void ra_log_queue_free(ra_log_queue_t *q) {
   if (!q) return;
   pthread_mutex_destroy(&q->lock);
   free(q);
}
bool ra_log_queue_push(ra_log_queue_t *q, logpriority_t level,
                       const char *subsys, const char *message) {
   if (!q || !subsys || !message) return false;
   if (pthread_mutex_trylock(&q->lock)) {
      atomic_fetch_add_explicit(&q->contention_dropped, 1, memory_order_relaxed);
      atomic_fetch_add_explicit(&q->dropped, 1, memory_order_relaxed);
      return false;
   }
   if (q->length == q->capacity) {
      atomic_fetch_add_explicit(&q->full_dropped, 1, memory_order_relaxed);
      atomic_fetch_add_explicit(&q->dropped, 1, memory_order_relaxed);
      pthread_mutex_unlock(&q->lock);
      return false;
   }
   ra_log_record_t *r = &q->records[q->tail];
   r->level = level;
   clock_gettime(CLOCK_REALTIME, &r->submitted);
   snprintf(r->subsystem, sizeof(r->subsystem), "%s", subsys);
   snprintf(r->message, sizeof(r->message), "%s", message);
   q->tail = (q->tail + 1) % q->capacity;
   q->length++;
   pthread_mutex_unlock(&q->lock);
   return true;
}
size_t ra_log_queue_drain(ra_log_queue_t *q, size_t max_records) {
   if (!q) return 0;
   size_t n = 0;
   while (n < max_records) {
      ra_log_record_t item;
      pthread_mutex_lock(&q->lock);
      if (!q->length) {
         pthread_mutex_unlock(&q->lock);
         break;
      }
      item = q->records[q->head];
      q->head = (q->head + 1) % q->capacity;
      q->length--;
      pthread_mutex_unlock(&q->lock);
      delivery_timestamp = item.submitted;
      delivery_active = true;
      Log(item.level, item.subsystem, "%s", item.message);
      delivery_active = false;
      n++;
   }
   return n;
}
uint64_t ra_log_queue_dropped(const ra_log_queue_t *q) {
   return q ? atomic_load_explicit(&q->dropped, memory_order_relaxed) : 0;
}

uint64_t ra_log_queue_contention_dropped(const ra_log_queue_t *q) {
   return q ? atomic_load_explicit(&q->contention_dropped, memory_order_relaxed) : 0;
}
uint64_t ra_log_queue_full_dropped(const ra_log_queue_t *q) {
   return q ? atomic_load_explicit(&q->full_dropped, memory_order_relaxed) : 0;
}
