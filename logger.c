// librustyaxe/logger.c: a reusable logging system
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <fnmatch.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

#define DEFAULT_LOG_LEVEL LOG_DEBUG

// These are in main
extern char latest_timestamp[64];
extern time_t now;              // you must provide a >= 1hz refresh rate
static time_t last_ts_update;
bool log_stdout = true;
static bool tui_mode_enabled = false;

// Do we need to show a timestamp in log messages?
static bool cfg_log_show_ts = false;
char latest_timestamp[64];      // Current printed timestamp

static struct log_callback *log_callbacks = NULL;

/* String constants we use more than a few times */
const char s_prio_none[] = " NONE";

// Here we define our logo priorities to be used everywhere else
static struct log_priority log_priorities[] = {
   {
      .prio = LOG_AUDIT, .msg = "audit"
   },                                     // Auditing events
   {
      .prio = LOG_CRIT, .msg = "crit"
   },                                     // Critical issues
   {
      .prio = LOG_WARN, .msg = "warn"
   },                                     // Warnings
   {
      .prio = LOG_INFO, .msg = "info"
   },                                     // Normal operational info
   {
      .prio = LOG_DEBUG, .msg = "debug"
   },                                     // Debugging information
   {
      .prio = LOG_CRAZY, .msg = "crazy"
   },                                     // Extreme debugging
   {
      .prio = LOG_BLITZKREIG, .msg = "blitz"
   },                                           // Don't use this
   {
      .prio = LOG_NONE, .msg = s_prio_none
   }                                         // Invalid
};

FILE *logfp = NULL;

enum LogPriority log_priority_from_str(const char *priority) {
   int log_levels = (sizeof(log_priorities) / sizeof(struct log_priority));

   for (int i = 0 ; i < log_levels ; i++) {
      if (strcasecmp(log_priorities[i].msg, priority) == 0) {
         return log_priorities[i].prio;
      }
   }

   return LOG_NONE;
}

const char *log_priority_to_str(logpriority_t priority) {
   int log_levels = (sizeof(log_priorities) / sizeof(struct log_priority));

   for (int i = 0 ; i < log_levels ; i++) {
      if (log_priorities[i].prio == priority) {
         return log_priorities[i].msg;
      }
   }

   return s_prio_none;
}

struct log_filter {
   char *pattern;               // subsystem name or wildcard, e.g. "www.*"
   logpriority_t level;         // max log level to show
   unsigned rate_count;
   unsigned rate_seconds;
   struct log_filter *next;
};

static struct log_filter *log_filters = NULL;
#define LOG_RATE_SLOTS 256
struct log_rate_slot {
   char subsys[128];
   logpriority_t priority;
   unsigned emitted, suppressed;
   time_t window_start;
   bool used;
};
static struct log_rate_slot log_rate_slots[LOG_RATE_SLOTS];

/* Fixed monotonic windows, keyed by subsystem and severity. No per-call allocation. */
static bool log_rate_allowed(const char *subsys, logpriority_t level,
      unsigned count, unsigned seconds, unsigned *skipped) {
   *skipped = 0;
   if (!count || level <= LOG_AUDIT) return true;
   struct timespec ts;
   if (clock_gettime(CLOCK_MONOTONIC, &ts)) return true;
   struct log_rate_slot *slot = NULL;
   for (unsigned i = 0; i < LOG_RATE_SLOTS; ++i) {
      if (log_rate_slots[i].used && log_rate_slots[i].priority == level &&
          !strcmp(log_rate_slots[i].subsys, subsys)) {
         slot = &log_rate_slots[i];
         break;
      }
   }
   if (!slot) {
      for (unsigned i = 0; i < LOG_RATE_SLOTS; ++i)
         if (!log_rate_slots[i].used) { slot = &log_rate_slots[i]; break; }
      if (!slot) return true; /* capacity exhausted: fail open */
      memset(slot, 0, sizeof(*slot));
      slot->used = true;
      slot->priority = level;
      snprintf(slot->subsys, sizeof(slot->subsys), "%s", subsys);
      slot->window_start = ts.tv_sec;
   }
   if (ts.tv_sec - slot->window_start >= (time_t)seconds ||
       ts.tv_sec < slot->window_start) {
      *skipped = slot->suppressed;
      slot->emitted = slot->suppressed = 0;
      slot->window_start = ts.tv_sec;
   }
   if (slot->emitted >= count) {
      if (slot->suppressed < UINT32_MAX) ++slot->suppressed;
      return false;
   }
   ++slot->emitted;
   return true;
}

