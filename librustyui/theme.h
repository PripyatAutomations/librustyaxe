/* SPDX-License-Identifier: MIT */
#ifndef RUSTYUI_THEME_H
#define RUSTYUI_THEME_H
#include <stdbool.h>
#include <stdint.h>
typedef enum {
   RRUI_THEME_WORKSPACE, RRUI_THEME_PANEL, RRUI_THEME_HEADER,
   RRUI_THEME_BORDER, RRUI_THEME_FOCUS, RRUI_THEME_TEXT,
   RRUI_THEME_ACCENT, RRUI_THEME_INPUT, RRUI_THEME_SELECTION,
   RRUI_THEME_SUCCESS, RRUI_THEME_WARNING, RRUI_THEME_TX,
   RRUI_THEME_ACTIVE_HEADER, RRUI_THEME_COUNT
} rrui_theme_role_t;
typedef struct { uint32_t rgb[RRUI_THEME_COUNT]; } rrui_theme_t;
void rrui_theme_default(rrui_theme_t *);
const char *rrui_theme_name(rrui_theme_role_t);
/* Parse a GLib keyfile [theme] section. Unknown keys ignored. Invalid
 * known values reject the entire update; unspecified roles retain defaults. */
bool rrui_theme_load(rrui_theme_t *,const char *path);
void rrui_tui_theme_set(const rrui_theme_t *);
void rrui_tui_theme_style(rrui_theme_role_t foreground,rrui_theme_role_t background);
void rrui_tui_theme_rgb(uint32_t,bool background);
#endif
