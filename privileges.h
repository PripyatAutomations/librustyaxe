/* Linux-only, irreversible child-side privilege lockdown.
 * Intended after scheduling policy setup and immediately before exec.
 * Does not modify the parent or other threads. */
#ifndef LIBRUSTYAXE_PRIVILEGES_H
#define LIBRUSTYAXE_PRIVILEGES_H
int ra_privileges_restrict_exec(void);
#endif