static bool log_rate_parse(const char *str, unsigned *count, unsigned *seconds) {
   if (!str || !*str) return false;
   char *end;
   errno = 0;
   unsigned long n = strtoul(str, &end, 10);
   if (errno || !n || n > 1000000 || *end != '/') return false;
   str = end + 1;
   unsigned long duration = 1;
   if (*str >= '0' && *str <= '9') {
      duration = strtoul(str, &end, 10);
      str = end;
   }
   unsigned long factor = !strcmp(str, "s") ? 1 : !strcmp(str, "m") ? 60 : !strcmp(str, "h") ? 3600 : 0;
   if (!factor || !duration || duration > 86400 / factor) return false;
   *count = (unsigned)n;
   *seconds = (unsigned)(duration * factor);
   return true;
}


static bool log_add_filter_rate(const char *pattern, logpriority_t level,
      unsigned count, unsigned seconds) {
   struct log_filter *f = malloc(sizeof(*f));

   if (f == NULL) {
      fprintf(stderr, "OOM in log_add_filter\n");

      return false;
   }

   if ((f->pattern = strdup(pattern)) == NULL) {
      free(f);

      return false;
   }
   f->level = level;
   f->rate_count = count;
   f->rate_seconds = seconds;
   f->next = log_filters;
   log_filters = f;

   return true;
}

bool log_add_filter(const char *pattern, logpriority_t level) {
   return log_add_filter_rate(pattern, level, 0, 0);
}

void log_clear_filters(void) {
   struct log_filter *f = log_filters;
   while (f) {
      struct log_filter *next = f->next;
      free(f->pattern);
      free(f);
      f = next;
   }
   log_filters = NULL;
   memset(log_rate_slots, 0, sizeof(log_rate_slots));
}

void load_log_filters_from_config(void) {
   const char *cfg = cfg_get_exp("log.level");

   if (!cfg) {
      return;
   }

   char *copy = strdup(cfg);
   free((void *)cfg);

   if (!copy) {
      abort();
   }

   char *tok = copy;

   while (*tok) {
      while (*tok && (isspace((unsigned char)*tok) || *tok == ',')) {
         tok++;
      }

      if (!*tok) {
         break;
      }

      char *end = tok;
      while (*end && !isspace((unsigned char)*end) && *end != ',') {
         end++;
      }

      if (*end) {
         *end++ = '\0';
      }

      char *sep = strchr(tok, ':');

      if (sep) {
         *sep = '\0';

         const char *subsys = tok;
         char *level_str = sep + 1;
         char *rate = strchr(level_str, '@');
         unsigned count = 0, seconds = 0;
         if (rate) {
            *rate++ = '\0';
            if (!log_rate_parse(rate, &count, &seconds)) {
               fprintf(stderr, "Invalid log rate for %s: %s\n", subsys, rate);
               tok = end;
               continue;
            }
         }
         logpriority_t level = log_priority_from_str(level_str);
         if (level == LOG_NONE) { tok = end; continue; }

         size_t len = strlen(subsys);

         if (len >= 2 && strcmp(subsys + len - 2, ".*") == 0) {
            char *plain = strndup(subsys, len - 2);

            if (!plain) {
               free(copy);
               abort();
            }

            log_add_filter_rate(plain, level, count, seconds);
            free(plain);
         }

         log_add_filter_rate(subsys, level, count, seconds);
      }

      tok = end;
   }
   free(copy);
}

