//
// librustyaxe/config.c: Flexible configuration handling inspired by ini files
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Features:
//	defconfig:	Default configuration values stored in one place
//	load/save:	Can save configuration from memory
//	extendible:	Register load/save callbacks
//	variable expansion: Support for %{key} expansion in _exp() versions
//	reload events:	Dispatch events to your callback if reloaded

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <limits.h>
#include <libgen.h>
#include <librustyaxe/core.h>
#include <librustyaxe/util.file.h>
#include <librrprotocol/rrprotocol.h>

#if defined(__GNUC__) || defined(__clang__)
extern defconfig_t defcfg[] __attribute__((weak));
#else
extern defconfig_t defcfg[];
#endif

const char *config_file = NULL;
dict *cfg = NULL;
dict *default_cfg = NULL;                // Hard-coded defaults (defcfg.c)
cfg_cb_list_t *cfg_callbacks = NULL;

bool cfg_set_default(dict *d, const char *key, const char *val) {
   if (!key || !d) {
      Log(LOG_CRIT, "cfg", "cfg_set_default: dict:<%p> key:<%p> is not valid", d, key);
      return false;
   }

   Log(LOG_CRAZY, "cfg", "Setting default for dict:<%p>/%s to '%s'", d, key, val);
   if (dict_add(d, key, (char *)val) != 0) {
      Log(LOG_CRIT, "cfg", "defcfg dict:<%p> failed to set key |%s| to val |%s| at <%p>", d, key, val, val);
      return false;
   }

   return true;
}

bool cfg_set_defaults(dict *d, defconfig_t *defaults) {
   if (!d) {
      Log(LOG_CRIT, "cfg", "cfg_set_defaults: NULL dict");
      return false;
   }

   if (!defaults) {
      Log(LOG_CRIT, "cfg", "cfg_set_defaults: NULL input");
      return false;
   }
   Log(LOG_DEBUG, "cfg", "cfg_set_defaults: Loading defaults from <%p>", defaults);

   int i = 0;
   int warnings = 0;
   while (defaults[i].key) {
      if (!defaults[i].val) {
         Log(LOG_CRAZY, "cfg", "cfg_set_defaults: Skipping key |%s| as its empty", defaults[i].key);
         i++;
         continue;
      }

      Log(LOG_CRAZY, "cfg", "cfg_set_defaults: |%s| => |%s|", defaults[i].key, defaults[i].val);
      if ( !cfg_set_default(d, defaults[i].key, defaults[i].val) ) {
         Log(LOG_WARN, "cfg", "cfg_set_defaults: Failed to set key: |%s|", defaults[i].key);
         warnings++;
      }
      i++;
   }
   Log(LOG_INFO, "cfg", "Imported %d default settings with %d warnings", i, warnings);

   return true;
}

const defconfig_t *cfg_defconfig_find(const char *key) {
   if (!key || !defcfg) return NULL;
   for (size_t i = 0; defcfg[i].key; i++) {
      if (strcasecmp(defcfg[i].key, key) == 0) return &defcfg[i];
   }
   return NULL;
}

bool cfg_set_value(const char *key, const char *value) {
   const defconfig_t *def = cfg_defconfig_find(key);
   if (!cfg || !def || !value) return false;

   char canonical[128];
   const char *stored = value;
   char *end = NULL;
   errno = 0;
   switch (def->type) {
   case DEFCONFIG_BOOL: {
      bool b;
      if (!strcasecmp(value, "true") || !strcasecmp(value, "yes") ||
          !strcasecmp(value, "on") || !strcmp(value, "1")) b = true;
      else if (!strcasecmp(value, "false") || !strcasecmp(value, "no") ||
               !strcasecmp(value, "off") || !strcmp(value, "0")) b = false;
      else return false;
      snprintf(canonical, sizeof(canonical), "%s", b ? "true" : "false");
      stored = canonical;
      break;
   }
   case DEFCONFIG_INT: {
      long n = strtol(value, &end, 10);
      if (errno || end == value || *end) return false;
      snprintf(canonical, sizeof(canonical), "%ld", n);
      stored = canonical;
      break;
   }
   case DEFCONFIG_UINT: {
      if (*value == '-') return false;
      unsigned long n = strtoul(value, &end, 10);
      if (errno || end == value || *end) return false;
      snprintf(canonical, sizeof(canonical), "%lu", n);
      stored = canonical;
      break;
   }
   case DEFCONFIG_FLOAT: {
      double n = strtod(value, &end);
      if (errno || end == value || *end) return false;
      snprintf(canonical, sizeof(canonical), "%.9g", n);
      stored = canonical;
      break;
   }
   case DEFCONFIG_ENUM:
      if (def->choices && *def->choices) {
         char *choices = strdup(def->choices);
         bool found = false;
         char *save = NULL;
         for (char *p = strtok_r(choices, "|", &save); p;
              p = strtok_r(NULL, "|", &save)) {
            if (!strcasecmp(p, value)) { found = true; break; }
         }
         free(choices);
         if (!found) return false;
      }
      break;
   default:
      break;
   }
   if (dict_add(cfg, key, stored) != 0) return false;
   reload_event_run(key);
   /* Programmatic settings changes (for example /set in a client) should
      refresh the same cached runtime values as a file reload. */
   reload_event_run(NULL);
   return true;
}

