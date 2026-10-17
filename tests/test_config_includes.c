// Regression tests for include merging and comment-disabled directives.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <librustyaxe/config.h>
#include <librustyaxe/dict.h>

static int failures;
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
   dict *loaded = cfg_load(mainpath);
   CHECK(loaded != NULL);
   if (loaded) {
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
   unlink(mainpath);
   unlink(subpath);
   unlink(nestedpath);
   rmdir(dir);
   if (failures) return 1;
   puts("test_config_includes: all tests passed");
   return 0;
}
