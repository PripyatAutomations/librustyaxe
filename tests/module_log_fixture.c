// Exercise the same logger registration lifetime as the GTK module.
#include <librustyaxe/core.h>
static struct log_callback *token;
static unsigned calls;
static bool callback(logpriority_t priority, const char *subsys, const char *fmt, va_list ap) {
   calls++;

   return false;
}
unsigned fixture_calls(void) {
   return calls;
}
bool rr_module_init(void) {
   token = log_add_callback_token(callback);

   return token == NULL;
}
void rr_module_shutdown(void) {
   if (token) {
      log_remove_callback(token);
      token = NULL;
   }
}