bool cfg_detect_and_load(const char *configs[], int num_configs) {
   const char *homedir = getenv("HOME");

   // Find and load the configuration file
   char *fullpath = find_file_by_list(configs, num_configs);

   if (!default_cfg) {
      default_cfg = dict_new();
   }

   if (fullpath) {
      // save the path for later use
      if ( ( config_file = strdup(fullpath) ) == NULL ) {
         abort();
      }

      if ( !( cfg = cfg_load(fullpath) ) ) {
         Log(LOG_CRIT, "cfg", "Couldn't load config \"%s\", using defaults instead", fullpath);
         cfg = default_cfg;
      } else {
         Log(LOG_DEBUG, "cfg", "Loaded config from '%s'", fullpath);
      }
      free(fullpath);
   } else {
      // Use default settings and save it to default
      cfg = default_cfg;
      Log(LOG_CRIT, "cfg", "No config file found, saving defaults");
   }
   return true;
}

bool cfg_add_callback( const char *path, const char *section, bool (*cb) () ) {
   if (!section || !cb) {
      return false;
   }

   cfg_cb_list_t *new_cb = malloc( sizeof(cfg_cb_list_t) );
   if (new_cb == NULL) {
      abort();
   }

   memset( new_cb, 0, sizeof(cfg_cb_list_t) );
   if (!new_cb) {
      Log(LOG_CRIT, "cfg", "OOM in cfg_add_callback");

      return false;
   }

   if (path) {
      if ( ( new_cb->path = strdup(path) ) == NULL ) {
         abort();
      }
   }

   if (section) {
      if ( ( new_cb->section = strdup(section) ) == NULL ) {
         abort();
      }
   }
   new_cb->callback = cb;

   Log(LOG_DEBUG, "cfg", "Stored config callback cb:<%p> for section:|%s| path:|%s|", cb, section, path);

   // store our new callback
   if (!cfg_callbacks) {
      cfg_callbacks = new_cb;
   } else {
      // Find the end of the list
      cfg_cb_list_t *cbp = cfg_callbacks;

      while (cbp) {
         if (!cbp->next) {
            cbp->next = new_cb;
            break;
         }
         cbp = cbp->next;
      }
   }

   return true;
}

static bool cfg_dispatch_callback(const char *path, int line, const char *section, const char *buf) {
   if (!path || !section || !buf) {
      return true;
   }
   cfg_cb_list_t *cbp = cfg_callbacks, *prev = NULL;

   if (!cbp) {
      return false;
   }
   int i = 0;
   while (cbp && i < CONFIG_MAX_CALLBACKS) {
      if (cbp->section && fnmatch(cbp->section, section, 0) == 0) {
         if ( !cbp->path || (fnmatch(cbp->path, path, 0) == 0) ) {
            Log(LOG_CRAZY, "cfg", "cfg_dispatch_callback: Found callback at <%p> for section %s (%s) in path %s (%s)", cbp->callback,
               section, cbp->section, path, cbp->path);

            if (cbp->callback) {
               cbp->callback(path, line, section, buf);
            } else {
               Log(LOG_CRIT, "cfg", "cfg_dispatch_callback: The callback at <%p> for section |%s| path |%s| doesn't have a valid function attached",
                  cbp, section, path);
            }
         }
      }
      i++;
      prev = cbp;
      cbp = cbp->next;
      Log(LOG_CRAZY, "cfg", "prev;<%p> cbp:<%p> list:<%p>", prev, cbp, cfg_callbacks);
   }

   if (i > 10) {
      Log(LOG_WARN, "cfg", "%s: made (%d) iterations for cbp:<%p> for |%s| and probably could be optimized", __FUNCTION__, i, cfg_callbacks, cbp->path);
   }

   return false;
}

static dict *cfg_load_depth(const char *path, unsigned depth);

static bool cfg_merge_dict(dict *dst, dict *src) {
   int rank = 0;
   const char *key = NULL;
   char *val = NULL;

   while ((rank = dict_enumerate(src, rank, &key, &val)) >= 0) {
      if (dict_add(dst, key, val) != 0) {
         Log(LOG_WARN, "cfg", "Unable to merge included key |%s|", key);
         return true;
      }
   }
   return false;
}

