//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__librustyaxe_module_h)
#define __librustyaxe_module_h

#include <stdbool.h>

typedef struct rr_module_event {
   const char             *evt_name;
   bool (*evt_callback)();
   struct rr_module_event *next;
} rr_module_event_t;

/*
 * Optional per-module lifecycle entry points, resolved by dlsym() when present:
 *
 *   bool rr_module_init(void)          called once after dlopen(); return false on success, true on failure. Returning true unloads the module.
 *   void rr_module_shutdown(void)      called before dlclose(); the module must unregister every event token it created and invalidate any pointer it handed to
 * the host.
 *
 * A module that fails to clean up is left loaded (never dlclose()d) so no stale function pointer can be called after unmap.
 */
typedef struct rr_module {
   const char        *mod_path;
   const char        *mod_name;
   const char        *mod_description;
   const char        *mod_version;
   const char        *mod_copyright;
   rr_module_event_t *mod_events;                // events
   void              *dlptr;
   struct rr_module  *next;                      // next in list
} rr_module_t;

/*
 * Load a module by name (resolved through config:path.modules). Returns false on success. The module's rr_module_init() runs as part of loading; a failed init
 * unloads the module again.
 */
extern bool rr_load_module(const char *name);

/*
 * Unload a module previously loaded by name. Runs rr_module_shutdown() (if present) then dlclose(). Returns false on success.
 *
 * Hot-unload is only safe for modules whose callbacks are all removed in their shutdown path; if any remain armed the module is left loaded.
 */
extern bool rr_unload_module(const char *name);

/* Find the loaded module record for a name, or NULL. */
extern void *rr_find_loaded_module(const char *name);

#endif // !defined(__librustyaxe_module_h)
