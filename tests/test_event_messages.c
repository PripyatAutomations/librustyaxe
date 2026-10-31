#include <assert.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
static int received;
static void cb(const rr_event_message_t *m, void *user) {
   (void)user;
   assert(!strcmp(m->name, "test.cross-domain"));
   assert(!strcmp(m->sender, "node.123/connection.456/7"));
   assert(m->length == 4 && !memcmp(m->payload, "a\0bc", 4));
   received++;
}
int main(void) {
   event_init();
   rr_event_token_t token = event_on_message_token("test.cross-domain", cb, NULL, NULL, NULL);
   assert(token);
   char sender[] = "node.123/connection.456/7";
   char bytes[] = {'a', 0, 'b', 'c'};
   rr_event_message_t msg = {"test.cross-domain", sender, bytes, sizeof(bytes), 0};
   event_emit_message(&msg);
   assert(received == 1);
   event_off_token(token);
   event_emit_message(&msg);
   assert(received == 1);
   event_shutdown();
   return 0;
}
