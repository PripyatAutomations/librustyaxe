// Generic RustyRig subprocess transport and lifecycle helpers.
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <librustyaxe/core.h>
#include <librustyaxe/rr_subproc.h>

static void rr_subproc_reset(rr_subproc_t *process) {
   if (!process) {
      return;
   }
   process->pid = -1;
   process->input = NULL;
   process->output = NULL;
   process->error_fd = -1;
   process->running = false;
}

void rr_subproc_close(rr_subproc_t *process) {
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

bool rr_subproc_spawn(rr_subproc_t *process, const char *path,
   const char *const argv[], bool merge_stderr) {

   if (!process || !path || !*path || !argv || !argv[0]) {
      return false;
   }
   rr_subproc_reset(process);

   int child_input[2] = { -1, -1 };
   int child_output[2] = { -1, -1 };
   int child_error[2] = { -1, -1 };
   if (pipe(child_input) < 0 || pipe(child_output) < 0 || (!merge_stderr && pipe(child_error) < 0)) {
      if (child_input[0] >= 0) { 
         close(child_input[0]); close(child_input[1]);
      }

      if (child_output[0] >= 0) {
         close(child_output[0]); close(child_output[1]); }
      }

      if (child_error[0] >= 0) {
         close(child_error[0]); close(child_error[1]);
      }
      return false;
   }

   pid_t pid = fork();
   if (pid < 0) {
      close(child_input[0]); close(child_input[1]);
      close(child_output[0]); close(child_output[1]);
      if (!merge_stderr) {
         close(child_error[0]); close(child_error[1]);
      }
      return false;
   }
   if (pid == 0) {
      if (dup2(child_input[0], STDIN_FILENO) < 0 ||
          dup2(child_output[1], STDOUT_FILENO) < 0 ||
          (merge_stderr ? dup2(child_output[1], STDERR_FILENO) < 0 :
           dup2(child_error[1], STDERR_FILENO) < 0)) {
         _exit(126);
      }
      close(child_input[0]); close(child_input[1]);
      close(child_output[0]); close(child_output[1]);
      if (!merge_stderr) {
         close(child_error[0]); close(child_error[1]);
      }
      execv(path, (char *const *)argv);
      dprintf(STDERR_FILENO, "rr_subproc: exec %s failed: %s\n", path, strerror(errno));
      _exit(127);
   }

   close(child_input[0]);
   close(child_output[1]);

   if (!merge_stderr) {
      close(child_error[1]);
   }
   process->pid = pid;
   process->input = fdopen(child_input[1], "w");
   process->output = fdopen(child_output[0], "r");
   process->error_fd = merge_stderr ? -1 : child_error[0];
   process->running = process->input && process->output;
   if (process->running) {
      /* Keep poll() and fgets() synchronized.  A buffered FILE can read
       * several protocol lines at once, leaving later lines hidden from the
       * next poll even though they are already available to fgets(). */
      setvbuf(process->input, NULL, _IOLBF, 0);
      setvbuf(process->output, NULL, _IONBF, 0);
   }
   if (!process->running) {
      if (child_input[1] >= 0 && !process->input) close(child_input[1]);
      if (child_output[0] >= 0 && !process->output) close(child_output[0]);
      rr_subproc_stop(process, SIGTERM);
      return false;
   }
   return true;
}

bool rr_subproc_readline(rr_subproc_t *process, char *line, size_t length,
   int timeout_ms) {
   if (!process || !process->running || !process->output || !line || length < 2) {
      return false;
   }
   struct pollfd descriptor = { .fd = fileno(process->output), .events = POLLIN };
   int result;
   do {
      result = poll(&descriptor, 1, timeout_ms);
   } while (result < 0 && errno == EINTR);

   if (result <= 0 || !(descriptor.revents & (POLLIN | POLLHUP))) {
      return false;
   }
   return fgets(line, length, process->output) != NULL;
}

bool rr_subproc_write_line(rr_subproc_t *process, const char *line) {
   if (!process || !process->running || !process->input || !line) {
      return false;
   }
   return fprintf(process->input, "%s\n", line) >= 0 && fflush(process->input) == 0;
}

bool rr_subproc_reap(rr_subproc_t *process, int *status) {
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
         process->running = false;
      }
      return false;
   }

   process->running = false;
   if (status) {
      *status = child_status;
   }
   return true;
}

bool rr_subproc_stop(rr_subproc_t *process, int signal_number) {
   if (!process) {
      return false;
   }

   rr_subproc_close(process);

   if (process->pid <= 0) {
      rr_subproc_reset(process);
      return true;
   }

   if (kill(process->pid, signal_number) < 0 && errno != ESRCH) {
      return false;
   }
   int status = 0;
   for (int i = 0; i < 20; i++) {
      pid_t result = waitpid(process->pid, &status, WNOHANG);
      if (result == process->pid || (result < 0 && errno == ECHILD)) {
         rr_subproc_reset(process);
         return true;
      }

      if (result < 0 && errno != EINTR) {
         break;
      }
      struct timespec delay = { .tv_sec = 0, .tv_nsec = 50000000L };
      nanosleep(&delay, NULL);
   }

   if (kill(process->pid, SIGKILL) < 0 && errno != ESRCH) {
      return false;
   }

   while (waitpid(process->pid, &status, 0) < 0 && errno == EINTR) { }
   rr_subproc_reset(process);
   return true;
}
