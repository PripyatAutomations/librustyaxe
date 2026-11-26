/* SPDX-License-Identifier: MIT */
#ifndef RUSTYUI_FORM_H
#define RUSTYUI_FORM_H
#include <librustyui/ui.h>
struct tui_window;
typedef struct rrui_form rrui_form_t;
typedef void (*rrui_action_fn)(rrui_form_t *,const char *id,const char *action,void *);
typedef struct {
   const rrui_widget_t *widget;
   const char *value;
   bool checked, interactive, visible;
   size_t cursor, count, selected;
} rrui_field_t;
/* Form owns a snapshot of the layout. Call on the UI thread. */
rrui_form_t *rrui_form_new(const rrui_layout_t *,rrui_action_fn,void *);
void rrui_form_free(rrui_form_t *);
size_t rrui_form_count(const rrui_form_t *);
bool rrui_form_field(const rrui_form_t *,size_t,rrui_field_t *);
size_t rrui_form_focus(const rrui_form_t *);
bool rrui_form_set_focus(rrui_form_t *,size_t);
bool rrui_form_next(rrui_form_t *,int direction);
const char *rrui_form_value(const rrui_form_t *,const char *id);
/* Set an entry or dynamic display label. */
bool rrui_form_set_value(rrui_form_t *,const char *id,const char *value);
/* A stepper has an integer value or an empty (unavailable) value.
 * Host updates do not invoke actions. User adjustments do. */
bool rrui_form_adjust(rrui_form_t *,int direction);
/* Change slider orientation without rebuilding or changing its value. */
bool rrui_form_set_orientation(rrui_form_t *,const char *id,bool vertical);
/* User gesture: select a position in [0,span], snapped to the step. */
bool rrui_form_slider_position(rrui_form_t *,unsigned position,unsigned span);
bool rrui_form_checked(const rrui_form_t *,const char *id);
bool rrui_form_set_checked(rrui_form_t *,const char *id,bool);
bool rrui_form_list_clear(rrui_form_t *,const char *id);
bool rrui_form_list_append(rrui_form_t *,const char *id,const char *key,const char *label);
/* RGB foreground, 0xRRGGBB; UINT32_MAX uses theme default. */
bool rrui_form_list_append_color(rrui_form_t *,const char *,const char *,const char *,uint32_t);
/* Nonselectable section heading; included in viewport row indices. */
bool rrui_form_list_header(rrui_form_t *,const char *id,const char *key,const char *label);
bool rrui_form_list_selectable(const rrui_form_t *,size_t field,size_t item);
bool rrui_form_list_move(rrui_form_t *,size_t field,int direction,size_t steps,bool wrap);
uint32_t rrui_form_list_color(const rrui_form_t *,size_t,size_t);
const char *rrui_form_list_label(const rrui_form_t *,size_t field,size_t item);
bool rrui_form_select(rrui_form_t *,size_t field,size_t item);
size_t rrui_form_list_view(rrui_form_t *,size_t field,size_t span,bool follow_selection);
bool rrui_form_list_scroll(rrui_form_t *,size_t field,size_t span,int delta);
/* Entry edits use Unicode code points and byte-offset cursor positions. */
bool rrui_form_insert(rrui_form_t *,uint32_t codepoint);
bool rrui_form_edit(rrui_form_t *,int direction,bool erase);
bool rrui_form_move_word(rrui_form_t *,int direction);
bool rrui_form_activate(rrui_form_t *);
void rrui_form_cancel(rrui_form_t *);
/* Nonblocking modal terminal form; caller retains ownership and must close
 * before freeing. Actions may close the form. */
bool rrui_tui_open(rrui_form_t *);
bool rrui_tui_modal_active(void);
void rrui_tui_close(rrui_form_t *);
/* Borrowed form attached to a normal switchable tab. Detach before freeing. */
bool rrui_tui_tab_attach(struct tui_window *,rrui_form_t *);
void rrui_tui_tab_detach(struct tui_window *);
#endif
