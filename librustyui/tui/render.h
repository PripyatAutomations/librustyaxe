/* SPDX-License-Identifier: MIT */
#ifndef RRUI_RENDER_H
#define RRUI_RENDER_H
#include <stdbool.h>
#include <stdio.h>
bool rrui_render_init(void);
void rrui_render_fini(void);
bool rrui_render_curses(void);
void rrui_render_resize(int rows,int cols);
void rrui_render_begin(void);
void rrui_render_end(void);
void rrui_render_flush(void);
void rrui_render_write(const char *,size_t);
void rrui_render_printf(const char *,...) __attribute__((format(printf,1,2)));
void rrui_render_char(int);
#endif
