/* SPDX-License-Identifier: MIT */
/* Legacy ANSI stream backend. Intentionally excluded from the normal build.
 * The renderer interface preserves the original direct terminal drawing path. */
#include "render.h"
#include <stdarg.h>
#include <unistd.h>
static bool enabled;
bool rrui_render_init(void){
   enabled=isatty(STDIN_FILENO)&&isatty(STDOUT_FILENO);
   if(enabled){fputs("\033[?1049h",stdout);fflush(stdout);}
   return enabled;
}
bool rrui_render_curses(void){return false;}
void rrui_render_resize(int rows,int cols){(void)rows;(void)cols;}
void rrui_render_begin(void){}
void rrui_render_end(void){rrui_render_flush();}
void rrui_render_flush(void){if(enabled)fflush(stdout);}
void rrui_render_fini(void){
   if(enabled){fputs("\033[0m\033[r\033[?25h\033[?1049l",stdout);fflush(stdout);}
   enabled=false;
}
void rrui_render_write(const char *s,size_t n){if(enabled&&s)fwrite(s,1,n,stdout);}
void rrui_render_printf(const char *fmt,...){
   if(!enabled)return;
   va_list ap;va_start(ap,fmt);vfprintf(stdout,fmt,ap);va_end(ap);
}
void rrui_render_char(int ch){if(enabled)fputc(ch,stdout);}