static dict *cfg_load_depth(const char *path, unsigned depth) {
   int line = 0, errors = 0;
   char buf[32768];
   char *end, *skip, *key, *val;
   char this_section[128];

   memset( this_section, 0, sizeof(this_section) );

   if ( !file_exists(path) ) {
      Log(LOG_CRIT, "cfg", "Can't find config file %s", path);
      return NULL;
   }

   dict *newcfg = dict_new();
   if (!newcfg) {
      fprintf(stderr, "OOM in cfg_load?!\n");
      exit(EXIT_FAILURE);
   }

   // Temporarily point the global cfg at the dict being built so section
   // callbacks (which dict_add() into cfg) write into the dict we will
   // return. We must restore the previous value before returning -- callers
   // like cfg_reload() rely on cfg still being the live config dict, and
   // dict_free()ing newcfg would otherwise leave cfg dangling.
   dict *saved_cfg = cfg;
   cfg = newcfg;

   FILE *fp = fopen(path, "r");
   if (!fp) {
      free(newcfg);
      cfg = saved_cfg;
      fprintf( stderr, "Failed to open config %s: %d:%s\n", path, errno, strerror(errno) );
      return NULL;
   }
   fseek(fp, 0, SEEK_SET);

   bool in_comment = false;
   while (true) {
      memset( buf, 0, sizeof(buf) );
      if ( !fgets(buf, sizeof(buf) - 1, fp) ) {
         break;
      }
      line++;

      // skip leading spaces
      skip = buf;
      while (*skip == ' ') {
         skip++;
      }
      // trim trailing newlines and whitespace
      end = buf + strlen(buf) - 1;
      while ( end >= buf && (*end == '\r' || *end == '\n' || *end == ' ' || *end == '\t') ) {
         *end-- = '\0';
      }

      if ( (end - skip) < 0 ) {
         continue;
      }

      // Includes are processed inline. Values from the included file are
      // copied into the current dictionary, so entries later in this file
      // retain normal last-value-wins behavior. A leading ! makes an include
      // optional; a missing mandatory include is a fatal configuration error.
      bool optional_include = (*skip == '!' && strncasecmp(skip + 1, "include", 7) == 0);
      bool mandatory_include = (*skip == '.' && strncasecmp(skip + 1, "include", 7) == 0);
      if (!in_comment && (optional_include || mandatory_include) &&
          (skip[8] == '\0' || skip[8] == ' ' || skip[8] == '\t' || skip[8] == '=')) {
         bool optional = optional_include;
         char *include_path = skip + 8;
         while (*include_path == ' ' || *include_path == '\t' || *include_path == '=') {
            include_path++;
         }
         if (*include_path == '"' || *include_path == '\'') {
            char quote = *include_path++;
            char *close = strrchr(include_path, quote);
            if (close) {
               *close = '\0';
            }
         }
         if (!*include_path) {
            if (optional) {
               Log(LOG_INFO, "cfg", "Empty optional !include at %s:%d", path, line);
            } else {
               Log(LOG_CRIT, "cfg", "Empty mandatory .include at %s:%d", path, line);
               exit(EXIT_FAILURE);
            }
            errors++;
            continue;
         }
         if (depth >= 4) {
            if (optional) {
               Log(LOG_INFO, "cfg", "Maximum !include depth reached at %s:%d (limit 4)", path, line);
            } else {
               Log(LOG_CRIT, "cfg", "Maximum .include depth reached at %s:%d (limit 4)", path, line);
               exit(EXIT_FAILURE);
            }
            errors++;
            continue;
         }

         char resolved[PATH_MAX];
         if (include_path[0] == '/') {
            snprintf(resolved, sizeof(resolved), "%s", include_path);
         } else {
            char parent[PATH_MAX];
            snprintf(parent, sizeof(parent), "%s", path);
            char *dir = dirname(parent);
            snprintf(resolved, sizeof(resolved), "%s/%s", dir, include_path);
         }
         if (!file_exists(resolved)) {
            if (optional) {
               Log(LOG_INFO, "cfg", "Optional include file not found: %s (from %s:%d)", resolved, path, line);
               continue;
            }
            Log(LOG_CRIT, "cfg", "Mandatory include file not found: %s (from %s:%d)", resolved, path, line);
            exit(EXIT_FAILURE);
         }
         dict *included = cfg_load_depth(resolved, depth + 1);
         if (!included) {
            if (optional) {
               Log(LOG_INFO, "cfg", "Optional include file not found: %s (from %s:%d)", resolved, path, line);
            } else {
               Log(LOG_CRIT, "cfg", "Mandatory include file not found: %s (from %s:%d)", resolved, path, line);
               exit(EXIT_FAILURE);
            }
            errors++;
         } else {
            if (cfg_merge_dict(newcfg, included)) {
               if (!optional) {
                  Log(LOG_CRIT, "cfg", "Unable to merge mandatory include %s", resolved);
                  dict_free(included);
                  exit(EXIT_FAILURE);
               }
               Log(LOG_INFO, "cfg", "Unable to merge optional include %s", resolved);
               errors++;
            }
            dict_free(included);
         }
         continue;
      }
      // Handle line continuations
      while (1) {
         // Trim trailing newlines / carriage returns
         while ( end >= buf && (*end == '\r' || *end == '\n') ) {
            *end-- = '\0';
         }
         // Trim trailing spaces/tabs before checking for '\'
         while ( end >= buf && (*end == ' ' || *end == '\t') ) {
            *end-- = '\0';
         }

         if (end < buf || *end != '\\') {
            // No continuation
            break;
         }
         // Check if space before '\'
         bool space_before = (end > buf && *(end - 1) == ' ');

         // Remove the backslash
         *end = '\0';
         end--;

         // Also remove trailing spaces before backslash if any remain
         while ( end >= buf && (*end == ' ' || *end == '\t') ) {
            *end-- = '\0';
         }
         // Read continuation line
         char contbuf[sizeof(buf)];

         if ( !fgets(contbuf, sizeof(contbuf), fp) ) {
            break;   // EOF or error
         }
         line++;

         // Trim leading whitespace on continuation line
         char *cont = contbuf;
         while (*cont == ' ' || *cont == '\t') {
            cont++;
         }
         // Trim trailing whitespace/newlines on continuation line
         char *e2 = cont + strlen(cont) - 1;
         while ( e2 >= cont && (*e2 == '\r' || *e2 == '\n' || *e2 == ' ' || *e2 == '\t') ) {
            *e2-- = '\0';
         }

         // Append a space if needed
         if (space_before && strlen(buf) > 0) {
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
         }
         // Append continuation content
         strncat(buf, cont, sizeof(buf) - strlen(buf) - 1);

         // Update end pointer for next loop iteration
         end = buf + strlen(buf) - 1;
      }

      /////////////////////////////
      // parse the line contents //
      /////////////////////////////
      // A leading '\' escapes the comment characters (#, ;, //) so e.g.
      // CSS selectors like '#chat-view' can be used in config sections.
      // We unescape in place here, before any comment processing.
      if (*skip == '\\' && (skip[1] == '#' || skip[1] == ';' ||
                            (skip[1] == '/' && skip[2] == '/') || skip[1] == '\\') ) {
         memmove(skip, skip + 1, strlen(skip));        // shift left incl. NUL
         // Unescape any further occurrences in the line (e.g. 'a \# b \# c')
         for (char *p = skip; *p; p++) {
            if (*p == '\\' && (p[1] == '#' || p[1] == ';' || p[1] == '\\')) {
               memmove(p, p + 1, strlen(p));
            }
         }
         // recompute end pointer after the shifts
         end = buf + strlen(buf) - 1;
      }

      if (*skip == '*' && *(skip + 1) == '/') {
         in_comment = false;
         continue;
      } else if (*skip == '/' && *(skip + 1) == '*') {
         in_comment = true;
         continue;
      } else if (in_comment) {
         continue;
      } else if ( (*skip == '/' && *(skip + 1) == '/') || *skip == '#' || *skip == ';' ) {
         continue;
      } else if (*skip == '[' && *end == ']') {
         size_t section_len = sizeof(this_section);
         size_t skip_len = strlen(skip);
         // Copy only the text between the opening and closing brackets.
         // The previous -1 length copied the closing ']' into section names,
         // producing keys such as server:localhost].server.url.
         size_t copy_len = skip_len > 2 ? skip_len - 2 : 0;
         if (copy_len >= section_len) {
            copy_len = section_len - 1;
         }
         memset(this_section, 0, section_len);
         memcpy(this_section, skip + 1, copy_len);
         this_section[copy_len] = '\0';
         continue;
      }

      if (this_section[0] == '\0') {
         fprintf(stderr, "[Debug] config %s has line outside section header at line %d: %s\n", path, line, buf);
         errors++;
         continue;
      }

      if (strncasecmp(this_section, "general", 7) == 0) {
         key = NULL;
         val = NULL;
         char *eq = strchr(skip, '=');

         if (eq) {
            *eq = '\0';
            key = skip;
            val = eq + 1;
            while (*val == ' ' || *val == '\t') {
               val++;
            }
         }

         if (!key) {
            continue;
         }
         char *key_end = key + strlen(key) - 1;
         while ( key_end >= key && (*key_end == ' ' || *key_end == '\t') ) {
            *key_end-- = '\0';
         }

         if (!key && !val) {
            continue;
         }
         dict_add(newcfg, key, val);
      } else if (strncasecmp(this_section, "server:", 7) == 0) {
         key = NULL;
         val = NULL;
         char *eq = strchr(skip, '=');
         char fullkey[256];

         if (eq) {
            *eq = '\0';
            key = skip;
            val = eq + 1;
            while (*val == ' ' || *val == '\t') {
               val++;
            }
            strlcpy(fullkey, "server:", sizeof(fullkey));
            strlcat(fullkey, this_section + 7, sizeof(fullkey));
            strlcat(fullkey, ".", sizeof(fullkey));
            strlcat(fullkey, key, sizeof(fullkey));
            dict_add(newcfg, fullkey, val);
         } else {
            Log(LOG_CRIT, "cfg", "Malformed line parsing |%s| at %s:%d", buf, path, line);
         }
      } else if (strncasecmp(this_section, "callsign-lookup", 15) == 0 &&
                 this_section[15] == '\0') {
         key = NULL;
         val = NULL;
         char *eq = strchr(skip, '=');
         char fullkey[256];

         if (eq) {
            *eq = '\0';
            key = skip;
            val = eq + 1;
            while (*val == ' ' || *val == '\t') {
               val++;
            }
            while (*key && (key[strlen(key) - 1] == ' ' || key[strlen(key) - 1] == '\t')) {
               key[strlen(key) - 1] = '\0';
            }
            if (*key) {
               strlcpy(fullkey, "callsign-lookup:", sizeof(fullkey));
               strlcat(fullkey, key, sizeof(fullkey));
               dict_add(newcfg, fullkey, val);
            }
         } else {
            Log(LOG_CRIT, "cfg", "Malformed line parsing |%s| at %s:%d", buf, path, line);
         }
      } else if (strncasecmp(this_section, "site", 4) == 0 &&
                 this_section[4] == '\0') {
         key = NULL;
         val = NULL;
         char *eq = strchr(skip, '=');
         char fullkey[256];
         if (eq) {
            *eq = '\0';
            key = skip;
            val = eq + 1;
            while (*val == ' ' || *val == '\t') {
               val++;
            }
            while (*key && (key[strlen(key) - 1] == ' ' || key[strlen(key) - 1] == '\t')) {
               key[strlen(key) - 1] = '\0';
            }
            if (*key) {
               strlcpy(fullkey, "site:", sizeof(fullkey));
               strlcat(fullkey, key, sizeof(fullkey));
               dict_add(newcfg, fullkey, val);
            }
         } else {
            Log(LOG_CRIT, "cfg", "Malformed line parsing |%s| at %s:%d", buf, path, line);
         }
      } else if ( cfg_dispatch_callback(path, line, this_section, buf) ) {
         Log(LOG_CRIT, "cfg", "Unknown configuration section |%s| parsing |%s| at %s:%d", this_section, buf, path,
            line);
         errors++;
      }
   }

   if (errors > 0) {
      Log(LOG_INFO, "cfg", "cfg loaded %d lines from %s with %d warnings/errors", line, path, errors);
   } else {
      Log(LOG_INFO, "cfg", "cfg loaded %d lines from %s with no errors", line, path);
   }

   if (fp) {
      fclose(fp);
   }

   // Restore the global cfg pointer. Callers decide what to do with newcfg:
   // initial load takes ownership of it, cfg_reload() merges it into the
   // live cfg and frees it.
   cfg = saved_cfg;
   return newcfg;
}