static const struct log_filter *log_matching_filter(const char *subsys) {
   const struct log_filter *best = NULL;
   for (const struct log_filter *f = log_filters; f; f = f->next)
      if (fnmatch(f->pattern, subsys, 0) == 0 &&
          (!best || strlen(f->pattern) > strlen(best->pattern))) best = f;
   return best;
}
bool debug_filter(const char *subsys, logpriority_t msg_level) {
   const struct log_filter *best = log_matching_filter(subsys ? subsys : "");
   return msg_level > (best ? best->level : DEFAULT_LOG_LEVEL);
}

// Dump all log_filters
void log_dump_log_filters(void) {
   fprintf(stderr, "---------------------\n");
   printf("---- Log Filters ----\n");
   struct log_filter *f = log_filters;
   while (f) {
      fprintf(stderr, "  '%s' = %d (%s)\n", f->pattern, f->level, log_priority_to_str(f->level));
      f = f->next;
   }
   printf("---------------------\n");
}

//////////////////////////////
// Actual log handling code //
//////////////////////////////
void logger_init(const char *logfile, bool tui_mode) {
   const char *ll = NULL;
#if defined(USE_EEPROM)
   ll = eeprom_get_str("debug/loglevel");
   cfg_log_show_ts = eeprom_get_bool("debug/show-ts");
#endif
   ll = "crazy";
   cfg_log_show_ts = cfg_get_bool("log.show-ts", true);

   // save tui mode state so we don't trash the console...
   tui_mode_enabled = tui_mode;

   if (logfile[0] == '-' && logfile[1] == '\0') {
      logfp = stdout;
   } else {
      if (!logfp) {
         logfp = fopen(logfile, "a+");

         if (!logfp) {
            fprintf(stderr, "Couldn't open log file %s, falling back to stdout\n", logfile);
            logfp = stdout;
         }
      }
   }

   // Load fine-grained log_filters from config
   load_log_filters_from_config();
   /* Log filters remain available for explicit diagnostics only. */
#ifdef LOGGER_TRACE_FILTERS
   log_dump_log_filters();
#endif
}

void logger_set_show_timestamp(bool enabled) {
   cfg_log_show_ts = enabled;
}

void logger_end(void) {
   if (logfp && (logfp != stdout && logfp != stderr)) {
      fclose(logfp);
   }
   logfp = NULL;

   // free all registered callbacks
   struct log_callback *lp = log_callbacks;
   while (lp) {
      struct log_callback *next = lp->next;
      free(lp);
      lp = next;
   }
   log_callbacks = NULL;

   // free all log_filters
   log_clear_filters();
}

int update_timestamp(void) {
   struct tm *tmp;

   // If we've already updated this second or have gone back in time, return
   // success, to save CPU
   if (last_ts_update >= now) {
      return 0;
   }
   last_ts_update = now;
   memset(latest_timestamp, 0, sizeof(latest_timestamp));

   if ((tmp = localtime(&now))) {
      /* success, proceed */
      if (strftime(latest_timestamp, sizeof(latest_timestamp), "%Y/%m/%d %H:%M:%S", tmp) == 0) {
         /* handle the error */
         memset(latest_timestamp, 0, sizeof(latest_timestamp));
         snprintf(latest_timestamp, sizeof(latest_timestamp), "<%lu>", (unsigned long)now);
      }
   } else {
      return 1;
   }

   return 0;
}

