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
   int result=rrscript_parse_registrations(code,collect,&s,&e);
   if(result != 0)
      fprintf(stderr,"RustyScript parse failed at %u:%u: %s\n",
              e.line,e.column,e.message);
   assert(result==0);
   assert(s.count==3);
   /* Terminators may be separated from the registration by spaces,
    * newlines or comments; the semicolon itself is optional. */
   assert(rrscript_parse_registrations(
      "event sample.one {}  ;\n"
      "hotkey ^X {} /* comment */ ;\n"
      "script(\"/example\", {})\n ;\n",NULL,NULL,&e)==0);
   /* All three comment styles work outside registrations and inside
    * handler blocks; punctuation in quoted strings is never a comment. */
   assert(rrscript_parse_registrations(
      "# initial configuration comment\n"
      "script(\"/hash\", { # } ignored brace\n"
      "  print(\"# // /* not comments\") // } ignored brace\n"
      "  /* { ignored brace } */ print(1)\n"
      "}) # trailing registration comment\n"
      "// between registrations\n"
      "event sample.two { /* } */ print(\"// and #\") }\n",
      NULL,NULL,&e)==0);
   assert(rrscript_parse_registrations(
      "hotkey ^X {} # inline comment\n"
      "event sample.three {} /* comment */ ; # after terminator\n",
      NULL,NULL,&e)==0);
   bad("# opening comment\n event sample.bad { /* never ends");
   bad("event irc.privmsg print(1)");
   bad("hotkey ^^X { print(1) }");
   bad("script(\"test\", { print(1) })");
   bad("event irc.privmsg { print(1)");
   bad("event irc.privmsg { /* unfinished");
   assert(rrscript_parse_registrations(
      "int increment(int value) { value++; return value; }\n"
      "void notify(const char message) { log.info(message); }\n"
      "event test.event { increment(10) }\n",NULL,NULL,&e)==0);
   bad("int missing(int value) return value;");
   bad("event foo.bar { print(1) } garbage");
   assert(rrscript_parse_registrations("event (irc.privmsg || chat.msg) {}",NULL,NULL,&e)==0);
   bad("event (foo ||) {}");
   bad("event (foo && bar) {}");
   bad("event () {}");
   puts("RustyScript registration syntax tests passed");
   return 0;
}