dict *cfg_load(const char *path) {
   return cfg_load_depth(path, 0);
}

const char *cfg_get(const char *key) {
   if (!key) {
      Log(LOG_CRIT, "cfg", "got cfg_get with NULL key!");
      return NULL;
   }
   const char *p = dict_get(cfg, key, NULL);

   // nope! try default
   if (!p) {
      if (!default_cfg) {
         Log(LOG_CRAZY, "cfg", "defcfg not found looking for key |%s|", key);
         return NULL;
      }
      p = dict_get(default_cfg, key, NULL);
      Log(LOG_CRAZY, "cfg", "returning default value |%s| for key |%s|", p, key);
   } else {
      Log(LOG_CRAZY, "cfg", "returning user value |%s| for key |%s|", p, key);
   }

   return p;
}

bool cfg_get_bool(const char *key, bool def) {
   return dict_get_bool(cfg, key, def);
}

int cfg_get_int(const char *key, int def) {
   return dict_get_int(cfg, key, def);
}

unsigned long cfg_get_ulong(const char *key, unsigned long def) {
   return dict_get_ulong(cfg, key, def);
}

// You *MUST* free the return value
const char *cfg_get_exp(const char *key) {
   return dict_get_exp(cfg, key);
}

char *cfg_get_path(const char *key) {
   const char *expanded = cfg_get_exp(key);
   if (!expanded) {
      return NULL;
   }
   char *path = expand_path(expanded);
   free((void *)expanded);
   return path;
}