void Log(logpriority_t priority, const char *subsys, const char *fmt, ...) {
   va_list ap;

   if (!subsys || !fmt) {
      fprintf(stderr, "Invalid Log request: No subsys/fmt\n");
      return;
   }
   va_start(ap, fmt);
   bool allowed = !debug_filter(subsys, priority);
   unsigned suppressed = 0;
   if (allowed) {
      const struct log_filter *filter = log_matching_filter(subsys);
      if (filter && !log_rate_allowed(subsys, priority,
         filter->rate_count, filter->rate_seconds, &suppressed)) allowed = false;
   }

   /* Audit/storage subscribers receive raw events; display subscribers
    * receive only admitted events. Each callback owns a fresh va_list. */
   for (struct log_callback *cb = log_callbacks; cb; cb = cb->next) {
      if (cb->callback && (cb->policy == LOG_CALLBACK_RAW || allowed)) {
         va_list args;
         va_copy(args, ap);
         cb->callback(priority, subsys, fmt, args);
         va_end(args);
      }
   }

   if (!allowed) {
      va_end(ap);
      return;
   }
   if (cfg_log_show_ts) update_timestamp();

   /* Format exactly once without a fixed buffer ceiling. Do not consume
    * the original arguments while determining the required allocation. */
   va_list sizing;
   va_copy(sizing, ap);
   int length = vsnprintf(NULL, 0, fmt, sizing);
   va_end(sizing);
   if (length < 0 || (size_t)length == SIZE_MAX) {
      va_end(ap);
      return;
   }
   char *message = malloc((size_t)length + 1);
   if (!message) {
      fprintf(stderr, "Log: out of memory formatting %s message\n", subsys);
      va_end(ap);
      return;
   }
   vsnprintf(message, (size_t)length + 1, fmt, ap);
   va_end(ap);

   /* Output the prefix, message and suppression summary separately. This
    * avoids allocating another copy or truncating at the output stage. */
   const char *level = log_priority_to_str(priority);
   FILE *destinations[2] = {logfp, NULL};
   if (!tui_mode_enabled && (!logfp || log_stdout) && logfp != stdout)
      destinations[1] = stdout;
   for (unsigned i = 0; i < 2; i++) {
      FILE *stream = destinations[i];
      if (!stream) continue;
      if (cfg_log_show_ts) fprintf(stream, "[%s] ", latest_timestamp);
      fprintf(stream, "<%s@%s> ", subsys, level);
      fputs(message, stream);
      if (suppressed)
         fprintf(stream, " [rate limit: %u earlier messages suppressed]", suppressed);
      fputc('\n', stream);
      fflush(stream);
   }
   free(message);
}

bool log_remove_callback(struct log_callback *log_callback) {
   struct log_callback *prev = NULL, *i = log_callbacks;
   while (i) {
      if (i == log_callback) {
         if (prev) {
            prev->next = i->next;
         } else {
            log_callbacks = i->next;
         }
         free(i);

         return true;           // signal success
      }
      prev = i;
      i = i->next;
   }
   return false;                // callback not found
}

struct log_callback *log_add_callback_token(bool (*log_va_cb)(logpriority_t, const char *, const char *, va_list),
   enum log_callback_policy policy) {
   if (policy != LOG_CALLBACK_RAW && policy != LOG_CALLBACK_FILTERED) return NULL;
   struct log_callback *newcb = malloc(sizeof(struct log_callback));

   if (!newcb) {
      fprintf(stderr, "OOM in log_set_callback!\n");

      return NULL;
   }
   memset(newcb, 0, sizeof(struct log_callback));
   newcb->callback = log_va_cb;
   newcb->policy = policy;

   if (log_callbacks) {
      // find the end
      struct log_callback *prev = NULL;
      struct log_callback *i = log_callbacks;

      while (i) {
         // continue the loop
         prev = i;
         i = i->next;
      }

      if (prev) {
         prev->next = newcb;
      }
   } else {
      // first entry, pop it at the top of the list
      log_callbacks = newcb;
   }

   return newcb;
}

bool log_add_callback(bool (*callback)(logpriority_t, const char *, const char *, va_list),
   enum log_callback_policy policy) {
   return log_add_callback_token(callback, policy) != NULL;
}
