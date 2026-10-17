// librustyaxe/cfg.serial.c: [serial] and [serial:*] configuration parsing
// Licensed under MIT license, if built without mongoose or GPL if built with.
#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librustyaxe/io.serial.h>

/* The loader dispatches named sections to their component's callback. The dotted form also accepts cfg_save()'s [serial] NAME.option representation. */
static bool serial_config(const char *path, int line, const char *section, const char *buf) {
   char *copy = strdup(buf);

   if (!copy) {
      return true;
   }
   char *value = strchr(copy, '=');
   bool failed = true;

   if (value) {
      *value++ = '\0';
      char *key = copy;
      while (isspace((unsigned char)*key)) {
         key++;
      }
      char *end = key + strlen(key);
      while (end > key && isspace((unsigned char)end[-1])) {
         *--end = '\0';
      }
      while (isspace((unsigned char)*value)) {
         value++;
      }
      end = value + strlen(value);
      while (end > value && isspace((unsigned char)end[-1])) {
         *--end = '\0';
      }
      char full[256];
      int len = snprintf(full, sizeof(full), "%s.%s", section, key);

      if (!strcmp(section, "serial") && strchr(key, '.')) {
         full[6] = ':';
      }

      if (*key && len > 0 && (size_t)len < sizeof(full)) {
         failed = dict_add(cfg, full, value) != 0;
      }
   }

   if (failed) {
      Log(LOG_WARN, "cfg.serial", "Invalid [%s] setting at %s:%d", section, path, line);
   }
   free(copy);

   return !failed;
}

bool rr_serial_config_register(void) {
   return cfg_add_callback(NULL, "serial", serial_config) && cfg_add_callback(NULL, "serial:*", serial_config);
}
