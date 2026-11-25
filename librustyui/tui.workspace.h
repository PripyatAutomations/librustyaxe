/* SPDX-License-Identifier: MIT */
#ifndef RUSTYUI_WORKSPACE_H
#define RUSTYUI_WORKSPACE_H
#include <librustyui/form.h>
typedef struct {int x,y,width,height;} rrui_rect_t;
/* Terminal cells, 1-based. Return nearest rectangle in a directional half
 * plane, or SIZE_MAX. Direction components must be -1, 0 or 1. */
size_t rrui_workspace_nearest(const rrui_rect_t *,size_t,size_t,int,int);
bool rrui_tui_panel_open(rrui_form_t *,rrui_rect_t);
bool rrui_tui_panel_geometry(rrui_form_t *,rrui_rect_t);
void rrui_tui_focus_chat(void);
/* Ordered side-column tiles: reserve chat, hide trailing tiles when space
 * is insufficient. Returns chat width; invisible rectangles are zeroed. */
int rrui_workspace_tiles(int rows,int cols,int width,const int *minimum,
                         const int *weight,size_t n,rrui_rect_t *out);
bool rrui_tui_tile_open(rrui_form_t *,int minimum_height,int weight);
void rrui_tui_tile_width(int width);
int rrui_tui_chat_width(int rows,int cols,bool structured);
bool rrui_tui_panel_focused(const rrui_form_t *);
#endif
