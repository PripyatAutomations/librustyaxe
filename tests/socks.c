#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/socks.h>
#include <librustyaxe/util.time.h>

static unsigned errors;
static void handler(struct mg_connection *c, int ev, void *data) {
   (void)c;

   if (ev == MG_EV_ERROR) {
      assert(strstr(data, "SOCKS5:"));
      errors++;
      c->is_closing = 0; /* Failure must stay closed even if a callback clears it. */
   }
}
static void clear(struct mg_connection *c) {
   mg_iobuf_del(&c->send, 0, c->send.len);
}
static int feed(rr_socks_t *s, struct mg_connection *c, const void *p, size_t n) {
   assert(mg_iobuf_add(&c->recv, c->recv.len, p, n) == n);

   return rr_socks_event(s, c, MG_EV_READ);
}
int main(void) {
   const char *host = "chat.invalid";
   uint16_t port = 6697;
   rr_socks_t s;
   assert(!rr_socks_init(&s, "", NULL, NULL, host, port));
   const char *invalid[] = {
      "http://localhost", "socks4://localhost", "socks5h://", "socks5h://localhost:0",
      "socks5h://localhost:65536", "socks5h://user:secret@localhost", "socks5h://localhost/path", "socks5h://[bad]"
   };

   for (unsigned i = 0 ; i < sizeof(invalid) / sizeof(invalid[0]) ; i++) {
      assert(!rr_socks_init(&s, invalid[i], NULL, NULL, host, port));
   }

   assert(!rr_socks_init(&s, "socks5h://localhost", "user", "", host, port));
   assert(!rr_socks_init(&s, "socks5h://localhost", "", "secret", host, port));
   assert(rr_socks_init(&s, "socks5h://localhost", NULL, NULL, host, port));
   assert(!strcmp(s.url, "tcp://localhost:1080"));
   struct mg_connection c = {
      .fn = handler
   };
   assert(!rr_socks_event(&s, &c, MG_EV_CONNECT));
   assert(c.send.len == 3 && !memcmp(c.send.buf, "\5\1\0", 3));
   clear(&c);
   assert(!feed(&s, &c, "\5", 1) && !c.send.len);
   assert(!feed(&s, &c, "\0", 1));
   assert(c.send.len == 19 && !memcmp(c.send.buf, "\5\1\0\3\14chat.invalid\32\51", 19));
   clear(&c);
   unsigned char reply[] = {
      5, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2
   };

   for (unsigned i = 0 ; i < sizeof(reply) - 1 ; i++) {
      assert(!feed(&s, &c, reply + i, 1));
   }

   unsigned char tail[] = {
      2, 'h', 'i'
   };
   assert(feed(&s, &c, tail, sizeof(tail)) == 1);
   assert(c.recv.len == 2 && !memcmp(c.recv.buf, "hi", 2));
   mg_iobuf_del(&c.recv, 0, c.recv.len);
   assert(rr_socks_init(&s, "socks5://[::1]:1081", "user", "secret", host, port));
   assert(!strcmp(s.url, "tcp://[::1]:1081"));
   rr_socks_event(&s, &c, MG_EV_CONNECT);
   assert(c.send.len == 3 && c.send.buf[2] == 2);
   clear(&c);
   assert(!feed(&s, &c, "\5\2", 2));
   assert(c.send.len == 13 && !memcmp(c.send.buf, "\1\4user\6secret", 13));
   clear(&c);
   assert(!feed(&s, &c, "\1\0", 2) && c.send.len == 19);
   clear(&c);
   assert(feed(&s, &c, "\5\0\0\3\3abc\0\0", 10) == 1);
   assert(!c.recv.len);

   for (unsigned kind = 0 ; kind < 6 ; kind++) {
      c.is_closing = 0;
      mg_iobuf_del(&c.recv, 0, c.recv.len);
      assert(rr_socks_init(&s, "socks5h://localhost", NULL, NULL, host, port));

      if (kind == 0) {
         assert(feed(&s, &c, "\5\377", 2) == -1);
      } else if (kind == 1) {
         s.deadline_ms = mono_ms();
         assert(rr_socks_event(&s, &c, MG_EV_POLL) == -1);
      } else {
         assert(!feed(&s, &c, "\5\0", 2));
         const unsigned char bad[][5] = { {
                                             5, 5, 0, 1
                                          }, {
                                             4, 0, 0, 1
                                          }, {
                                             5, 0, 0, 7
                                          }, {
                                             5, 0, 0, 3, 0
                                          } };
         assert(feed(&s, &c, bad[kind - 2], 5) == -1);
      }
      assert(c.is_closing);
      c.is_closing = 0;
      assert(rr_socks_event(&s, &c, MG_EV_CONNECT) == -1 && c.is_closing);
      clear(&c);
   }

   assert(errors == 6);
   c.is_closing = 0;
   mg_iobuf_del(&c.recv, 0, c.recv.len);
   assert(rr_socks_init(&s, "socks5h://localhost", "user", "wrong", host, port));
   assert(!feed(&s, &c, "\5\2", 2));
   clear(&c);
   assert(feed(&s, &c, "\1\1", 2) == -1 && errors == 7);
   assert(c.is_closing);
   c.is_closing = 0;
   assert(rr_socks_init(&s, "socks5h://localhost", NULL, NULL, host, port));
   assert(rr_socks_event(&s, &c, MG_EV_ERROR) == -1 && c.is_closing);
   c.is_closing = 0;
   assert(rr_socks_event(&s, &c, MG_EV_CONNECT) == -1 && c.is_closing);
   const char *addresses[] = {
      "192.0.2.4", "2001:db8::1"
   };

   for (unsigned i = 0 ; i < 2 ; i++) {
      c.is_closing = 0;
      mg_iobuf_del(&c.recv, 0, c.recv.len);
      mg_iobuf_del(&c.send, 0, c.send.len);
      host = addresses[i];
      port = 6667;
      assert(rr_socks_init(&s, "socks5h://localhost", NULL, NULL, host, port));
      assert(!feed(&s, &c, "\5\0", 2));
      assert(c.send.len == (i ? 22 : 10) && c.send.buf[3] == (i ? 4 : 1));
      assert(c.send.buf[c.send.len - 2] == 26 && c.send.buf[c.send.len - 1] == 11);

      if (!i) {
         assert(!memcmp(c.send.buf + 4, "\300\0\2\4", 4));
      }
   }

   char too_long[257];
   memset(too_long, 'x', sizeof(too_long) - 1);
   too_long[sizeof(too_long) - 1] = '\0';
   assert(!rr_socks_init(&s, "socks5h://localhost", too_long, "secret", host, port));
   mg_iobuf_free(&c.recv);
   mg_iobuf_free(&c.send);
   puts("PASS: SOCKS5 addressing, credentials, fragmented replies, leftovers, rejection and timeout");
}
