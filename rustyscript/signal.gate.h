/* SPDX-License-Identifier: MIT */
#ifndef RRCLIENT_SIGNAL_GATE_H
#define RRCLIENT_SIGNAL_GATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Transport-neutral pin contract. ANY is resolved by a patch, never a
 * payload type. PTT/COS are semantic roles of Boolean signals. */
typedef enum {
   RR_SIGNAL_ANY=0, RR_SIGNAL_BOOL, RR_SIGNAL_NUMBER, RR_SIGNAL_PCM,
   RR_SIGNAL_SERIAL, RR_SIGNAL_SPECTRUM, RR_SIGNAL_STRING
} rr_signal_type_t;
typedef enum {
   RR_SIGNAL_ROLE_GENERIC=0, RR_SIGNAL_ROLE_PTT, RR_SIGNAL_ROLE_COS
} rr_signal_role_t;
typedef struct {
   rr_signal_type_t type;
   rr_signal_role_t role;
} rr_signal_pin_t;
typedef struct {
   rr_signal_pin_t pin;
   bool valid;
   const void *data;
   size_t bytes;
} rr_signal_value_t;

/* No implicit reinterpretation of a payload as another signal type. */
static inline bool rr_signal_pin_compatible(rr_signal_pin_t from,rr_signal_pin_t to) {
   if (from.type>RR_SIGNAL_STRING || to.type>RR_SIGNAL_STRING ||
       from.role>RR_SIGNAL_ROLE_COS || to.role>RR_SIGNAL_ROLE_COS) { return false; }
   if ((from.role!=RR_SIGNAL_ROLE_GENERIC && from.type!=RR_SIGNAL_BOOL && from.type!=RR_SIGNAL_ANY) ||
       (to.role!=RR_SIGNAL_ROLE_GENERIC && to.type!=RR_SIGNAL_BOOL && to.type!=RR_SIGNAL_ANY)) { return false; }
   return (from.type==RR_SIGNAL_ANY || to.type==RR_SIGNAL_ANY || from.type==to.type) &&
      (from.role==RR_SIGNAL_ROLE_GENERIC || to.role==RR_SIGNAL_ROLE_GENERIC || from.role==to.role);
}

/* Resolve polymorphic sockets at connection time, never while forwarding.
 * Both endpoints must agree on a concrete payload and compatible roles. */
static inline bool rr_signal_pin_resolve(rr_signal_pin_t from,rr_signal_pin_t to,
                                         rr_signal_pin_t *resolved) {
   if (!resolved || !rr_signal_pin_compatible(from,to)) { return false; }
   rr_signal_type_t type=from.type!=RR_SIGNAL_ANY?from.type:to.type;
   if (type==RR_SIGNAL_ANY) { return false; }
   rr_signal_role_t role=from.role!=RR_SIGNAL_ROLE_GENERIC?from.role:to.role;
   if (role!=RR_SIGNAL_ROLE_GENERIC && type!=RR_SIGNAL_BOOL) { return false; }
   *resolved=(rr_signal_pin_t){type,role};
   return true;
}

/* A cable's runtime value must match the type resolved at patch time.
 * This prevents ANY from becoming an untyped escape hatch. */
static inline bool rr_signal_value_matches(rr_signal_value_t value,
                                            rr_signal_pin_t patched) {
   return value.valid && value.data && value.bytes &&
      patched.type!=RR_SIGNAL_ANY &&
      value.pin.type==patched.type &&
      rr_signal_pin_compatible(value.pin,patched);
}

/* Host-independent socket definition for any modular graph. The host owns
 * placement, persistence and execution; socket direction and compatibility
 * are shared by radio, audio and automation applications. */
typedef enum {
   RR_SIGNAL_INPUT=0, RR_SIGNAL_OUTPUT=1
} rr_signal_direction_t;
typedef struct {
   const char *id;                 /* Stable within its owning module. */
   rr_signal_pin_t pin;
   rr_signal_direction_t direction;
} rr_signal_port_t;

/* Output -> input only. The resolved type belongs to this connection,
 * not globally to a polymorphic socket (which may have many cables). */
static inline bool rr_signal_patch_resolve(const rr_signal_port_t *source,
                                           const rr_signal_port_t *sink,
                                           rr_signal_pin_t *resolved) {
   if (!source || !sink || !source->id || !*source->id ||
       !sink->id || !*sink->id || !resolved ||
       source->direction!=RR_SIGNAL_OUTPUT ||
       sink->direction!=RR_SIGNAL_INPUT) { return false; }
   return rr_signal_pin_resolve(source->pin,sink->pin,resolved);
}

/* A gate forwards a borrowed payload unchanged. Closed means absent,
 * not a fabricated numeric value or a buffered packet. Consumers choose
 * silence/invalid/deassertion as appropriate for their type. */
static inline rr_signal_value_t rr_signal_gate(rr_signal_value_t input,bool enable_valid,bool enable) {
   if (!enable_valid || !enable || !input.valid || input.pin.type==RR_SIGNAL_ANY ||
       input.pin.type>RR_SIGNAL_STRING || !input.data || !input.bytes) {
      input.valid=false;
      input.data=NULL;
      input.bytes=0;
   }
   return input;
}
/* Typed Boolean trigger path used by any gate, including PCM and serial.
 * An invalid or non-Boolean trigger closes the gate (fail-safe). */
static inline rr_signal_value_t rr_signal_gate_trigger(rr_signal_value_t input,
                                                       rr_signal_value_t trigger) {
   bool enabled=false;
   bool valid=trigger.valid && trigger.pin.type==RR_SIGNAL_BOOL &&
      trigger.data && trigger.bytes>=sizeof(enabled);
   if (valid) { memcpy(&enabled,trigger.data,sizeof(enabled)); }
   return rr_signal_gate(input,valid,enabled);
}
#endif
