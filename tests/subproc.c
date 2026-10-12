/* Exercise the existing subprocess transport through its consolidated API. */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <librustyaxe/subproc.h>
static const char *program;
static bool spawn(subproc_t *p, const char *mode, bool merge) {
   const char *args[] = { program, mode, NULL };
   return subproc_spawn(p, program, args, merge);
}
static int exited(subproc_t *p) {
   int status = -1;
   for (unsigned i = 0; i < 400; i++) {
      if (subproc_reap(p, &status)) {
         assert(!p->running && p->pid == -1);
         return status;
      }
      usleep(5000);
   }
   assert(!"child did not exit");
   return -1;
}
int main(int argc, char **argv) {
   program = argv[0];
   if (argc > 1) {
      if (!strcmp(argv[1], "--mapped")) {
         char packet[8];
         assert(read(3, packet, 8) == 8 && !memcmp(packet, "request!", 8));
         assert(write(3, "reply!!!", 8) == 8);
         assert(write(1, "output", 6) == 6);
         return 9;
      } else if (!strcmp(argv[1], "--echo")) {
         char line[256];
         while (fgets(line, sizeof(line), stdin)) {
            fputs(line, stdout);
            fflush(stdout);
         }
      } else if (!strcmp(argv[1], "--exit")) {
         return 7;
      } else if (!strcmp(argv[1], "--stderr")) {
         puts("stdout");
         fflush(stdout);
         fputs("stderr\n", stderr);
      } else if (!strcmp(argv[1], "--ignore")) {
         signal(SIGTERM, SIG_IGN);
         puts("ready");
         fflush(stdout);
         for (;;)
            pause();
      }
      return 0;
   }
   subproc_t p = SUBPROC_INIT;
   char line[256];
   int status;
   assert(!subproc_spawn(NULL, program, (const char *[]) { program, NULL }, true));
   assert(!subproc_spawn(&p, "", (const char *[]) { program, NULL }, true));
   assert(spawn(&p, "--echo", true));
   assert(!subproc_reap(&p, &status));
   assert(!subproc_read_line(&p, line, sizeof(line), 0));
   assert(subproc_write_line(&p, "first"));
   assert(subproc_write_line(&p, "second"));
   assert(subproc_read_line(&p, line, sizeof(line), 500) && !strcmp(line, "first\n"));
   assert(subproc_read_line(&p, line, sizeof(line), 500) && !strcmp(line, "second\n"));
   assert(subproc_stop(&p, SIGTERM));
   assert(subproc_stop(&p, SIGTERM));
   assert(!p.running && p.pid == -1 && !p.input && !p.output && p.error_fd == -1);
   assert(spawn(&p, "--stderr", false));
   assert(subproc_read_line(&p, line, sizeof(line), 500) && !strcmp(line, "stdout\n"));
   assert(read(p.error_fd, line, sizeof(line)) == 7 && !memcmp(line, "stderr\n", 7));
   status = exited(&p);
   assert(WIFEXITED(status) && !WEXITSTATUS(status));
   assert(subproc_stop(&p, SIGTERM));
   assert(spawn(&p, "--stderr", true));
   assert(subproc_read_line(&p, line, sizeof(line), 500) && !strcmp(line, "stdout\n"));
   assert(subproc_read_line(&p, line, sizeof(line), 500) && !strcmp(line, "stderr\n"));
   assert(p.error_fd == -1 && subproc_stop(&p, SIGTERM));
   assert(spawn(&p, "--exit", true));
   status = exited(&p);
   assert(WIFEXITED(status) && WEXITSTATUS(status) == 7);
   assert(subproc_stop(&p, SIGTERM));
   assert(spawn(&p, "--ignore", true));
   assert(subproc_read_line(&p, line, sizeof(line), 500));
   assert(subproc_stop(&p, SIGTERM));
   assert(spawn(&p, "--echo", true));
   subproc_close(&p);
   assert(!p.input && !p.output && p.error_fd == -1 && p.pid > 0);
   status = exited(&p);
   assert(WIFEXITED(status) && !WEXITSTATUS(status));
   assert(subproc_stop(&p, SIGTERM));
   // Deliberately swap the source descriptor numbers in the child. Sequential
   // dup2 without preserving sources would clobber the control socket.
   int control[2], output[2];
   assert(!socketpair(AF_UNIX, SOCK_SEQPACKET, 0, control));
   assert(!pipe(output));
   int control_source = fcntl(control[1], F_DUPFD_CLOEXEC, 10);
   int output_source = fcntl(output[1], F_DUPFD_CLOEXEC, 10);
   assert(control_source >= 10 && output_source > control_source);
   const char *mapped_args[] = { program, "--mapped", NULL };
   subproc_fd_t maps[] = { { control_source, 3 }, { output_source, 1 },
   { control_source, output_source }, { output_source, control_source }
   };
   int closed[] = { control[0], control[1], output[0], output[1], control_source, output_source };
   assert(subproc_spawn_fds(&p, program, mapped_args, maps, 4, closed, 6));
   assert(!p.input && !p.output && p.error_fd == -1);
   close(control[1]);
   close(output[1]);
   close(control_source);
   close(output_source);
   assert(write(control[0], "request!", 8) == 8);
   assert(read(control[0], line, sizeof(line)) == 8 && !memcmp(line, "reply!!!", 8));
   assert(read(output[0], line, sizeof(line)) == 6 && !memcmp(line, "output", 6));
   status = exited(&p);
   assert(WIFEXITED(status) && WEXITSTATUS(status) == 9);
   assert(subproc_stop(&p, SIGTERM));
   // Caller-owned descriptors survive lifecycle cleanup.
   assert(fcntl(control[0], F_GETFD) >= 0 && fcntl(output[0], F_GETFD) >= 0);
   close(control[0]);
   close(output[0]);
   maps[0] = (subproc_fd_t) {
   STDOUT_FILENO, 1};
   maps[1] = maps[0];
   assert(!subproc_spawn_fds(&p, program, mapped_args, maps, 2, NULL, 0));
   assert(!subproc_spawn_fds(&p, program, mapped_args, NULL, 1, NULL, 0));
   assert(spawn(&p, "--echo", true));
   pid_t owned = p.pid;
   assert(!spawn(&p, "--echo", true) && p.pid == owned);
   subproc_t other = SUBPROC_INIT;
   assert(subproc_spawn_fds(&other, program, (const char *[]) { program, "--exit", NULL }, NULL, 0, NULL, 0));
   status = exited(&other);
   assert(WIFEXITED(status) && WEXITSTATUS(status) == 7);
   assert(p.pid == owned && subproc_write_line(&p, "still owned"));
   assert(subproc_read_line(&p, line, sizeof(line), 500));
   assert(subproc_stop(&p, SIGTERM));
   puts
      ("PASS: common subprocess API, request/reply pipes, stderr modes, descriptor mappings, independent child ownership, exit status, shutdown escalation and handle reuse");
}