// ---------------------------------------------------------------
// Config save callbacks
// ---------------------------------------------------------------
cfg_save_cb_entry_t *cfg_save_callbacks = NULL;

bool cfg_add_save_callback(const char *name, cfg_save_cb_t callback) {
   if (!callback) {
      Log(LOG_WARN, "cfg", "Attempt to add NULL save callback");
      return false;
   }

   for (cfg_save_cb_entry_t *cbp = cfg_save_callbacks; cbp; cbp = cbp->next) {
      if (cbp->callback == callback) {
         Log(LOG_WARN, "cfg", "Save callback |%s| already registered", name ? name : "unnamed");
         return false;
      }
   }

   cfg_save_cb_entry_t *cb = malloc(sizeof(cfg_save_cb_entry_t));

   if (!cb) {
      fprintf(stderr, "OOM in cfg_add_save_callback!\n");
      abort();
   }
   memset(cb, 0, sizeof(cfg_save_cb_entry_t));
   cb->name = name;
   cb->callback = callback;
   cb->next = NULL;

   if (!cfg_save_callbacks) {
      cfg_save_callbacks = cb;
   } else {
      cfg_save_cb_entry_t *p = cfg_save_callbacks;

      while (p->next) {
         p = p->next;
      }
      p->next = cb;
   }
   Log(LOG_DEBUG, "cfg", "Registered save callback |%s| at <%p>", name ? name : "unnamed", callback);

   return true;
}

bool cfg_remove_save_callback(cfg_save_cb_t callback) {
   if (!callback) {
      return false;
   }

   for (cfg_save_cb_entry_t *cbp = cfg_save_callbacks, *prev = NULL; cbp; prev = cbp, cbp = cbp->next) {
      if (cbp->callback == callback) {
         if (prev) {
            prev->next = cbp->next;
         } else {
            cfg_save_callbacks = cbp->next;
         }
         free(cbp);
         return true;
      }
   }
   Log(LOG_CRIT, "cfg", "Save callback at <%p> not found for removal", callback);

   return false;
}

// Returns true when all save callbacks succeed.
bool cfg_run_save_callbacks(FILE *fp, const char *path) {
   if (!fp || !path) {
      return false;
   }

   bool errors = false;

   for (cfg_save_cb_entry_t *cbp = cfg_save_callbacks; cbp; cbp = cbp->next) {
      Log(LOG_DEBUG, "cfg", "Running save callback |%s| at <%p>", cbp->name ? cbp->name : "unnamed", cbp->callback);

      if (cbp->callback(fp, path)) {
         Log(LOG_CRIT, "cfg", "Save callback |%s| reported errors saving %s", cbp->name ? cbp->name : "unnamed", path);
         errors = true;
      }
   }

   return !errors;
}

