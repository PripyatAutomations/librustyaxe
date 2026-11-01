#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>

static int received;
static pthread_t receiver_thread;
static event_dispatch_fn_t queued_fn;
static void *queued_arg;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER;

static void cb(const rr_event_message_t *m, void *user) {
   (void)user;
   assert(pthread_equal(pthread_self(), receiver_thread));
   assert(!strcmp(m->name, "test.cross-domain"));
   assert(!strcmp(m->sender, "node.123/connection.456/7"));
   assert(m->length == 4 && !memcmp(m->payload, "a\0bc", 4));
   received++;
}
static void enqueue(event_dispatch_fn_t fn, void *arg, void *user) {
   (void)user;
   pthread_mutex_lock(&lock);
   assert(!queued_fn);
   queued_fn = fn;
   queued_arg = arg;
   pthread_cond_signal(&ready);
   pthread_mutex_unlock(&lock);
}
static void *consume(void *unused) {
   (void)unused;
   receiver_thread = pthread_self();
   pthread_mutex_lock(&lock);
   while (!queued_fn) pthread_cond_wait(&ready, &lock);
   event_dispatch_fn_t fn = queued_fn;
   void *arg = queued_arg;
   queued_fn = NULL;
   queued_arg = NULL;
   pthread_mutex_unlock(&lock);
   fn(arg);
   return NULL;
}
int main(void) {
   event_init();
   assert(event_domain_register("gtk", enqueue, NULL));
   assert(!event_domain_register("gtk", enqueue, NULL));
   rr_event_token_t token = event_on_message_domain_token("test.cross-domain", cb, NULL, "gtk");
   assert(!event_on_message_domain_token("test.cross-domain", cb, NULL, "unknown"));
   assert(token);
   char sender[] = "node.123/connection.456/7";
   char bytes[] = {'a', 0, 'b', 'c'};
   rr_event_message_t msg = {"test.cross-domain", sender, bytes, sizeof(bytes), 0};
   pthread_t worker;
   assert(!pthread_create(&worker, NULL, consume, NULL));
   event_emit_message(&msg);
   memset(sender, 'X', strlen(sender));
   memset(bytes, 'X', sizeof(bytes));
   pthread_join(worker, NULL);
   assert(received == 1);
   assert(!event_domain_unregister("gtk"));
   event_off_token(token);
   event_message_token_drain(token);
   /* Emit while registered, then unsubscribe before draining the queue. */
   token = event_on_message_domain_token("test.cross-domain", cb, NULL, "gtk");
   assert(token);
   event_emit_message(&msg);
   event_off_token(token);
   event_message_token_drain(token);
   /* Drain on the registered receiver thread, not on the emitter thread.
    * An unexpectedly delivered callback will still fail the count check. */
   assert(!pthread_create(&worker, NULL, consume, NULL));
   pthread_join(worker, NULL);
   assert(received == 1);
   assert(event_domain_unregister("gtk"));
   event_emit_message(&msg);
   assert(received == 1);
   event_shutdown();
   return 0;
}
