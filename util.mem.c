//
// util.mem.c: allocation wrappers. Allocation failure means OOM and is fatal.
//
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librustyaxe/util.mem.h>

void oom_fatal(const char *what) {
   Log(LOG_CRIT, "mem", "out of memory%s%s", (what ? ": " : ""), (what ? what : ""));
   exit(ENOMEM);
}

void *xmalloc(size_t size) {
   void *p = malloc(size);

   if (!p) {
      oom_fatal("malloc");
   }
   return p;
}

void *xcalloc(size_t nmemb, size_t size) {
   void *p = calloc(nmemb, size);

   if (!p) {
      oom_fatal("calloc");
   }
   return p;
}

void *xrealloc(void *ptr, size_t size) {
   void *p = realloc(ptr, size);

   if (!p) {
      oom_fatal("realloc");
   }
   return p;
}

char *xstrdup(const char *s) {
   char *p = strdup(s);

   if (!p) {
      oom_fatal("strdup");
   }
   return p;
}