static void cfg_print_servers(dict *d, FILE *fp) {
   if (!d || !fp) {
      return;
   }
   const char *key;
   char *val;
   int rank = 0;
   dict *seen = dict_new();

   while ( ( rank = dict_enumerate(d, rank, &key, &val) ) >= 0 ) {
      if (strncmp(key, "server:", 7) != 0) {
         continue;
      }
      const char *name_start = key + 7;
      const char *dot = strchr(name_start, '.');

      if (!dot) {
         continue;
      }
      size_t name_len = dot - name_start;
      char name[64];

      if ( name_len >= sizeof(name) ) {
         continue;
      }
      strlcpy(name, name_start, name_len + 1);
      name[name_len] = '\0';

      if ( dict_get(seen, name, NULL) ) {
         continue;
      }
      dict_add(seen, name, (char *)"1");

      fprintf(fp, "[server:%s]\n", name);

      int inner_rank = 0;
      const char *inner_key;
      char *inner_val;
      while ( ( inner_rank = dict_enumerate(d, inner_rank, &inner_key, &inner_val) ) >= 0 ) {
         if (strncmp(inner_key, "server:", 7) == 0) {
            const char *inner_name = inner_key + 7;

            if (strncmp(inner_name, name, name_len) == 0 && inner_name[name_len] == '.') {
               fprintf(fp, "%s=%s\n", inner_name + name_len + 1, inner_val ? inner_val : "");
            }
         }
      }
      fputc('\n', fp);
   }
   dict_free(seen);
}

typedef struct cfg_save_entry {
   const char *key;
   char *value;
} cfg_save_entry_t;

static bool cfg_save_entry_is_skipped(const char *key) {
   if (!key) return true;
   if (strncmp(key, "server:", 7) == 0) return true;
   if (strncmp(key, "network.", 8) == 0) return true;
   if (strcmp(key, "ui.gtk.css") == 0) return true;
   return false;
}

static int cfg_save_entry_compare(const void *left, const void *right) {
   const cfg_save_entry_t *a = left;
   const cfg_save_entry_t *b = right;
   const char *a_colon = strchr(a->key, ':');
   const char *b_colon = strchr(b->key, ':');

   /* Keep ordinary [general] keys before section-qualified keys. */
   if (!a_colon && b_colon) return -1;
   if (a_colon && !b_colon) return 1;
   if (!a_colon && !b_colon) return strcmp(a->key, b->key);

   size_t a_section_len = (size_t)(a_colon - a->key);
   size_t b_section_len = (size_t)(b_colon - b->key);
   size_t common = a_section_len < b_section_len ? a_section_len : b_section_len;
   int section_cmp = strncmp(a->key, b->key, common);
   if (section_cmp != 0) return section_cmp;
   if (a_section_len != b_section_len)
      return a_section_len < b_section_len ? -1 : 1;
   return strcmp(a_colon + 1, b_colon + 1);
}

static bool cfg_save_entry_same_section(const cfg_save_entry_t *entry,
   const char *section, size_t section_len) {
   const char *colon = strchr(entry->key, ':');
   return colon && (size_t)(colon - entry->key) == section_len &&
      strncmp(entry->key, section, section_len) == 0;
}

