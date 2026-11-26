/* SPDX-License-Identifier: MIT */
#include <librustyui/canvas.h>
#include <glib.h>
#include <stdlib.h>
typedef struct {uint32_t glyph,rgb;} cell_t;
struct rrui_canvas {int width,height;cell_t *cells;};
rrui_canvas_t *rrui_canvas_new(int w,int h){
   if(w<1||h<1||w>4096||h>4096||(size_t)w*h>1048576)return NULL;
   rrui_canvas_t *c=g_new0(rrui_canvas_t,1);c->width=w;c->height=h;
   c->cells=g_new0(cell_t,(size_t)w*h);return c;
}
void rrui_canvas_free(rrui_canvas_t *c){if(c){g_free(c->cells);g_free(c);}}
int rrui_canvas_width(const rrui_canvas_t *c){return c?c->width:0;}
int rrui_canvas_height(const rrui_canvas_t *c){return c?c->height:0;}
static cell_t *cell(const rrui_canvas_t *c,int x,int y){
   return c&&x>=0&&y>=0&&x<c->width&&y<c->height?&c->cells[(size_t)y*c->width+x]:NULL;
}
void rrui_canvas_put(rrui_canvas_t *c,int x,int y,uint32_t glyph,uint32_t rgb){
   cell_t *p=cell(c,x,y);if(!p)return;
   if(!g_unichar_validate(glyph)||g_unichar_iscntrl(glyph)||g_unichar_iswide(glyph)||g_unichar_iszerowidth(glyph)||g_unichar_type(glyph)==G_UNICODE_FORMAT)glyph='?';
   *p=(cell_t){glyph,rgb};
}
uint32_t rrui_canvas_get(const rrui_canvas_t *c,int x,int y){cell_t *p=cell(c,x,y);return p&&p->glyph?p->glyph:' ';}
uint32_t rrui_canvas_color(const rrui_canvas_t *c,int x,int y){cell_t *p=cell(c,x,y);return p&&p->glyph?p->rgb:UINT32_MAX;}
void rrui_canvas_text(rrui_canvas_t *c,int x,int y,int width,const char *s,uint32_t rgb){
   if(!s||width<=0)return;
   while(*s&&width--){
      gunichar ch=g_utf8_get_char_validated(s,-1);
      if(ch==(gunichar)-1||ch==(gunichar)-2){ch='?';s++;}else s=g_utf8_next_char(s);
      rrui_canvas_put(c,x++,y,ch,rgb);
   }
}
void rrui_canvas_line(rrui_canvas_t *c,int x1,int y1,int x2,int y2,uint32_t rgb){
   if(!c)return;
   if(y1==y2){for(int x=MAX(0,MIN(x1,x2));x<=MIN(c->width-1,MAX(x1,x2));x++)rrui_canvas_put(c,x,y1,rrui_canvas_get(c,x,y1)==0x2502?0x253c:0x2500,rgb);}
   else if(x1==x2){for(int y=MAX(0,MIN(y1,y2));y<=MIN(c->height-1,MAX(y1,y2));y++)rrui_canvas_put(c,x1,y,rrui_canvas_get(c,x1,y)==0x2500?0x253c:0x2502,rgb);}
}
void rrui_canvas_box(rrui_canvas_t *c,int x,int y,int w,int h,uint32_t rgb){
   if(w<2||h<2||w>4096||h>4096)return;
   for(int j=0;j<h;j++)for(int i=0;i<w;i++)rrui_canvas_put(c,x+i,y+j,' ',rgb);
   rrui_canvas_line(c,x,y,x+w-1,y,rgb);rrui_canvas_line(c,x,y+h-1,x+w-1,y+h-1,rgb);
   rrui_canvas_line(c,x,y,x,y+h-1,rgb);rrui_canvas_line(c,x+w-1,y,x+w-1,y+h-1,rgb);
   rrui_canvas_put(c,x,y,0x250c,rgb);rrui_canvas_put(c,x+w-1,y,0x2510,rgb);
   rrui_canvas_put(c,x,y+h-1,0x2514,rgb);rrui_canvas_put(c,x+w-1,y+h-1,0x2518,rgb);
}
