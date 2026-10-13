// Profiling must allow log callbacks to re-enter and mutate the event registry.
#include <assert.h>
#include <signal.h>
#include <stdarg.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

#ifndef USE_PROFILING
#define USE_PROFILING 1
#endif
static time_t test_now = 1000;
static time_t profile_time(time_t *result) {
   if (result) {
      *result = test_now;
   }

   return test_now;
}

#define time profile_time
#ifndef EVENT_BUS_SOURCE
#define EVENT_BUS_SOURCE "librustyaxe/event-bus.c"
#endif
#include EVENT_BUS_SOURCE
#undef time

static unsigned dumps, forwarded, received, text_received;
static bool forward_logs, remove_listener, removed_row_logged;

static void binary_listener(const char *event, const void *data, size_t len, rrconn_t *client, void *user) {
   received++;
}

static void text_listener(const char *event, const char *data, rrconn_t *client, void *user) {
   text_received++;
}

void Log(logpriority_t priority, const char *subsys, const char *fmt, ...) {
   if (strcmp(subsys, "profile")) {
      return;
   }
   char message[256];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(message, sizeof(message), fmt, ap);
   va_end(ap);

   if (!strcmp(message, "Event dispatch counts (cumulative):")) {
      dumps++;

      if (remove_listener) {
         // The pending profile rows must survive removal of dictionary keys.
         event_off("profile.remove", NULL, NULL);
         remove_listener = false;
      }
   } else if (!strcmp(message, "  profile.remove: 1")) {
      removed_row_logged = true;
   }

   if (forward_logs) {
      // rrserver's hostlog callback sends binary media while handling Log().
      forwarded++;
      event_emit_binary("protocol.media.send", NULL, message, strlen(message));
      event_profile_dump();     // Re-entry must retain the five-minute limit.
   }
}

int main(void) {
   alarm(5);                    // A regression must fail rather than hang the entire test suite.
   event_init();
   event_on("profile.text", text_listener, NULL);
   event_on("profile.remove", text_listener, NULL);
   event_on_binary("protocol.media.send", binary_listener, NULL);
   event_emit("profile.text", NULL, "first");
   event_emit("profile.remove", NULL, "retained in the snapshot");
   assert(dumps == 1 && text_received == 2);

   forward_logs = true;
   remove_listener = true;
   test_now += 300;
   event_emit_binary("protocol.media.send", NULL, "frame", 5);
   assert(dumps == 2 && removed_row_logged);
   assert(received == forwarded + 1);

   test_now += 299;
   event_profile_dump();
   assert(dumps == 2);
   test_now++;
   event_profile_dump();
   assert(dumps == 3 && received == forwarded + 1);

   test_now += 300;
   event_emit("profile.text", NULL, "next");
   assert(dumps == 4 && text_received == 3 && received == forwarded + 1);
   event_shutdown();
   alarm(0);
   puts("PASS: profiler logging re-enters event dispatch without deadlock, preserves snapshots and rate limiting");

   return 0;
}
