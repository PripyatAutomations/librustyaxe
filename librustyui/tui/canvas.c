/* SPDX-License-Identifier: MIT */
#include <librustyui/canvas.h>
#include <librustyui/theme.h>
#include "render.h"
#include <glib.h>
void rrui_canvas_draw(const rrui_canvas_t *c,int row,int col){
   for(int y=0;y<rrui_canvas_height(c);y++){
      rrui_render_printf("\033[%d;%dH",row+y,col);
      uint32_t previous=UINT32_MAX,previous_bg=UINT32_MAX;
      rrui_tui_theme_style(RRUI_THEME_TEXT,RRUI_THEME_WORKSPACE);
      for(int x=0;x<rrui_canvas_width(c);x++){
         uint32_t rgb=rrui_canvas_color(c,x,y);
         uint32_t bg=rrui_canvas_background(c,x,y);
         if (rgb!=previous || bg!=previous_bg) {
            rrui_tui_theme_style(RRUI_THEME_TEXT,RRUI_THEME_WORKSPACE);
            if (rgb<=0xffffff) { rrui_tui_theme_rgb(rgb,false); }
            if (bg<=0xffffff) { rrui_tui_theme_rgb(bg,true); }
            previous=rgb;previous_bg=bg;
         }
         char utf[7];int n=g_unichar_to_utf8(rrui_canvas_get(c,x,y),utf);
         rrui_render_write(utf,n);
      }
   }
}
