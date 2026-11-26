/* SPDX-License-Identifier: MIT */
#ifndef RUSTYUI_UI_H
#define RUSTYUI_UI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct rrui_layout rrui_layout_t;
typedef enum {
   RRUI_TUI, RRUI_GTK3
} rrui_backend_t;
typedef enum {
   RRUI_DIALOG, RRUI_PANEL, RRUI_ROW, RRUI_COLUMN, RRUI_LABEL,
   RRUI_ENTRY, RRUI_CHECKBOX, RRUI_BUTTON, RRUI_LIST, RRUI_STEPPER, RRUI_SLIDER
} rrui_kind_t;

typedef enum { RRUI_PACK_VERTICAL, RRUI_PACK_HORIZONTAL, RRUI_PACK_FLOW, RRUI_PACK_GRID, RRUI_PACK_FIXED } rrui_pack_mode_t;
typedef struct {
   unsigned padding, column, row;
   int x, y;
   bool expand, fill;
} rrui_pack_options_t;

/* Strings are owned by the layout and valid until replacement/free. */
typedef struct {
   rrui_kind_t kind;
   const char *id, *parent, *label, *action, *value;
   unsigned width_chars, height_rows, max_length, spacing;
   int minimum, maximum, step;
   bool vertical;
   rrui_pack_mode_t layout;
   unsigned columns;
   rrui_pack_options_t pack;
   bool visible, enabled, checked, expand_x, expand_y, masked;
} rrui_widget_t;

rrui_layout_t *rrui_layout_new(rrui_backend_t backend);
void rrui_layout_free(rrui_layout_t *layout);
/* Evaluate layout-only RustyScript in a fresh VM. Failure preserves the
 * previous model. On success the old widget pointers become invalid.
 * All operations belong to the application's UI thread. */
bool rrui_layout_load(rrui_layout_t *layout, const char *source);
const char *rrui_layout_error(const rrui_layout_t *layout);
bool rrui_capability(rrui_backend_t backend, const char *name);
size_t rrui_layout_count(const rrui_layout_t *layout);
const rrui_widget_t *rrui_layout_at(const rrui_layout_t *layout, size_t index);
const rrui_widget_t *rrui_layout_find(const rrui_layout_t *layout, const char *id);
#endif
