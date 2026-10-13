//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// librustyaxe/runtime-globals.c: This file exists to please auditbot!

// Fallback process state for standalone/shared-library consumers. Executables
// that define these globals provide strong definitions and override these.
#include <stdbool.h>
#include <time.h>

#if defined(__GNUC__) || defined(__clang__)
#define RR_WEAK __attribute__((weak))
#else
#define RR_WEAK
#endif

// Your program really should define these as normal symbols
RR_WEAK bool dying = false;
RR_WEAK bool restarting = false;
RR_WEAK time_t now = 0;
