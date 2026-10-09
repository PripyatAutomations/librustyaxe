// Exercise untrusted JSON at the same boundary used by HTTP/WebSocket events.
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>

int main(void) {
   const char *bad[] = {
      "{\"key\"", "{\"key\":", "{\"key\":\"ends\\", "\"\\",
      "{\"a\":1 \"b\":2}", "{\"a\":1,}", "[1 2]", "[1,]",
      "{\"a\":-}", "{\"a\":-9223372036854775809}",
      "{\"a\":18446744073709551616}", "{\"a\":01}", "{\"a\":+1}",
      "{\"a\":1.}", "{\"a\":.1}", "{\"a\":0x1}", "{\"a\":1e}",
      "{\"a\":NaN}", "{\"a\":1e999}", "{\"a\":bogus}",
      "{\"a\":\"\\q\"}", "{\"a\":\"\\u0000\"}", "{\"a\":\"\\u123\"}",
      "{\"a\":\"\\ud800\"}", "{\"a\":\"\\udc00\"}",
      "{\"a\":\"\\ud800\\u0041\"}", "{\"a\":\"line\nfeed\"}",
      "{\"a\":1,\"a\":2}", "{\"msg\":{\"type\":\"cat\"},\"msg.type\":\"auth\"}",
      "{\"msg\":{},\"msg\":{}}", "{}{}", "{} trailing", "\v{}"
   };

   for (size_t i = 0 ; i < sizeof(bad) / sizeof(bad[0]) ; i++) {
      // Exact-size allocations make the trailing-backslash overread visible to ASan.
      char *input = strdup(bad[i]);
      dict *d = json2dict(input);

      if (d) {
         fprintf(stderr, "Accepted malformed JSON case %zu\n", i);
      }
      assert(!d);
      free(input);
   }

   const char sample[] = "{\"a\":\"escaped \\u0041\",\"b\":[1,true,{\"c\":null}]}";

   for (size_t n = 0 ; n < strlen(sample) ; n++) {
      char *input = strndup(sample, n);
      assert(!json2dict(input));
      free(input);
   }

   srand(1);

   for (unsigned attempt = 0 ; attempt < 5000 ; attempt++) {
      size_t n = rand() % 96;
      char *input = malloc(n + 1);

      for (size_t i = 0 ; i < n ; i++) {
         input[i] = 1 + rand() % 127;
      }

      input[n] = 0;
      dict *parsed = json2dict(input);
      dict_free(parsed);
      free(input);
   }

   char deep[2048];
   memset(deep, '[', 512);
   deep[512] = '0';
   memset(deep + 513, ']', 512);
   deep[1025] = 0;
   assert(!json2dict(deep));
   dict *d = json2dict(" {\"a\":\"quote: \\\" \\n \\t \\u00e9 "
      "\\ud83d\\ude00\",\"n\":-9223372036854775808,\"u\":18446744073709551615,\"arr\":[true,null,1.5e2]} ");
   assert(d);
   assert(!strcmp(dict_get(d, "a", ""), "quote: \" \n \t \xc3\xa9 \xf0\x9f\x98\x80"));
   assert(dict_get_llong(d, "n", 0) == LLONG_MIN);
   assert(dict_get_ullong(d, "u", 0) == ULLONG_MAX);
   assert(dict_get_bool(d, "arr[0]", false));
   assert(dict_get_type(d, "arr[1]") == VAL_NULL);
   assert(dict_get_double(d, "arr[2]", 0) == 150);
   char *wire = dict2json(d);
   assert(wire);
   dict *roundtrip = json2dict(wire);
   assert(roundtrip);
   assert(!strcmp(dict_get(roundtrip, "a", ""), dict_get(d, "a", "")));
   free(wire);
   dict_free(roundtrip);
   dict_free(d);
   d = dict_new();
   dict_add_double(d, "signed", -(double)LLONG_MIN);
   dict_add_double(d, "unsigned", (double)(ULLONG_MAX / 2 + 1) * 2.0);
   assert(dict_get_llong(d, "signed", 7) == 7);
   assert(dict_get_ullong(d, "unsigned", 7) == 7);
   dict_add_double(d, "signed", -(double)LONG_MIN);
   dict_add_double(d, "unsigned", (double)(ULONG_MAX / 2 + 1) * 2.0);
   assert(dict_get_long(d, "signed", 7) == 7);
   assert(dict_get_ulong(d, "unsigned", 7) == 7);
   dict_free(d);
   puts("PASS: bounded JSON parsing, malformed tokens, duplicate fields, Unicode and numeric bounds");

   return 0;
}
