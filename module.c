//
// module.c: Loadable module support
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * support logging to a a few places Targets: syslog console flash (file)
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <dlfcn.h>
#include <limits.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

//
// Here we deal with loading and unloading modules
//
#define	RUSTY_MODULE_API_VER 100

rr_module_t *modules = NULL;

char *concat_path(const char *dir, const char *file, const char *suffix) {
   char *tmp = malloc(PATH_MAX + 1);

   if (tmp == NULL) {
      abort();
   }
   memset(tmp, 0, PATH_MAX + 1);

   if (suffix) {
      snprintf(tmp, PATH_MAX, "%s/%s.%s", dir, file, suffix);
   } else {
      snprintf(tmp, PATH_MAX, "%s/%s", dir, file);
   }

   return tmp;
}

char *rr_find_module(const char *name) {
   // Try to find mod path and return it in an allocated string

   char *cpath = cfg_get_path("path.modules");
   char *tmp = NULL;

   if (cpath) {
      // dlopen() does not append .so to paths containing a slash, so add
      // the suffix unless the caller already included one.
      if (strstr(name, ".so")) {
         tmp = concat_path(cpath, name, NULL);
      } else {
         tmp = concat_path(cpath, name, "so");
      }
      free(cpath);
   }

   return tmp;
}

bool rr_load_module(const char *name) {
   if (!name) {
      return true;
   }
   // Already loaded?
   if (rr_find_loaded_module(name)) {
      Log(LOG_WARN, "module", "rr_load_module: %s is already loaded", name);
      return false;
   }
   // Does the module exist?
   char *mod_path = rr_find_module(name);

   if (!mod_path) {
      // Display an error that the module wasn't found
      Log(LOG_WARN, "module", "rr_load_module: Couldn't find module %s (is path.modules set?): File not found.", name);

      return true;
   }
   // Try to load the module
   void *dp = dlopen(mod_path, RTLD_NOW | RTLD_GLOBAL);

   if (!dp) {
      Log(LOG_WARN, "module", "rr_load_module: Failed opening module %s: %s", mod_path, dlerror());
      free(mod_path);
      return true;
   }
   Log(LOG_DEBUG, "module", "rr_load_module: Module %s opened from %s at <%p>", name, mod_path, dp);
   rr_module_t *mp = calloc(1, sizeof(rr_module_t));

   if (mp == NULL) {
      Log(LOG_CRIT, "librustyaxe", "OOM in rr_load_module!");
      dlclose(dp);
      free(mod_path);
      return true;
   }

   mp->dlptr = dp;

   if ( ( mp->mod_path = strdup(mod_path) ) == NULL ) {
      abort();
   }

   if ( ( mp->mod_name = strdup(name) ) == NULL ) {
      abort();
   }

   // Optional module init hook; runs before the module is registered so a
   // failed init leaves no half-initialized module behind.
   bool (*mod_init)(void) = dlsym(mp->dlptr, "rr_module_init");
   if (mod_init && mod_init()) {
      Log(LOG_CRIT, "module", "rr_load_module: init failed for %s", mod_path);
      dlclose(mp->dlptr);
      free((void *)mp->mod_name);
      free((void *)mp->mod_path);
      free(mp);
      free(mod_path);
      return true;
   }

   // Look for export list
   rr_module_event_t *mep = dlsym(mp->dlptr, "modexports");

   if (mep) {
      mp->mod_events = mep;
   } else {
      Log(LOG_WARN, "module", "rr_load_module: No exports found for module %s", mod_path);
   }

   // Add to module's list
   if (!modules) {
      modules = mp;
   } else {
      rr_module_t *lp = modules;
      while (lp->next) {
         lp = lp->next;
      }
      lp->next = mp;
   }
   Log(LOG_INFO, "module", "rr_load_module: Module %s loaded from %s", name, mod_path);
   free(mod_path);

   return false;
}

void *rr_find_loaded_module(const char *name) {
   if (!name) {
      return NULL;
   }
   for (rr_module_t *mp = modules; mp; mp = mp->next) {
      if (mp->mod_name && !strcmp(mp->mod_name, name)) {
         return mp;
      }
   }
   return NULL;
}

bool rr_unload_module(const char *name) {
   if (!name) {
      return true;
   }
   rr_module_t **link = &modules;
   while (*link && strcmp((*link)->mod_name ? (*link)->mod_name : "", name) != 0) {
      link = &(*link)->next;
   }
   if (!*link) {
      Log(LOG_WARN, "module", "rr_unload_module: %s not loaded", name);
      return true;
   }
   rr_module_t *mp = *link;

   // Give the module a chance to unregister events, cancel timers, and
   // invalidate pointers it handed out before its code is unmapped.
   void (*mod_shutdown)(void) = dlsym(mp->dlptr, "rr_module_shutdown");
   if (mod_shutdown) {
      mod_shutdown();
   }
   *link = mp->next;
   bool failed = dlclose(mp->dlptr) != 0;
   if (failed) {
      Log(LOG_CRIT, "module", "rr_unload_module: dlclose(%s) failed: %s", name, dlerror());
   } else {
      Log(LOG_INFO, "module", "rr_unload_module: Module %s unloaded", name);
   }
   free((void *)mp->mod_name);
   free((void *)mp->mod_path);
   free(mp);
   return failed;
}
