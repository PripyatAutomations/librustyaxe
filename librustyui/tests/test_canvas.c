/* SPDX-License-Identifier: MIT */
#include <librustyui/canvas.h>
#include <assert.h>
#include <stdio.h>
int main(void){
   rrui_canvas_t *c=rrui_canvas_new(20,10);assert(c);
   rrui_canvas_line(c,-5,3,30,3,0xff00ff);
   assert(rrui_canvas_get(c,0,3)==0x2500);
   rrui_canvas_line(c,5,-2,5,12,0x00ffff);
   assert(rrui_canvas_get(c,5,3)==0x253c);
   rrui_canvas_box(c,2,2,8,5,0xffffff);
   assert(rrui_canvas_get(c,2,2)==0x250c);
   assert(rrui_canvas_get(c,5,3)==' ');
   rrui_canvas_text(c,3,3,4,"Hello",0x123456);
   assert(rrui_canvas_get(c,6,3)=='l'&&rrui_canvas_color(c,6,3)==0x123456);
   rrui_canvas_put(c,0,0,27,0);assert(rrui_canvas_get(c,0,0)=='?');
   rrui_canvas_free(c);puts("PASS: clipped canvas, box drawing, junctions and text");return 0;
}
