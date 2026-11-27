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
   RRS_NULL, RRS_INT, RRS_FLOAT, RRS_BOOL, RRS_CHAR, RRS_STRING, RRS_DICT, RRS_ARRAY
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
rrs_value_t *rrs_array(void);
bool rrs_array_append(rrs_value_t *array, const rrs_value_t *value);
size_t rrs_array_length(const rrs_value_t *array);
const rrs_value_t *rrs_array_get(const rrs_value_t *array, size_t index);
bool rrs_dict_put(rrs_value_t *dict, const char *key, const rrs_value_t *value);
rrs_value_t *rrs_value_copy(const rrs_value_t *value);
/* Human-readable representation of any value, including nested dictionaries.
 * Returns a malloc-allocated string; free() it after use. */
char *rrs_value_repr(const rrs_value_t *value);
void rrs_value_free(rrs_value_t *value);

typedef struct {
   size_t used_bytes; /* managed cells and their recursively-owned values */
   size_t peak_bytes;
   size_t live_cells;
   size_t peak_cells;
   size_t cell_allocations;
   size_t cell_releases;
   size_t reference_bindings;
   size_t collection_requests;
} rrs_memory_stats_t;
/* Managed variable cells are reference-counted; their current graph is
 * acyclic because composite values contain copies, not references.
 * A collection request reports zero unreachable cycles in this VM version. */
void rrs_vm_memory_stats(const rrs_vm_t *vm,rrs_memory_stats_t *stats);
size_t rrs_vm_gc_collect(rrs_vm_t *vm);

rrs_vm_t *rrs_vm_new(void);
void rrs_vm_free(rrs_vm_t *vm);
bool rrs_vm_register_native(rrs_vm_t *vm, const char *name, rrs_native_fn fn, void *user);
void rrs_vm_raise(rrs_vm_t *vm, const char *message);
const char *rrs_vm_error(const rrs_vm_t *vm);
/* Position of the currently executing statement/native callback (1-based). */
void rrs_vm_position(const rrs_vm_t *vm, unsigned *line, unsigned *column);
/* Load and compile registration envelope atomically. Old handlers survive a
 * failure. Handler bodies are executed only by invoke(). */
bool rrs_vm_load(rrs_vm_t *vm, const char *source);
/* Run one bound handler, passing event as an owned snapshot. Returns false for
 * unknown handler or runtime error. This function is not thread-safe. Hosts
 * must dispatch onto their script execution context. */
bool rrs_vm_invoke(rrs_vm_t *vm, rrscript_binding_kind_t kind,
                   const char *name, const rrs_value_t *event);
/* Evaluate console code in a persistent, VM-local scope. The REPL accepts
 * expressions and RustyScript statements; result is an owned value (null
 * for statements). On error, use rrs_vm_error(). No script registrations. */
bool rrs_vm_eval(rrs_vm_t *vm,const char *source,rrs_value_t **result);
size_t rrs_vm_binding_count(const rrs_vm_t *vm);
/* Enumerate current registrations to attach/unattach host command, key and
 * event hooks. Borrowed pointers live until next load or VM destruction. */
const rrscript_binding_t *rrs_vm_binding(const rrs_vm_t *vm, size_t index);
#endif
