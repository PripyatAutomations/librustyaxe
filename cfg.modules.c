//
// librustyaxe/cfg.modules.c: [modules] config section
//    This is part of rustyray-fw / rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Any program can list loadable modules and their options in a [modules]
// section:
//
//    [modules]
//    rrclient-gtk.so=
//    mymodule.so=foo=bar baz
//
// The name may include the .so suffix or not; an empty value (or no value)
// is valid and means "no options". Options are stored as
// "module:<name>.options" in the config dictionary for the loader or module
// init to consume via cfg_modules_get().
//
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <librustyaxe/core.h>

static bool cfg_modules_section_cb(const char *path, int line, const char *section, const char *buf) {
   if (!path || !section || strcmp(section, "modules") != 0 || !buf) {
      return true;
   }

   char *copy = strdup(buf);

   if (!copy) {
      return true;
   }
   char *value = strchr(copy, '=');

   if (!value) {
      Log(LOG_CRIT, "cfg.modules", "Missing '=' in [%s] at %s:%d", section, path, line);
      free(copy);

      return true;
   }
   *value++ = '\0';

   // Trim key
   char *key_end = copy + strlen(copy);
   while (key_end > copy && isspace( (unsigned char)key_end[-1]) ) {
      *--key_end = '\0';
   }
   char *key = copy;
   while (*key && isspace( (unsigned char)*key) ) {
      key++;
   }
   // Trim value (may legitimately be empty: "name=")
   while (*value && isspace( (unsigned char)*value) ) {
      value++;
   }
   char *value_end = value + strlen(value);
   while (value_end > value && isspace( (unsigned char)value_end[-1]) ) {
      *--value_end = '\0';
   }

   if (!*key) {
      Log(LOG_CRIT, "cfg.modules", "Empty module name in [%s] at %s:%d", section, path, line);
      free(copy);

      return true;
   }

   // Normalize: store under the name without a trailing .so
   char name[128];
   snprintf(name, sizeof(name), "%s", key);
   size_t nlen = strlen(name);

   if (nlen > 3 && strcmp(name + nlen - 3, ".so") == 0) {
      name[nlen - 3] = '\0';
   }

   char fullkey[160];

   if (snprintf(fullkey, sizeof(fullkey), "module:%s.options", name) <= 0 ||
      dict_add(cfg, fullkey, value) != 0) {
      Log(LOG_CRIT, "cfg.modules", "Unable to store options for %s at %s:%d", name, path, line);
      free(copy);

      return true;
   }
   free(copy);

   return false;
}

bool cfg_modules_init(void) {
   return cfg_add_callback(NULL, "modules", cfg_modules_section_cb);
}

// Enumerate configured modules. Returns the name of the index-th configured
// module (order as written), or NULL when exhausted. options_out receives
// the options string ("" when none was given, never NULL).
const char *cfg_modules_get(int index, const char **options_out) {
   const char *key = NULL;
   char *val = NULL;
   int rank = 0;
   int seen = 0;

   while ( (rank = dict_enumerate(cfg, rank, &key, &val) ) >= 0) {
      if (!key || strncmp(key, "module:", 7) != 0) {
         continue;
      }

      if (seen++ < index) {
         continue;
      }
      const char *name = key + 7;
      static char namebuf[128];
      // strip ".options"
      const char *dot = strstr(name, ".options");
      size_t len = dot ? (size_t)(dot - name) : strlen(name);

      if (len >= sizeof(namebuf) ) {
         len = sizeof(namebuf) - 1;
      }
      memcpy(namebuf, name, len);
      namebuf[len] = '\0';

      if (options_out) {
         *options_out = val ? val : "";
      }

      return namebuf;
   }
   return NULL;
}

const char *cfg_modules_options(const char *name) {
   if (!name || !*name) {
      return NULL;
   }
   char fullkey[160];

   if (snprintf(fullkey, sizeof(fullkey), "module:%s.options", name) <= 0) {
      return NULL;
   }
   const char *val = dict_get(cfg, fullkey, NULL);

   return val ? val : "";
}
