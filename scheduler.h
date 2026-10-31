/* Portable policy wrapper: Linux/POSIX scheduling, explicit opt-in.
 * RT operations require OS permission. Apply in the target thread/process. */
#ifndef RUSTYAXE_SCHEDULER_H
#define RUSTYAXE_SCHEDULER_H
#include <stdbool.h>
#include <sys/types.h>
typedef enum {
   RA_SCHED_NORMAL = 0,
   RA_SCHED_IDLE,
   RA_SCHED_BATCH,
   RA_SCHED_FIFO,
   RA_SCHED_RR
} ra_sched_class_t;
typedef struct {
   ra_sched_class_t policy;
   int priority;  /* FIFO/RR OS priority; zero for other policies */
   int nice;      /* process nice value; -20..19 */
   bool set_nice; /* nice is process/thread implementation-dependent */
} ra_sched_options_t;
/* pid=0 selects the calling task; use native TID for Linux threads.
 * Changes are applied to the selected task, never its entire process group. */
int ra_sched_apply(pid_t pid, const ra_sched_options_t *options);
/* Force managed worker to non-RT policy before optional niceness change. */
int ra_sched_drop_rt(pid_t pid, int nice_value);
/* Query one task's policy and priority; nice is only read when supported. */
int ra_sched_query(pid_t pid, ra_sched_options_t *options);
/* Named worker profiles: normal, background, interactive, realtime-audio.
 * Null/empty name selects normal. No global privilege changes. */
int ra_sched_apply_profile(pid_t pid, const char *name);
const char *ra_sched_class_name(ra_sched_class_t policy);
#endif
