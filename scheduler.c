//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "scheduler.h"
#include <errno.h>
#include <sched.h>
#include <sys/resource.h>
#include <string.h>
static int native_policy(ra_sched_class_t policy) {
   switch (policy) {
      case RA_SCHED_NORMAL: {
         return SCHED_OTHER;
      }
#ifdef SCHED_IDLE
      case RA_SCHED_IDLE: {
         return SCHED_IDLE;
      }
#endif
#ifdef SCHED_BATCH
      case RA_SCHED_BATCH: {
         return SCHED_BATCH;
      }
#endif
      case RA_SCHED_FIFO: {
         return SCHED_FIFO;
      }
      case RA_SCHED_RR: {
         return SCHED_RR;
      }
      default: {
         errno = ENOTSUP;

         return -1;
      }
   }
}
int ra_sched_apply(pid_t pid, const ra_sched_options_t *options) {
   if (!options || pid < 0) {
      errno = EINVAL;

      return -1;
   }
   int policy = native_policy(options->policy);

   if (policy < 0) {
      return -1;
   }
   int min = sched_get_priority_min(policy), max = sched_get_priority_max(policy);

   if (min < 0 || max < 0) {
      return -1;
   }

   if (options->priority < min || options->priority > max || (options->set_nice && (options->nice < -20 || options->nice > 19))) {
      errno = EINVAL;

      return -1;
   }
   struct sched_param param = {
      .sched_priority = options->priority
   };

   if (sched_setscheduler(pid, policy, &param) < 0) {
      return -1;
   }

   if (options->set_nice && setpriority(PRIO_PROCESS, (id_t) pid, options->nice) < 0) {
      return -1;                /* Policy may have changed; caller should query if needed. */
   }

   return 0;
}
int ra_sched_drop_rt(pid_t pid, int nice_value) {
   ra_sched_options_t options = {
      .policy = RA_SCHED_NORMAL, .priority = 0, .nice = nice_value,
      .set_nice = true
   };

   return ra_sched_apply(pid, &options);
}
int ra_sched_query(pid_t pid, ra_sched_options_t *options) {
   if (!options || pid < 0) {
      errno = EINVAL;

      return -1;
   }
   int policy = sched_getscheduler(pid);

   if (policy < 0) {
      return -1;
   }
   memset(options, 0, sizeof(*options));

   switch (policy) {
      case SCHED_OTHER: {
         options->policy = RA_SCHED_NORMAL;
         break;
      }
      case SCHED_FIFO: {
         options->policy = RA_SCHED_FIFO;
         break;
      }
      case SCHED_RR: {
         options->policy = RA_SCHED_RR;
         break;
      }
#ifdef SCHED_BATCH
      case SCHED_BATCH: {
         options->policy = RA_SCHED_BATCH;
         break;
      }
#endif
#ifdef SCHED_IDLE
      case SCHED_IDLE: {
         options->policy = RA_SCHED_IDLE;
         break;
      }
#endif
      default: {
         errno = ENOTSUP;

         return -1;
      }
   }
   struct sched_param param;

   if (sched_getparam(pid, &param) < 0) {
      return -1;
   }
   options->priority = param.sched_priority;
   errno = 0;
   options->nice = getpriority(PRIO_PROCESS, (id_t) pid);

   if (errno) {
      return -1;
   }
   options->set_nice = true;

   return 0;
}

const char *ra_sched_class_name(ra_sched_class_t policy) {
   switch (policy) {
      case RA_SCHED_NORMAL: {
         return "normal";
      }
      case RA_SCHED_BATCH: {
         return "batch";
      }
      case RA_SCHED_IDLE: {
         return "idle";
      }
      case RA_SCHED_FIFO: {
         return "fifo";
      }
      case RA_SCHED_RR: {
         return "rr";
      }
      default: {
         return "unknown";
      }
   }
}
int ra_sched_apply_profile(pid_t pid, const char *name) {
   if (!name || !*name || !strcmp(name, "normal")) {
      /* Avoid a redundant sched_setscheduler() on an ordinary worker:
       * container/LSM policy can reject even an unchanged scheduler. Only call the kernel if an inherited policy needs demotion. */
      int existing = sched_getscheduler(pid);

      if (existing < 0) {
         return -1;
      }

      if (existing == SCHED_OTHER) {
         return 0;
      }
      ra_sched_options_t p = {
         .policy = RA_SCHED_NORMAL
      };

      return ra_sched_apply(pid, &p);
   }
   ra_sched_options_t p = {
      0
   };

   if (!strcmp(name, "background")) {
      p.policy = RA_SCHED_BATCH;
      p.set_nice = true;
      p.nice = 5;
   } else if (!strcmp(name, "idle")) {
      p.policy = RA_SCHED_IDLE;
      p.set_nice = true;
      p.nice = 10;
   } else if (!strcmp(name, "interactive")) {
      p.policy = RA_SCHED_NORMAL;
      p.set_nice = true;
      p.nice = -2;
   } else if (!strcmp(name, "realtime-audio")) {
      p.policy = RA_SCHED_RR;
      p.priority = 10;
   } else {
      errno = EINVAL;

      return -1;
   }

   return ra_sched_apply(pid, &p);
}