bool cfg_save(dict *d, const char *path) {
   // Back up the existing config before overwriting it. Keep two-digit
   // sequence numbers so multiple saves on the same day remain distinct.
   if (file_exists(path)) {
      time_t now = time(NULL);
      struct tm tm_buf;
      localtime_r(&now, &tm_buf);
      char backup[PATH_MAX];
      bool backed_up = false;

      for (unsigned int sequence = 1; sequence <= 99; sequence++) {
         int written = snprintf(backup, sizeof(backup),
            "%s.%04d%02d%02d.%02u.cfg.old", path,
            tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday, sequence);
         if (written < 0 || (size_t)written >= sizeof(backup)) {
            Log(LOG_WARN, "cfg", "Config backup path is too long for '%s'", path);
            return false;
         }
         if (file_exists(backup)) continue;
         if (rename(path, backup) == 0) {
            Log(LOG_INFO, "cfg", "Saved previous config as '%s'", backup);
            backed_up = true;
         } else {
            Log(LOG_WARN, "cfg", "Failed to back up config '%s' to '%s': %d:%s",
               path, backup, errno, strerror(errno));
         }
         break;
      }
      if (!backed_up) {
         Log(LOG_CRIT, "cfg", "Unable to create a backup before saving '%s'", path);
         return false;
      }
   }

   FILE *fp = fopen(path, "w");

   if (!fp) {
      Log( LOG_CRIT, "cfg", "Failed to open save file: '%s': %d:%s", path, errno, strerror(errno) );

      return false;
   }
   dict *merged = NULL;

   // Right-side argument overrides defaults
   merged = dict_merge_new(default_cfg, d);

   /* Collect entries once so the output is deterministic and section-qualified
    * keys can be written back in the INI section form accepted by the loader.
    * For example, fwdsp:path becomes [fwdsp] path=... . */
   cfg_save_entry_t *entries = NULL;
   size_t entry_count = 0;
   size_t entry_capacity = 0;
   int rank = 0;
   const char *key;
   char *val;
   while ( ( rank = dict_enumerate(merged, rank, &key, &val) ) >= 0 ) {
      if (cfg_save_entry_is_skipped(key)) continue;
      if (entry_count == entry_capacity) {
         size_t next = entry_capacity ? entry_capacity * 2 : 64;
         cfg_save_entry_t *grown = realloc(entries, next * sizeof(*entries));
         if (!grown) {
            Log(LOG_CRIT, "cfg", "Unable to allocate configuration save entries");
            free(entries);
            dict_free(merged);
            fclose(fp);
            return false;
         }
         entries = grown;
         entry_capacity = next;
      }
      entries[entry_count].key = key;
      entries[entry_count].value = val;
      entry_count++;
   }

   qsort(entries, entry_count, sizeof(*entries), cfg_save_entry_compare);

   fprintf(fp, "[general]\n");
   size_t i = 0;
   for (; i < entry_count; i++) {
      if (strchr(entries[i].key, ':')) break;
      fprintf(fp, "%s=%s\n", entries[i].key,
         entries[i].value ? entries[i].value : "");
   }

   while (i < entry_count) {
      const char *colon = strchr(entries[i].key, ':');
      if (!colon) {
         i++;
         continue;
      }
      size_t section_len = (size_t)(colon - entries[i].key);
      const char *section_name = entries[i].key;
      if (section_len == 8 && strncmp(section_name, "pipeline", 8) == 0) {
         section_name = "pipelines";
      }
      fprintf(fp, "\n[%.*s]\n", (int)(section_name == entries[i].key ? section_len : 9),
         section_name);
      while (i < entry_count && cfg_save_entry_same_section(entries + i,
            entries[i].key, section_len)) {
         const char *entry_colon = strchr(entries[i].key, ':');
         fprintf(fp, "%s=%s\n", entry_colon + 1,
            entries[i].value ? entries[i].value : "");
         i++;
      }
   }

   free(entries);
   dict_free(merged);

   // Print the server sections
   cfg_print_servers(d, fp);

   // Let registered modules emit their own sections (network:autojoin, etc)
   cfg_run_save_callbacks(fp, path);

   fflush(fp);
   fclose(fp);

   return true;
}

/* PARITY: rustyrig-www/js/webui.config.js */
bool cfg_apply_new(dict *oldcfg, dict *newcfg) {
   if (!newcfg) {
      Log(LOG_CRIT, "cfg", "cfg_apply_new: newcfg is NULL, ignoring");
      return false;
   }

   int rank = 0;
   const char *key;
   dict_value_t val;
   val_type_t type;
   int changed = 0, added = 0, removed = 0;

   /* Full swap into the live cfg dict: add/update everything in newcfg,
      remove everything in oldcfg that's gone from newcfg. */

   // Add/update every key from the new config in the live config
   while ( ( rank = dict_enumerate_typed(newcfg, rank, &key, &val, &type) ) >= 0 ) {
      const char *oldval = NULL;
      const char *newval = NULL;

      if (oldcfg) {
         oldval = dict_get(oldcfg, key, NULL);
      }

      if (type == VAL_STR) {
         newval = val.s;
      }

      if (oldcfg && oldval && newval && strcmp(oldval, newval) == 0) {
         continue;   // Unchanged
      }

      // Replace the value in the live config
      dict_add(cfg, key, newval);

      if (oldcfg && oldval) {
         Log(LOG_DEBUG, "cfg", "cfg_apply_new: '%s' changed: '%s' => '%s'",
             key, oldval, newval ? newval : "");
         changed++;
      } else {
         Log(LOG_DEBUG, "cfg", "cfg_apply_new: '%s' added: '%s'",
             key, newval ? newval : "");
         added++;
      }

      // Run any reload callbacks registered for this key
      reload_event_run(key);
   }

   // Remove keys from the live config that no longer exist in the new config
   rank = 0;
   const char *rkey;
   char *rval;

   while ( ( rank = dict_enumerate(cfg, rank, &rkey, &rval) ) >= 0 ) {
      if (!dict_get(newcfg, rkey, NULL)) {
         /* dict_del() frees the stored key, so we must work on a copy:
            rkey would otherwise dangle for the Log/reload_event_run calls
            below (and any callbacks they trigger). */
         char *rkcopy = strdup(rkey);

         if (!rkcopy) {
            Log(LOG_CRIT, "cfg", "cfg_apply_new: strdup failed removing '%s'", rkey);
            rank = 0;
            continue;
         }
         Log(LOG_DEBUG, "cfg", "cfg_apply_new: '%s' removed", rkcopy);
         dict_del(cfg, rkcopy);
         removed++;
         reload_event_run(rkcopy);
         free(rkcopy);
         rank = 0;   // Restart enumeration, the dict may have been modified
      }
   }

   /* Notify modules once after the complete new configuration is live.  A
      reload event registered with a NULL key is intentionally reserved for
      this phase, so cached runtime settings can be refreshed atomically. */
   reload_event_run(NULL);

   Log(LOG_INFO, "cfg", "cfg_apply_new: %d added, %d changed, %d removed",
       added, changed, removed);

   // Free the old config dict if it isn't the live one
   if (oldcfg && oldcfg != cfg) {
      dict_free(oldcfg);
   }

   // Free the temporary new dict -- its values were copied into cfg
   dict_free(newcfg);

   return true;
}

