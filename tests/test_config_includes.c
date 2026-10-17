// Regression tests for include merging and comment-disabled directives.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <librustyaxe/config.h>
#include <librustyaxe/dict.h>

static int failures;
extern dict *cfg;

static bool typed_callback(const char *path, int line, const char *section, const char *input) {
   (void)path; (void)line; (void)section; (void)input;
   return dict_add_int(cfg, "typed:int", 42) == 0 &&
      dict_add_bool(cfg, "typed:bool", true) == 0 &&
      dict_add_null(cfg, "typed:null") == 0 &&
      dict_add_double(cfg, "typed:double", 3.5) == 0;
}

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

static void write_file(const char *path, const char *data) {
   FILE *fp = fopen(path, "w");
   CHECK(fp != NULL);
   if (!fp) return;
   CHECK(fputs(data, fp) >= 0);
   CHECK(fclose(fp) == 0);
}

static void expect(dict *d, const char *key, const char *value) {
   const char *actual = dict_get(d, key, NULL);
   if (!actual || strcmp(actual, value) != 0) {
      fprintf(stderr, "FAIL %s: expected <%s>, got <%s>\n", key, value, actual ? actual : "(null)");
      failures++;
   }
}

int main(void) {
   char dir[] = "/tmp/rustyaxe-cfg-includes-XXXXXX";
   CHECK(mkdtemp(dir) != NULL);
   char mainpath[512], subpath[512], nestedpath[512];
   snprintf(mainpath, sizeof(mainpath), "%s/main.cfg", dir);
   snprintf(subpath, sizeof(subpath), "%s/extra.cfg", dir);
   snprintf(nestedpath, sizeof(nestedpath), "%s/nested.cfg", dir);

   write_file(nestedpath, "[general]\nnested=yes\nfoo=nested\n");
   write_file(subpath,
      "[general]\nbar=two\nfoo=extra\n"
      ".include nested.cfg\n"
      "[general]\nbar=again\n");
   write_file(mainpath,
      "[general]\nfoo=first\n"
      "/* .include missing-block.cfg */\n"
      "/* beginning comment\n"
      ".include missing-multiline.cfg\n"
      "!include missing-optional.cfg\n"
      "[general]\n"
      "*/\n"
      "// .include missing-slash.cfg\n"
      "# .include missing-hash.cfg\n"
      "; .include missing-semicolon.cfg\n"
      "[general]\n"
      "quoted=\"/* not a comment */\"\n"
      "inline=before/* remove */after\n"
      ".include extra.cfg\n"
      "[general]\nlast=three\n"
      "[general-extra]\nshould-not-be-general=bad\n"
      "[general]\nend=ok\n");
   CHECK(cfg_add_callback(NULL, "typed-test", typed_callback));
   dict *loaded = cfg_load(mainpath);
   CHECK(loaded != NULL);
   if (loaded) {
      const char *key;
      dict_value_t value;
      val_type_t type;
      int rank = 0;
      unsigned found = 0;
      while ((rank = dict_enumerate_typed(loaded, rank, &key, &value, &type)) >= 0) {
         if (!strcmp(key, "typed:int")) { CHECK(type == VAL_INT && value.i == 42); found |= 1; }
         if (!strcmp(key, "typed:bool")) { CHECK(type == VAL_BOOL && value.i == 1); found |= 2; }
         if (!strcmp(key, "typed:null")) { CHECK(type == VAL_NULL); found |= 4; }
         if (!strcmp(key, "typed:double")) { CHECK(type == VAL_DOUBLE && value.d == 3.5); found |= 8; }
      }
      CHECK(found == 15);
      expect(loaded, "foo", "nested");
      expect(loaded, "bar", "again");
      expect(loaded, "nested", "yes");
      expect(loaded, "last", "three");
      expect(loaded, "end", "ok");
      expect(loaded, "inline", "beforeafter");
      expect(loaded, "quoted", "\"/* not a comment */\"");
      CHECK(dict_get(loaded, "should-not-be-general", NULL) == NULL);
      dict_free(loaded);
   }
   /* Typed reload: preserve values, distinguish NULL from missing,
    * and report only changed/removed keys to reload listeners. */
   cfg = dict_new();
   CHECK(cfg != NULL);
   if (cfg) {
      CHECK(dict_add_int(cfg, "n", 1) == 0);
      CHECK(dict_add_bool(cfg, "unchanged", true) == 0);
      CHECK(dict_add_null(cfg, "removed") == 0);
      dict *replacement = dict_new();
      CHECK(replacement != NULL);
      if (replacement) {
         CHECK(dict_add_double(replacement, "n", 2.5) == 0);
         CHECK(dict_add_bool(replacement, "unchanged", true) == 0);
         CHECK(dict_add_null(replacement, "new-null") == 0);
         CHECK(cfg_apply_new(cfg, replacement));
         CHECK(dict_get_type(cfg, "n") == VAL_DOUBLE);
         CHECK(dict_get_double(cfg, "n", 0) == 2.5);
         CHECK(dict_get_type(cfg, "unchanged") == VAL_BOOL);
         CHECK(dict_get_type(cfg, "new-null") == VAL_NULL);
         const char *key;
         dict_value_t value;
         val_type_t type;
         bool removed_seen = false, null_seen = false;
         int rank = 0;
         while ((rank = dict_enumerate_typed(cfg, rank, &key, &value, &type)) >= 0) {
            if (!strcmp(key, "removed")) removed_seen = true;
            if (!strcmp(key, "new-null")) null_seen = true;
         }
         CHECK(!removed_seen && null_seen);
      }
      dict_free(cfg);
      cfg = NULL;
   }
   unlink(mainpath);
   unlink(subpath);
   unlink(nestedpath);
   rmdir(dir);
   if (failures) return 1;
   puts("test_config_includes: all tests passed");
   return 0;
}
