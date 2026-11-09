#include "../log-queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Stub the delivery sink to verify ordering without a logger runtime. */
static unsigned received;
void Log(logpriority_t level, const char *subsys, const char *fmt, ...) {
   (void)level;
   (void)subsys;
   (void)fmt;
   received++;
}
int main(void) {
   ra_log_queue_t *q = ra_log_queue_new(2);
   assert(q);
   assert(ra_log_queue_push(q, LOG_INFO, "test", "one"));
   assert(ra_log_queue_push(q, LOG_WARN, "test", "two"));
   assert(!ra_log_queue_push(q, LOG_WARN, "test", "three"));
   assert(ra_log_queue_dropped(q) == 1);
   char source[64];
   ra_log_queue_full_source(q, source, sizeof(source));
   assert(!strcmp(source, "test"));
   ra_log_queue_full_source(q, source, sizeof(source));
   assert(!*source);
   assert(ra_log_queue_drain(q, 1) == 1);
   assert(received == 1);
   assert(ra_log_queue_drain(q, 10) == 1);
   assert(received == 2);
   assert(ra_log_queue_drain(q, 10) == 0);
   ra_log_queue_free(q);

   return 0;
}
