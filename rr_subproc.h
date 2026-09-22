// Generic RustyRig subprocess transport and lifecycle helpers.
#if !defined(_rr_subproc_h)
#define _rr_subproc_h

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

typedef struct rr_subproc rr_subproc_t;

struct rr_subproc {
   pid_t pid;
   FILE *input;
   FILE *output;
   int error_fd;
   bool running;
};

bool rr_subproc_spawn(rr_subproc_t *process, const char *path,
   const char *const argv[], bool merge_stderr);
bool rr_subproc_readline(rr_subproc_t *process, char *line, size_t length,
   int timeout_ms);
bool rr_subproc_write_line(rr_subproc_t *process, const char *line);
bool rr_subproc_reap(rr_subproc_t *process, int *status);
bool rr_subproc_stop(rr_subproc_t *process, int signal_number);
void rr_subproc_close(rr_subproc_t *process);

#endif
