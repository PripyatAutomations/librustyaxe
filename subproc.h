/* Generic subprocess lifecycle with line pipes or caller-owned descriptors. */
#ifndef LIBRUSTYAXE_SUBPROC_H
#define LIBRUSTYAXE_SUBPROC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

typedef struct subproc subproc_t;
struct subproc {
   pid_t pid;
   FILE *input;
   FILE *output;
   int error_fd;
   bool running;
};
#define SUBPROC_INIT { .pid = -1, .error_fd = -1 }

/* argv includes argv[0] and a final NULL. No shell or PATH lookup is performed.
 * Initialize with SUBPROC_INIT. Stop an existing child before reusing its handle.
 * With merge_stderr=false, the caller must drain error_fd separately.
 * Success means pipes and child were created; exec failure is child status 127. */
bool subproc_spawn(subproc_t * process, const char *path, const char *const argv[], bool merge_stderr);
typedef struct subproc_fd {
   int source;
   int target;
} subproc_fd_t;
/* Fork/exec with explicit descriptor mappings (pipes, socketpairs, files, etc.).
 * Sources may overlap targets; all mappings are applied without clobbering sources.
 * close_fds are closed in the child only, except mapped targets. Other descriptors
 * retain their normal exec inheritance. Set FD_CLOEXEC on unrelated descriptors.
 * Parent descriptors remain caller-owned; input/output/error_fd stay unset.
 * NULL arrays are valid only with zero counts. Targets must be distinct.
 * Lifecycle and exec failure semantics are the same as subproc_spawn(). */
bool subproc_spawn_fds(subproc_t * process, const char *path, const char *const argv[],
   const subproc_fd_t * mappings, size_t mapping_count, const int *close_fds, size_t close_count);
/* timeout_ms waits for initial output: 0 probes, -1 waits indefinitely.
 * Like fgets(), the helper must produce complete lines and capacity may split them. */
bool subproc_read_line(subproc_t * process, char *line, size_t length, int timeout_ms);
/* Adds one newline and flushes. The host owns its SIGPIPE handling policy. */
bool subproc_write_line(subproc_t * process, const char *line);
/* Nonblocking wait for this child; true supplies a waitpid status if requested.
 * Clears pid after reaping so stop cannot signal a recycled PID.
 * False means no exit was collected (including waitpid failure). */
bool subproc_reap(subproc_t * process, int *status);
/* Close pipes, signal and reap, escalating to SIGKILL after about one second.
 * Calling stop again on an idle handle is safe. */
bool subproc_stop(subproc_t * process, int signal_number);
/* Close pipes only; the caller still owns any unreaped child. */
void subproc_close(subproc_t * process);
#endif
