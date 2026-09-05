//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Unit tests for librustyaxe/dict.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/dict.h>

static int failures = 0;
#define CHECK(cond) do { \
   if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void test_str_basic(void) {
   dict *d = dict_new();
   CHECK(d != NULL);

   CHECK(dict_add(d, "foo", "bar") == 0);
   CHECK(dict_get(d, "foo", NULL) != NULL);
   CHECK(strcmp(dict_get(d, "foo", NULL), "bar") == 0);
   // Default value when missing
   CHECK(strcmp(dict_get(d, "nope", "def"), "def") == 0);
   // Overwrite
   CHECK(dict_add(d, "foo", "baz") == 0);
   CHECK(strcmp(dict_get(d, "foo", NULL), "baz") == 0);

   dict_free(d);
}

static void test_typed(void) {
   dict *d = dict_new();
   CHECK(dict_add_int(d, "i", -42) == 0);
   CHECK(dict_add_uint(d, "u", 7) == 0);
   CHECK(dict_add_bool(d, "b", true) == 0);
   CHECK(dict_add_double(d, "dbl", 3.5) == 0);
   CHECK(dict_add_long(d, "l", 123456789L) == 0);
   CHECK(dict_add_char(d, "c", 'x') == 0);
   CHECK(dict_add_null(d, "n") == 0);

   CHECK(dict_get_int(d, "i", 0) == -42);
   CHECK(dict_get_uint(d, "u", 0) == 7);
   CHECK(dict_get_bool(d, "b", false) == true);
   CHECK(dict_get_double(d, "dbl", 0.0) == 3.5);
   CHECK(dict_get_long(d, "l", 0) == 123456789L);
   CHECK(dict_get_char(d, "c", 0) == 'x');
   CHECK(dict_get_type(d, "n") == VAL_NULL);
   CHECK(dict_get_type(d, "missing") == VAL_END);

   dict_free(d);
}

static void test_many_and_remove(void) {
   dict *d = dict_new();
   char key[32], val[32];
   for (int i = 0; i < 200; i++) {
      snprintf(key, sizeof(key), "key%d", i);
      snprintf(val, sizeof(val), "val%d", i);
      CHECK(dict_add(d, key, val) == 0);
   }
   for (int i = 0; i < 200; i++) {
      snprintf(key, sizeof(key), "key%d", i);
      snprintf(val, sizeof(val), "val%d", i);
      const char *v = dict_get(d, key, NULL);
      CHECK(v != NULL && strcmp(v, val) == 0);
   }
   // Remove every other one, verify
   for (int i = 0; i < 200; i += 2) {
      snprintf(key, sizeof(key), "key%d", i);
      CHECK(dict_del(d, key) == 0);
   }
   for (int i = 0; i < 200; i++) {
      snprintf(key, sizeof(key), "key%d", i);
      const char *v = dict_get(d, key, NULL);
      if (i % 2 == 0) {
         CHECK(v == NULL);
      } else {
         CHECK(v != NULL);
      }
   }
   CHECK(dict_del(d, "missing") != 0);   // deleting missing key fails
   dict_free(d);
}

static void test_merge_and_diff(void) {
   dict *a = dict_new(), *b = dict_new();
   dict_add(a, "common", "a");
   dict_add(a, "only-a", "1");
   dict_add(b, "common", "b");
   dict_add(b, "only-b", "2");

   dict *m = dict_merge_new(a, b);
   CHECK(m != NULL);
   CHECK(strcmp(dict_get(m, "common", NULL), "b") == 0);
   CHECK(strcmp(dict_get(m, "only-a", NULL), "1") == 0);
   CHECK(strcmp(dict_get(m, "only-b", NULL), "2") == 0);

   dict *df = dict_diff(a, b);
   CHECK(df != NULL);
   CHECK(dict_get(df, "common", NULL) != NULL);   // differs
   CHECK(dict_get(df, "only-a", NULL) != NULL);
   CHECK(dict_get(df, "only-b", NULL) != NULL);

   dict_free(m);
   dict_free(df);
   dict_free(a);
   dict_free(b);
}

int main(void) {
   test_str_basic();
   test_typed();
   test_many_and_remove();
   test_merge_and_diff();
   if (failures) {
      fprintf(stderr, "%d failure(s)\n", failures);
      return 1;
   }
   printf("test_dict: all tests passed\n");
   return 0;
}