/*
 * Reload a config file into the global cfg dict.
 * The caller may pass a specific config file name; if NULL, the currently
 * loaded config_file is re-read.
 */
bool cfg_reload(const char *filename) {
   const char *path = filename ? filename : config_file;

   if (!path) {
      Log(LOG_CRIT, "cfg", "cfg_reload: No config file to reload");
      return false;
   }

   Log(LOG_INFO, "cfg", "cfg_reload: Starting config reload from %s", path);

   dict *newcfg = cfg_load(path);

   if (!newcfg) {
      Log(LOG_CRIT, "cfg", "cfg_reload: Failed to load config from %s", path);
      return false;
   }

   cfg_apply_new(cfg, newcfg);
   Log(LOG_INFO, "cfg", "cfg_reload: Finished reloading config from %s", path);

   return true;
}

// Config save stuff
#if     0       // XX: Not yet
char pathbuf[PATH_MAX + 1];
memset( pathbuf, 0, sizeof(pathbuf) );

// If we don't couldnt find a config file, save the defaults to
// ~/.config/rrgtk.cfg
if (homedir && empty_config) {
#ifdef _WIN32
   snprintf(pathbuf, sizeof(pathbuf), "%%APPDATA%%\\rrgtk\\rrgtk.cfg");
#else
   snprintf(pathbuf, sizeof(pathbuf), "%s/.config/rrgtk.cfg", homedir);
#endif

   if ( !file_exists(pathbuf) ) {
      Log(LOG_WARN, "main", "Saving default config to %s since it doesn't exist", pathbuf);
      cfg_save(cfg, pathbuf);
      config_file = pathbuf;
   }
}
#endif

///////////////////
// Reload Events //
///////////////////
// This facility allows us to notify modules when a configuration key is changed
reload_event_t *reload_events = NULL;

reload_event_t *reload_event_add(const char *key, bool (*callback) (), const char *note) {
   /* A NULL key registers a callback for a completed configuration reload. */
   if (!callback) {
      return NULL;
   }
   reload_event_t *r = malloc( sizeof(reload_event_t) );

   if (r == NULL) {
      fprintf(stderr, "OOM in reload_event_add!\n");

      return NULL;
   }
   memset( r, 0, sizeof(reload_event_t) );

   if (key && ( r->key = strdup(key) ) == NULL ) {
      abort();
   }
   r->callback = callback;

   if (note) {
      if ( ( r->note = strdup(note) ) == NULL ) {
         abort();
      }
   }
   // Find the end of the list and append it.  The first registration must
   // become the list head; losing it here silently disables every reload
   // callback until a later registration happens.
   if (!reload_events) {
      reload_events = r;
   } else {
      reload_event_t *ep = reload_events;
      while (ep->next) {
         ep = ep->next;
      }
      ep->next = r;
   }
   return r;
}

bool reload_event_list(const char *key) {
   if (!key) {
      return true;
   }
   reload_event_t *r = reload_events;

   Log(LOG_DEBUG, "cfg", "****** rel dump ******\n");
   r = reload_event_find(key, NULL);
   while (r) {
      Log(LOG_DEBUG, "cfg", "* %s has callback at <%p>: %s\n", r->key, r->callback,
         r->note ? r->note : "*** No note ***");
      r = r->next;
   }
   Log(LOG_DEBUG, "cfg", "**********************\n");

   return false;
}

// Find an event in the linked list
reload_event_t *reload_event_find( const char *key, bool (*callback) () ) {
   reload_event_t *r = reload_events;

   // one or both must be passed
   if (!key && !callback) {
      return NULL;
   }
   while (r) {
      bool match_key = false, match_cb = false;

      if ((!key && !r->key) || (key && r->key && strcasecmp(key, r->key) == 0)) {
         match_key = true;
      }

      if (callback && r->callback && callback == r->callback) {
         match_cb = true;
      }
      Log(LOG_DEBUG, "cfg", "reload_event_find matched entry at <%p>, key: %s <%p>, callback:%s <%p>", r,
         (match_key ? "true" : "false"), r->key, (match_cb ? "true" : "false"), r->callback);

      // If (no key or key matches) and (no callback or callback matches),
      // return the entry
      if ( (!key || match_key) && (!callback || match_cb) ) {
         return r;
      }
      r = r->next;
   }
   return NULL;
}

bool reload_event_run(const char *key) {
   if (!reload_events) {
      return false;
   }
   bool ran = false;
   for (reload_event_t *rl = reload_events; rl; rl = rl->next) {
      bool match = (!key && !rl->key) ||
                   (key && rl->key && strcasecmp(key, rl->key) == 0);
      if (match) {
         Log(LOG_DEBUG, "cfg", "reload: run callback at <%p> for key '%s'",
             rl->callback, key ? key : "<complete>");
         rl->callback(key);
         ran = true;
      }
   }
   return ran;
}

// Remove a reload event from the list
bool reload_event_remove(reload_event_t *evt) {
   if (!evt) {
      return false;
   }
   // Free resources
   free(evt);
   return true;
}
