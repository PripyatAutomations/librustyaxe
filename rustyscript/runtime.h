/* RustyScript runtime public API. SPDX-License-Identifier: MIT */
#ifndef RUSTYSCRIPT_RUNTIME_H
#define RUSTYSCRIPT_RUNTIME_H
#include "rrscript.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct rrs_vm rrs_vm_t;
typedef struct rrs_value rrs_value_t;
typedef enum {
   RRS_NULL, RRS_INT, RRS_FLOAT, RRS_BOOL, RRS_CHAR, RRS_STRING, RRS_DICT
} rrs_type_t;
/* Returned strings and dictionaries are borrowed; copy if retaining. */
rrs_type_t rrs_value_type(const rrs_value_t *value);
int64_t rrs_value_int(const rrs_value_t *value);
double rrs_value_float(const rrs_value_t *value);
bool rrs_value_bool(const rrs_value_t *value);
const char *rrs_value_string(const rrs_value_t *value);
const rrs_value_t *rrs_value_field(const rrs_value_t *value, const char *name);

/* A host function is called synchronously and must return an owned value or NULL
 * (equivalent to null). It may report an exception with rrs_vm_raise().
 * The implementation takes ownership of the returned value. */
typedef rrs_value_t *(*rrs_native_fn)(rrs_vm_t *, int argc,
                                     const rrs_value_t *const argv[], void *user);
rrs_value_t *rrs_null(void);
rrs_value_t *rrs_integer(int64_t value);
rrs_value_t *rrs_floating(double value);
rrs_value_t *rrs_boolean(bool value);
rrs_value_t *rrs_string(const char *value);
rrs_value_t *rrs_dictionary(void);
bool rrs_dict_put(rrs_value_t *dict, const char *key, const rrs_value_t *value);
rrs_value_t *rrs_value_copy(const rrs_value_t *value);
void rrs_value_free(rrs_value_t *value);

rrs_vm_t *rrs_vm_new(void);
void rrs_vm_free(rrs_vm_t *vm);
bool rrs_vm_register_native(rrs_vm_t *vm, const char *name, rrs_native_fn fn, void *user);
void rrs_vm_raise(rrs_vm_t *vm, const char *message);
const char *rrs_vm_error(const rrs_vm_t *vm);
/* Load and compile registration envelope atomically. Old handlers survive a
 * failure. Handler bodies are executed only by invoke(). */
bool rrs_vm_load(rrs_vm_t *vm, const char *source);
/* Run one bound handler, passing event as an owned snapshot. Returns false for
 * unknown handler or runtime error. This function is not thread-safe. Hosts
 * must dispatch onto their script execution context. */
bool rrs_vm_invoke(rrs_vm_t *vm, rrscript_binding_kind_t kind,
                   const char *name, const rrs_value_t *event);
size_t rrs_vm_binding_count(const rrs_vm_t *vm);
/* Enumerate current registrations to attach/unattach host command, key and
 * event hooks. Borrowed pointers live until next load or VM destruction. */
const rrscript_binding_t *rrs_vm_binding(const rrs_vm_t *vm, size_t index);
#endif
