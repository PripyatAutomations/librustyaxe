/* SPDX-License-Identifier: MIT */
#include "../rrscript.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { int count; } state;
static int collect(const rrscript_binding_t *b, void *data) {
   state *s = data;
   s->count++;
   if (s->count==1) {
      assert(b->kind==RRSCRIPT_COMMAND && b->name_length==5 &&
             !strncmp(b->name,"/test",5));
   }
   if (s->count==2) {
      assert(b->kind==RRSCRIPT_HOTKEY && b->name_length==4 &&
             !strncmp(b->name,"_&^X",4));
   }
   if (s->count==3) {
      assert(b->kind==RRSCRIPT_EVENT && b->name_length==11 &&
             !strncmp(b->name,"irc.privmsg",11));
   }
   assert(b->body_length>0);
   return 0;
}
static void bad(const char *code) {
   rrscript_error_t e;
   assert(rrscript_parse_registrations(code,NULL,NULL,&e)==-1);
   assert(e.line>0 && e.message[0]);
}
int main(void) {
   state s={0};
   rrscript_error_t e;
   const char *code =
      "script(\"/test\", { print(\"{braces}\"); }) ;\n"
      "hotkey _&^X { if (true) { print('}'); } }\n"
      "event irc.privmsg { /* } ignored */ print(event.text) }\n";
   assert(rrscript_parse_registrations(code,collect,&s,&e)==0);
   assert(s.count==3);
   bad("event irc.privmsg print(1)");
   bad("hotkey ^^X { print(1) }");
   bad("script(\"test\", { print(1) })");
   bad("event irc.privmsg { print(1)");
   bad("event irc.privmsg { /* unfinished");
   bad("event foo.bar { print(1) } garbage");
   puts("RustyScript registration syntax tests passed");
   return 0;
}
