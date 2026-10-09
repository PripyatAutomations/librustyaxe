#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <librustyaxe/core.h>
time_t now;
static unsigned host_calls;
static bool host_callback(logpriority_t priority, const char *subsys, const char *fmt, va_list ap) {
   host_calls++;

   return false;
}
int main(int argc, char **argv) {
   assert(argc == 2);
   cfg = dict_new();
   dict_add(cfg, "path.modules", argv[1]);
   struct log_callback *host = log_add_callback_token(host_callback);
   assert(host);

   for (unsigned i = 0 ; i < 3 ; i++) {
      assert(!rr_load_module("module_log_fixture"));
      rr_module_t *module = rr_find_loaded_module("module_log_fixture");
      assert(module);
      unsigned (*calls)(void) = dlsym(module->dlptr, "fixture_calls");
      assert(calls);
      unsigned before = calls();
      Log(LOG_INFO, "test", "loaded callback is callable");
      assert(calls() == before + 1);
      // Use the module-owned name just as rrclient_cleanup() does.
      assert(!rr_unload_module(module->mod_name));
      unsigned host_before = host_calls;
      Log(LOG_INFO, "test", "logging after dlclose remains safe");
      assert(host_calls == host_before + 1);
   }

   assert(log_remove_callback(host));
   unsigned before = host_calls;
   Log(LOG_INFO, "test", "removed host callback stays removed");
   assert(host_calls == before);
   dict_free(cfg);
   cfg = NULL;
   logger_end();
   puts("PASS: module logger tokens unregister before dlclose, reload safely and retain host callbacks");
}
