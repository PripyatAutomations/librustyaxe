//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Generic subprocess transport. Restart/watchdog policy belongs to its caller. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <librustyaxe/subproc.h>

/* Standalone subprocess transport helpers used by request/response children. */
static void subproc_reset(subproc_t *process) {
   if (!process) {
      return;
   }
   process->pid = -1;
   process->input = NULL;
   process->output = NULL;
   process->error_fd = -1;
   process->running = false;
}

void subproc_close(subproc_t *process) {
   if (!process) {
      return;
   }

   if (process->input) {
      fclose(process->input);
   }

   if (process->output) {
      fclose(process->output);
   }

   process->input = NULL;
   process->output = NULL;

   if (process->error_fd >= 0) {
      close(process->error_fd);
   }
   process->error_fd = -1;
}

static bool subproc_spawn_fds_impl(subproc_t *process, const char *path, const char *const argv[], const subproc_fd_t *mappings, size_t mapping_count, const int *close_fds,
   size_t close_count, bool group) {
   if (!process || process->pid > 0 || !path || !*path || !argv || !argv[0] ||
      (mapping_count && !mappings) || (close_count && !close_fds) || mapping_count > SIZE_MAX / sizeof(int)) {
      return false;
   }
   int first_fd = 3;

   for (size_t i = 0 ; i < mapping_count ; i++) {
      if (mappings[i].source < 0 || mappings[i].target < 0 || mappings[i].target == INT_MAX) {
         return false;
      }

      for (size_t j = 0 ; j < i ; j++) {
         if (mappings[i].target == mappings[j].target) {
            return false;
         }
      }

      if (mappings[i].target >= first_fd) {
         first_fd = mappings[i].target + 1;
      }
   }

   int *sources = mapping_count ? malloc(mapping_count * sizeof(*sources)) : NULL;

   if (mapping_count && !sources) {
      return false;
   }
   size_t duplicated = 0;

   for ( ; duplicated < mapping_count ; duplicated++) {
      sources[duplicated] = fcntl(mappings[duplicated].source, F_DUPFD_CLOEXEC, first_fd);

      if (sources[duplicated] < 0) {
         break;
      }
   }

   pid_t pid = duplicated == mapping_count ? fork() : -1;

   if (pid == 0) {
      if (group && setpgid(0, 0) < 0) _exit(126);
      for (size_t i = 0 ; i < mapping_count ; i++) {
         if (dup2(sources[i], mappings[i].target) < 0) {
            _exit(126);
         }
      }

      for (size_t i = 0 ; i < mapping_count ; i++) {
         close(sources[i]);
      }

      for (size_t i = 0 ; i < close_count ; i++) {
         bool target = false;

         for (size_t j = 0 ; j < mapping_count ; j++) {
            target |= close_fds[i] == mappings[j].target;
         }

         if (!target && close_fds[i] >= 0) {
            close(close_fds[i]);
         }
      }

      execv(path, (char *const *)argv);
      dprintf(STDERR_FILENO, "subproc: exec %s failed: %s\n", path, strerror(errno));
      _exit(127);
   }
   int saved_errno = errno;

   for (size_t i = 0 ; i < duplicated ; i++) {
      close(sources[i]);
   }

   free(sources);

   if (pid < 0) {
      errno = saved_errno;

      return false;
   }
   if (group) (void)setpgid(pid, pid); // Child also establishes the group before exec.
   subproc_reset(process);
   process->pid = pid;
   process->running = true;

   return true;
}

bool subproc_spawn_fds(subproc_t *process, const char *path, const char *const argv[], const subproc_fd_t *mappings, size_t mapping_count, const int *close_fds,
   size_t close_count) {
   return subproc_spawn_fds_impl(process, path, argv, mappings, mapping_count, close_fds, close_count, false);
}

