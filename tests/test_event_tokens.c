//
// librustyaxe/tests/test_event_tokens.c: lifecycle guarantees of the
// opaque event registration tokens.
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Covered here:
//   1. registration and delivery through tokens
//   2. unregister prevents later invocation
//   3. repeated unregister is a safe no-op
//   4. a callback may unregister itself while being dispatched
//   5. unregistration does not drop same-callback listeners registered
//      by another owner (token identity, not (cb,user) tuple alone)
//   6. a listener removed by a wildcard event_off() invalidates only its
//      own token; another token for the same event still works
//   7. binary tokens deliver and unregister
//   8. unregistered tokens do not fire after re-registration of a
//      same-shaped listener (no stale token rebinding)
//
#include <stdio.h>
#include <string.h>

#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>

static int hits;

static void count_cb(const char *event, const char *data, rrconn_t *cptr, void *user)
{
   (void)event;
   (void)data;
   (void)cptr;
   (void)user;
   hits++;
}

static void count_binary_cb(const char *event, const void *data, size_t len,
                            rrconn_t *cptr, void *user)
{
   (void)event;
   (void)data;
   (void)len;
   (void)cptr;
   (void)user;
   hits++;
}

static rr_event_token_t self_token;

static void self_unregister_cb(const char *event, const char *data,
                               rrconn_t *cptr, void *user)
{
   (void)event;
   (void)data;
   (void)cptr;
   (void)user;
   hits++;
   event_off_token(self_token);
}

int main(void)
{
   int failures = 0;
#define CHECK(cond, msg)                     \
   do                                        \
   {                                         \
      if (!(cond))                           \
      {                                      \
         fprintf(stderr, "FAIL: %s\n", msg); \
         failures++;                         \
      }                                      \
   } while (0)

   event_init();

   // 1. token registration receives events
   hits = 0;
   rr_event_token_t t1 = event_on_token("tok.test", count_cb, NULL);
   CHECK(t1 != NULL, "token returned");
   event_emit("tok.test", NULL, "x");
   CHECK(hits == 1, "token listener fired");

   // 2. unregister prevents later invocation
   event_off_token(t1);
   event_emit("tok.test", NULL, "x");
   CHECK(hits == 1, "unregistered token does not fire");

   // 3. repeated unregister is a no-op
   event_off_token(t1);
   event_off_token(t1);
   event_off_token(NULL);
   event_emit("tok.test", NULL, "x");
   CHECK(hits == 1, "double unregister safe");

   // 4. self-unregister during dispatch
   hits = 0;
   self_token = event_on_token("tok.self", self_unregister_cb, NULL);
   event_emit("tok.self", NULL, NULL);
   event_emit("tok.self", NULL, NULL);
   CHECK(hits == 1, "self-unregister fires once");

   // 5. token identity: two listeners with identical (cb,user)
   hits = 0;
   rr_event_token_t a = event_on_token("tok.ident", count_cb, (void *)0x1234);
   rr_event_token_t b = event_on_token("tok.ident", count_cb, (void *)0x1234);
   event_off_token(a);
   event_emit("tok.ident", NULL, NULL);
   CHECK(hits == 1, "unregistering one of two identical listeners keeps the other");
   event_off_token(b);
   event_emit("tok.ident", NULL, NULL);
   CHECK(hits == 1, "second identical listener removed by its own token");

   // 6. wildcard event_off() invalidates the token's listener; other
   // tokens for the same event still work and re-registration is not
   // hijacked by a stale token
   hits = 0;
   rr_event_token_t c = event_on_token("tok.wild", count_cb, (void *)0xAAAA);
   rr_event_token_t d = event_on_token("tok.wild", count_cb, (void *)0x5678);
   event_off("tok.wild", count_cb, (void *)0xAAAA);   // removes only c
   event_emit("tok.wild", NULL, NULL);
   CHECK(hits == 1, "wildcard removed one, token d still active");
   event_off_token(c);                      // already gone: no-op, must not
                                            // remove d's listener
   event_emit("tok.wild", NULL, NULL);
   CHECK(hits == 2, "stale token off does not kill a different listener");
   event_off_token(d);
   event_emit("tok.wild", NULL, NULL);
   CHECK(hits == 2, "d removed by its own token");

   // 7. binary tokens
   hits = 0;
   rr_event_token_t e = event_on_binary_token("tok.bin", count_binary_cb, NULL);
   event_emit_binary("tok.bin", NULL, "payload", 7);
   CHECK(hits == 1, "binary token fired");
   event_off_token(e);
   event_emit_binary("tok.bin", NULL, "payload", 7);
   CHECK(hits == 1, "binary token unregistered");

   // 8. stale token must not rebind to a newly registered listener with
   // the same (event, cb, user)
   hits = 0;
   rr_event_token_t f = event_on_token("tok.stale", count_cb, NULL);
   event_off_token(f);
   rr_event_token_t g = event_on_token("tok.stale", count_cb, NULL);
   event_off_token(f); // stale: must not remove g's listener
   event_emit("tok.stale", NULL, NULL);
   CHECK(hits == 1, "stale token cannot remove a re-registered listener");
   event_off_token(g);
   event_emit("tok.stale", NULL, NULL);
   CHECK(hits == 1, "g removed by its own token");

   event_shutdown();

   if (failures)
   {
      fprintf(stderr, "%d failure(s)\n", failures);
      return 1;
   }
   printf("PASS: event token lifecycle\n");
   return 0;
}
