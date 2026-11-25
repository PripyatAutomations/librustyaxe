/* SPDX-License-Identifier: MIT */
/* Feed real terminal bytes into the GLib stdin dispatcher, no display needed. */
#include <librustyui/tui.h>
#include <glib.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern void tui_keys_init(void),tui_keys_fini(void);
extern void tui_window_init(void),tui_window_fini(void);
extern char input_buf[];
extern int tui_input_len,tui_cursor_pos;
static int calls;
static char **provider(const char *line,const char *word){
   assert(!strcmp(line,"/he"));assert(!strcmp(word,"/he"));calls++;
   char **out=calloc(2,sizeof(*out));assert(out);
   out[0]=strdup("/help");assert(out[0]);return out;
}
static void send_keys(int fd,const char *bytes){
   size_t n=strlen(bytes);
   assert(write(fd,bytes,n)==(ssize_t)n);
   for(int i=0;i<32&&g_main_context_pending(NULL);i++)
      g_main_context_iteration(NULL,FALSE);
}
int main(void){
   int fds[2];assert(pipe(fds)==0);
   int saved=dup(STDIN_FILENO);assert(saved>=0);
   assert(dup2(fds[0],STDIN_FILENO)>=0);close(fds[0]);
   setenv("TERM","xterm-256color",1);
   tui_window_init();tui_keys_init();
   assert(tui_register_completion_provider(provider));
   send_keys(fds[1],"/he\t");
   assert(calls==1&&!strcmp(input_buf,"/help ")&&tui_cursor_pos==6);
   send_keys(fds[1],"\033[1~");
   assert(tui_cursor_pos==0);
   send_keys(fds[1],"\033[4~");
   assert(tui_cursor_pos==6);
   send_keys(fds[1],"\033[H");
   assert(tui_cursor_pos==0);
   send_keys(fds[1],"\033[F");
   assert(tui_cursor_pos==6);
   assert(tui_unregister_completion_provider(provider));
   tui_keys_fini();tui_window_fini();
   assert(dup2(saved,STDIN_FILENO)>=0);close(saved);close(fds[1]);
   puts("PASS: terminal byte dispatch, Tab completion and alternate Home/End");
   return 0;
}