static bool subproc_spawn_pipes(subproc_t *process, const char *path, const char *const argv[], bool merge_stderr, bool group) {

   if (!process || process->pid > 0 || !path || !*path || !argv || !argv[0]) {
      return false;
   }
   subproc_reset(process);

   int child_input[2] = {
      -1, -1
   };
   int child_output[2] = {
      -1, -1
   };
   int child_error[2] = {
      -1, -1
   };

   if (pipe(child_input) < 0 || pipe(child_output) < 0 || (!merge_stderr && pipe(child_error) < 0)) {
      if (child_input[0] >= 0) {
         close(child_input[0]);
      }

      if (child_input[1] >= 0) {
         close(child_input[1]);
      }

      if (child_output[0] >= 0) {
         close(child_output[0]);
      }

      if (child_output[1] >= 0) {
         close(child_output[1]);
      }

      if (child_error[0] >= 0) {
         close(child_error[0]);
      }

      if (child_error[1] >= 0) {
         close(child_error[1]);
      }

      return false;
   }

   subproc_fd_t mappings[] = {
      {
         child_input[0], STDIN_FILENO
      }, {
         child_output[1], STDOUT_FILENO
      },
      {
         merge_stderr ? child_output[1] : child_error[1], STDERR_FILENO
      }
   };
   int close_fds[] = {
      child_input[0], child_input[1], child_output[0], child_output[1], child_error[0], child_error[1]
   };
   bool prepared = true;

   for (size_t i = 0 ; i < 6 ; i++) {
      if (close_fds[i] >= 0 && fcntl(close_fds[i], F_SETFD, FD_CLOEXEC) < 0) {
         prepared = false;
         break;
      }
   }

   if (!prepared || !subproc_spawn_fds_impl(process, path, argv, mappings, 3, close_fds, 6, group)) {
      for (size_t i = 0 ; i < 6 ; i++) {
         if (close_fds[i] >= 0) {
            close(close_fds[i]);
         }
      }

      return false;
   }

   close(child_input[0]);
   close(child_output[1]);

   if (!merge_stderr) {
      close(child_error[1]);
   }
   process->input = fdopen(child_input[1], "w");
   process->output = fdopen(child_output[0], "r");
   process->error_fd = merge_stderr ? -1 : child_error[0];
   process->running = process->input && process->output;

   if (process->running) {
      /* Keep poll() and fgets() synchronized.  A buffered FILE can read several protocol lines at once, leaving later lines hidden from the next poll even
       * though they are already available to fgets(). */
      setvbuf(process->input, NULL, _IOLBF, 0);
      setvbuf(process->output, NULL, _IONBF, 0);
   }

   if (!process->running) {
      if (child_input[1] >= 0 && !process->input) {
         close(child_input[1]);
      }

      if (child_output[0] >= 0 && !process->output) {
         close(child_output[0]);
      }
      subproc_stop(process, SIGTERM);

      return false;
   }

   return true;
}

bool subproc_spawn(subproc_t *process, const char *path, const char *const argv[], bool merge_stderr) {
   return subproc_spawn_pipes(process, path, argv, merge_stderr, false);
}
bool subproc_spawn_group(subproc_t *process, const char *path, const char *const argv[], bool merge_stderr) {
   return subproc_spawn_pipes(process, path, argv, merge_stderr, true);
}

bool subproc_read_line(subproc_t *process, char *line, size_t length, int timeout_ms) {
   if (!process || !process->running || !process->output || !line || length < 2) {
      return false;
   }
   struct pollfd descriptor = {
      .fd = fileno(process->output), .events = POLLIN
   };
   int result;
   do {
      result = poll(&descriptor, 1, timeout_ms);
   } while (result < 0 && errno == EINTR);

   if (result <= 0 || !(descriptor.revents & (POLLIN | POLLHUP))) {
      return false;
   }

   return fgets(line, length, process->output) != NULL;
}

bool subproc_write_line(subproc_t *process, const char *line) {
   if (!process || !process->running || !process->input || !line) {
      return false;
   }

   return fprintf(process->input, "%s\n", line) >= 0 && fflush(process->input) == 0;
}

bool subproc_reap(subproc_t *process, int *status) {
   if (!process || process->pid <= 0) {
      return false;
   }

   int child_status = 0;
   pid_t result = waitpid(process->pid, &child_status, WNOHANG);

   if (result == 0) {
      return false;
   }

   if (result < 0) {
      if (errno == ECHILD) {
         process->pid = -1;
         process->running = false;
      }

      return false;
   }

   process->pid = -1;
   process->running = false;

   if (status) {
      *status = child_status;
   }

   return true;
}

bool subproc_stop(subproc_t *process, int signal_number) {
   if (!process) {
      return false;
   }

   subproc_close(process);

   if (process->pid <= 0) {
      subproc_reset(process);

      return true;
   }

   if (kill(process->pid, signal_number) < 0 && errno != ESRCH) {
      return false;
   }
   int status = 0;

   for (int i = 0 ; i < 20 ; i++) {
      pid_t result = waitpid(process->pid, &status, WNOHANG);

      if (result == process->pid || (result < 0 && errno == ECHILD)) {
         subproc_reset(process);

         return true;
      }

      if (result < 0 && errno != EINTR) {
         break;
      }
      struct timespec delay = {
         .tv_sec = 0, .tv_nsec = 50000000L
      };
      nanosleep(&delay, NULL);
   }

   if (kill(process->pid, SIGKILL) < 0 && errno != ESRCH) {
      return false;
   }

   while (waitpid(process->pid, &status, 0) < 0 && errno == EINTR) {
   }
   subproc_reset(process);

   return true;
}
