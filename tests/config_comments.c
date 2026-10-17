#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "../config.comments.h"

static void check(const char *input, bool *inside,
   const char *expected, bool expected_inside) {
   char line[512];
   strcpy(line, input);
   cfg_strip_block_comments(line, inside);
   assert(!strcmp(line, expected));
   assert(*inside == expected_inside);
}

int main(void) {
   bool block = false;
   check("a=one /* explanation */ two", &block, "a=one  two", false);
   check("[general] /* trailing section note */", &block, "[general] ", false);
   check("/* first */ a=1 /* second */", &block,  " a=1 " ", false);
   check("a=one /* spanning", &block,  "a=one " ", true);
   check("multiple /* nested syntax is just text", &block, "", true);
   check("continued */ b=2", &block, " b=2", false);
   check("url=\"http://host/*literal*/\"", &block,
      "url=\"http://host/*literal*/\"", false);
   check("key='/*not a comment*/'", &block,
      "key='/*not a comment*/'", false);
   check("key=\"escaped \\\" /* literal */\"", &block,
      "key=\"escaped \\\" /* literal */\"", false);
   check("/* whole line */", &block, "", false);
   check("  ", &block, "  ", false);
   check("x=1/*x*/+2", &block, "x=1 +2", false);
   return 0;
}
