/* RustyScript registration parser. SPDX-License-Identifier: MIT */
#ifndef RUSTYSCRIPT_RRSCRIPT_H
#define RUSTYSCRIPT_RRSCRIPT_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
   RRSCRIPT_COMMAND = 1,
   RRSCRIPT_HOTKEY,
   RRSCRIPT_EVENT
} rrscript_binding_kind_t;
/* Body/source spans are borrowed from the source passed to parse(). The host
 * must copy them if it stores them beyond the parse callback. */
typedef struct {
   rrscript_binding_kind_t kind;
   const char *name;
   size_t name_length;
   const char *body;
   size_t body_length;
   unsigned line;
} rrscript_binding_t;
typedef int (*rrscript_register_fn)(const rrscript_binding_t *, void *);
typedef struct {
   unsigned line, column;
   char message[160];
} rrscript_error_t;
/* v0: parse top-level registrations only. Bodies are balanced and retained,
 * but deliberately not executed or expression-checked yet.
 * Returns 0 on success, -1 on invalid input/callback failure. */
int rrscript_parse_registrations(const char *source, rrscript_register_fn register_fn,
                                void *user, rrscript_error_t *error);
#ifdef __cplusplus
}
#endif
#endif
