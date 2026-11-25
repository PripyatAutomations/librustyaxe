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
#endif
