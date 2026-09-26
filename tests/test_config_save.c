// Unit tests for section-aware configuration saving.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <librustyaxe/config.h>
#include <librustyaxe/dict.h>

extern dict *cfg;
extern dict *default_cfg;

static int failures = 0;
#define CHECK(cond) do { \
   if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(void) {
   char path[] = "/tmp/rustyrig-config-save-XXXXXX";
   int fd = mkstemp(path);
   CHECK(fd >= 0);
   if (fd >= 0) close(fd);
   unlink(path);

   default_cfg = dict_new();
   cfg = dict_new();
   CHECK(default_cfg != NULL && cfg != NULL);
   if (cfg && default_cfg) {
      CHECK(dict_add(cfg, "zeta", "last") == 0);
      CHECK(dict_add(cfg, "fwdsp:path", "/usr/bin/fwdsp") == 0);
      CHECK(dict_add(cfg, "pipeline:pc16.rx", "appsrc ! sink") == 0);
      CHECK(dict_add(cfg, "site:gridsquare", "EM98") == 0);
      CHECK(dict_add(cfg, "callsign-lookup:use-qrz", "true") == 0);
      CHECK(dict_add(cfg, "long-value",
         "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
         "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
         "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == 0);
      CHECK(cfg_save(cfg, path));
   }

   FILE *fp = fopen(path, "r");
   CHECK(fp != NULL);
   if (fp) {
      char text[4096] = "";
      size_t used = fread(text, 1, sizeof(text) - 1, fp);
      text[used] = '\0';
      fclose(fp);
      CHECK(strstr(text, "[general]\n") != NULL);
      CHECK(strstr(text, "zeta=last\n") != NULL);
      CHECK(strstr(text, "[fwdsp]\npath=/usr/bin/fwdsp\n") != NULL);
      CHECK(strstr(text, "[pipelines]\npc16.rx=appsrc ! sink\n") != NULL);
      CHECK(strstr(text, "[site]\ngridsquare=EM98\n") != NULL);
      CHECK(strstr(text, "[callsign-lookup]\nuse-qrz=true\n") != NULL);
      CHECK(strstr(text, "long-value=0123456789") != NULL);

      /* Every generated physical line must fit the documented limit. */
      char *line = text;
      while (*line) {
         char *newline = strchr(line, '\n');
         size_t length = newline ? (size_t)(newline - line) : strlen(line);
         CHECK(length <= 80);
         if (newline && length > 0 && line[length - 1] == '\\') {
            char *next = newline + 1;
            size_t indent = strlen("long-value=");
            CHECK(strncmp(next, "           ", indent) == 0);
         }
         if (!newline) break;
         line = newline + 1;
      }
   }

   /* Continuation indentation is syntax, not part of the loaded value. */
   FILE *manual = fopen(path, "w");
   CHECK(manual != NULL);
   if (manual) {
      fputs("[general]\nwrapped=first\\\n\t   second\\\n    third\n", manual);
      fclose(manual);
      dict *loaded = cfg_load(path);
      CHECK(loaded != NULL);
      if (loaded) {
         CHECK(strcmp(dict_get(loaded, "wrapped", ""), "firstsecondthird") == 0);
         dict_free(loaded);
      }
   }

   unlink(path);
   dict_free(cfg);
   dict_free(default_cfg);
   cfg = NULL;
   default_cfg = NULL;
   if (failures) return 1;
   puts("test_config_save: all tests passed");
   return 0;
}
