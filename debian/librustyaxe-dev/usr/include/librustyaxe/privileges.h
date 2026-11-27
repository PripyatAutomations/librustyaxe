//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Linux-only, irreversible child-side privilege lockdown. Intended after scheduling policy setup and immediately before exec. Does not modify the parent or
 * other threads. */
#ifndef LIBRUSTYAXE_PRIVILEGES_H
#define LIBRUSTYAXE_PRIVILEGES_H
int ra_privileges_restrict_exec(void);
#endif
