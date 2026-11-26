/* SPDX-License-Identifier: MIT */
#ifndef RRUI_CANVAS_H
#define RRUI_CANVAS_H
#include <stdint.h>
#include <stdbool.h>
typedef struct rrui_canvas rrui_canvas_t;
rrui_canvas_t *rrui_canvas_new(int width,int height);
void rrui_canvas_free(rrui_canvas_t *);
void rrui_canvas_put(rrui_canvas_t *,int x,int y,uint32_t glyph,uint32_t rgb);
uint32_t rrui_canvas_get(const rrui_canvas_t *,int x,int y);
void rrui_canvas_text(rrui_canvas_t *,int x,int y,int width,const char *,uint32_t rgb);
void rrui_canvas_line(rrui_canvas_t *,int x1,int y1,int x2,int y2,uint32_t rgb);
void rrui_canvas_box(rrui_canvas_t *,int x,int y,int width,int height,uint32_t rgb);
void rrui_canvas_draw(const rrui_canvas_t *,int row,int col);
int rrui_canvas_width(const rrui_canvas_t *);
int rrui_canvas_height(const rrui_canvas_t *);
uint32_t rrui_canvas_color(const rrui_canvas_t *,int x,int y);
#endif
