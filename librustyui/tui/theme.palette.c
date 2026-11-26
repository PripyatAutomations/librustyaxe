#include "render.h"
/* SPDX-License-Identifier: MIT */
#include <librustyui/theme.h>
#include <librustyui/tui.theme.h>
#include <glib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
static rrui_theme_t theme;
static bool initialized;
void rrui_tui_theme_set(const rrui_theme_t *t){if(t)theme=*t;else rrui_theme_default(&theme);initialized=true;}
void rrui_tui_theme_rgb(uint32_t rgb,bool bg){
   if(!cfg_tui_colors||rgb>0xffffff)return;
   unsigned r=rgb>>16,g=(rgb>>8)&255,b=rgb&255;
   /* The renderer already knows the actual terminal color capability.
    * Do not quantize here using possibly incomplete environment hints. */
   rrui_render_printf("\033[%d;2;%u;%u;%um",bg?48:38,r,g,b);
}
void rrui_tui_theme_style(rrui_theme_role_t fg,rrui_theme_role_t bg){
   if(!initialized)rrui_tui_theme_set(NULL);
   rrui_render_write("\033[0m",strlen("\033[0m"));
   if((unsigned)fg<RRUI_THEME_COUNT)rrui_tui_theme_rgb(theme.rgb[fg],false);
   if((unsigned)bg<RRUI_THEME_COUNT)rrui_tui_theme_rgb(theme.rgb[bg],true);
}

void rrui_tui_theme_role(rrui_theme_role_t role,bool bg){
   if(!initialized)rrui_tui_theme_set(NULL);
   if((unsigned)role<RRUI_THEME_COUNT)rrui_tui_theme_rgb(theme.rgb[role],bg);
}
