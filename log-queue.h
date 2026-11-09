//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Bounded, allocation-free submission for nonblocking worker logging. Drain on the thread that owns Log() and its callbacks. */
#ifndef LIBRUSTYAXE_LOG_QUEUE_H
#define LIBRUSTYAXE_LOG_QUEUE_H
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>
#include <librustyaxe/logger.h>
typedef struct ra_log_queue ra_log_queue_t;
ra_log_queue_t *ra_log_queue_new(size_t capacity);
void ra_log_queue_free(ra_log_queue_t *queue);
/* Never waits: returns false on contention or full queue. Message is copied. */
bool ra_log_queue_push(ra_log_queue_t *queue, logpriority_t level, const char *subsys, const char *message);
/* Must be called from the Log() owning thread; returns records consumed. */
size_t ra_log_queue_drain(ra_log_queue_t *queue, size_t max_records);
/* Captured at submission; valid only while the owning drain invokes Log(). */
bool ra_log_queue_delivery_time(struct timespec *timestamp);
uint64_t ra_log_queue_dropped(const ra_log_queue_t *queue);
uint64_t ra_log_queue_contention_dropped(const ra_log_queue_t *queue);
uint64_t ra_log_queue_full_dropped(const ra_log_queue_t *queue);
/* First subsystem to encounter a full queue since the previous snapshot. */
void ra_log_queue_full_source(ra_log_queue_t *queue, char *out, size_t out_size);
size_t ra_log_queue_depth(ra_log_queue_t *queue);

#endif
