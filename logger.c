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
   char msgbuf[16385];
   char ts_log_msg[1025];
   char log_msg[769];
   va_list ap, ap_c1;

   if (!subsys || !fmt) {
      fprintf(stderr, "Invalid Log request: No subsys/fmt\n");

      return;
   }
   va_start(ap, fmt);
   va_copy(ap_c1, ap);
   bool allowed = !debug_filter(subsys, priority);
   unsigned suppressed = 0;
   if (allowed) {
      const struct log_filter *filter = log_matching_filter(subsys);
      if (filter && !log_rate_allowed(subsys, priority,
         filter->rate_count, filter->rate_seconds, &suppressed)) allowed = false;
   }

   // Call registered callbacks even if this message is filtered from the
   // console/logfile below, e.g. for auditing; the callback can check
   // log_level itself if it only wants messages that would be shown
   if (log_callbacks) {
      struct log_callback *lp = log_callbacks;
      while (lp) {
         if (lp->callback && (!lp->policy == LOG_CALLBACK_RAW || allowed)) {
            va_list cb_ap;
            va_copy(cb_ap, ap_c1);
            lp->callback(priority, subsys, fmt, cb_ap);
            va_end(cb_ap);
         }
         lp = lp->next;
      }
   }

   if (!allowed) {
      va_end(ap_c1);
      va_end(ap);
      return;
   }

   // this is arranged so that it will return cached, if called more than once a second
   if (cfg_log_show_ts) {
      update_timestamp();
   }

   /* clear the message buffer */
   memset(msgbuf, 0, sizeof(msgbuf));

   /* Expand the format string */
   vsnprintf(msgbuf, sizeof(msgbuf) - 1, fmt, ap);
   if (suppressed) {
      size_t len = strlen(msgbuf);
      snprintf(msgbuf + len, sizeof(msgbuf) - len,
         " [rate limit: %u earlier messages suppressed]", suppressed);
   }
   memset(log_msg, 0, sizeof(log_msg));
   int log_prefix_len = snprintf(log_msg, sizeof(log_msg), "<%s@%s> ", subsys ? subsys : "core", log_priority_to_str(priority));

   if (log_prefix_len < 0) {
      log_msg[0] = '\0';
   } else if ((size_t)log_prefix_len < sizeof(log_msg)) {
      strlcpy(log_msg + log_prefix_len, msgbuf, sizeof(log_msg) - (size_t)log_prefix_len);
   }
   va_end(ap);

   if (logfp) {
      if (cfg_log_show_ts) {
         fprintf(logfp, "[%s] %s\n", latest_timestamp, log_msg);
      } else {
         fprintf(logfp, "%s\n", log_msg);
      }
      fflush(logfp);
   }

   if (!tui_mode_enabled) {
      /* Only spew to the console if logfile is closed or log.stdout == true, but avoid duplicating messages */
      if ((!logfp || log_stdout) && (logfp != stdout)) {
         if (cfg_log_show_ts) {
            fprintf(stdout, "[%s] %s\n", latest_timestamp, log_msg);
         } else {
            fprintf(stdout, "%s\n", log_msg);
         }
         fflush(stdout);
      }
   }
   va_end(ap_c1);
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
