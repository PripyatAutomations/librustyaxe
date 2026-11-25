/* SPDX-License-Identifier: MIT */
#include <librustyui/form.h>
#include <librustyui/tui.workspace.h>
#include <librustyui/tui.h>
#include "termkey.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>

static rrui_form_t *active;
static size_t scroll;
typedef struct {size_t field,item;int row;} hit_t;
static GArray *hits;
typedef struct {rrui_form_t *form;rrui_rect_t requested,rect;size_t scroll;GArray *hits;} panel_t;
static panel_t panels[32];
static size_t npanels;
static rrui_form_t *focused;
static int ox=1,oy=0;
static bool overlay;
static int workspace_rows,workspace_cols;
static rrui_form_t *dragging;
static int drag_x,drag_y;
static bool resizing;
static size_t modal_scroll;
static GArray *modal_hits;
static size_t panel_index(rrui_form_t *f) {for(size_t i=0;i<npanels;i++)if(panels[i].form==f)return i;return SIZE_MAX;}
static void raise_panel(size_t i) {if(i>=npanels)return;panel_t p=panels[i];memmove(panels+i,panels+i+1,(npanels-i-1)*sizeof(p));panels[npanels-1]=p;focused=p.form;}
bool rrui_tui_panel_geometry(rrui_form_t *f,rrui_rect_t r) {size_t i=panel_index(f);if(i==SIZE_MAX||r.width<12||r.height<4)return false;panels[i].requested=r;tui_redraw_request();return true;}
bool rrui_tui_panel_open(rrui_form_t *f,rrui_rect_t r) {if(!f||!tui_is_enabled||active==f||panel_index(f)!=SIZE_MAX||npanels>=32||r.width<12||r.height<4)return false;panels[npanels++]=(panel_t){.form=f,.requested=r,.rect=r};tui_redraw_request();return true;}
void rrui_tui_focus_chat(void){focused=NULL;tui_redraw_request();}
bool rrui_tui_modal_active(void){return active!=NULL;}


bool rrui_tui_open(rrui_form_t *f) {
   if(!f||!tui_is_enabled||active||panel_index(f)!=SIZE_MAX)return false;
   active=f;modal_scroll=0;tui_redraw_request();return true;
}
void rrui_tui_close(rrui_form_t *f) {
   if(active&&(!f||active==f)){active=NULL;if(modal_hits){g_array_unref(modal_hits);modal_hits=NULL;}tui_redraw_request();}
   for(size_t i=0;i<npanels;) {if(!f||panels[i].form==f){if(focused==panels[i].form)focused=NULL;if(dragging==panels[i].form)dragging=NULL;if(panels[i].hits)g_array_unref(panels[i].hits);memmove(panels+i,panels+i+1,(npanels-i-1)*sizeof(panel_t));npanels--;tui_redraw_request();}else i++;}
}
/* Print plain terminal text only. Count wide characters conservatively. */
static int clipped(const char *s,int width) {
   int initial=width;
   if(!s)return 0;
   for(const char *p=s;*p&&width>0;) {
      gunichar c=g_utf8_get_char_validated(p,-1);
      if(c==(gunichar)-1||c==(gunichar)-2){putchar('?');p++;width--;continue;}
      const char *q=g_utf8_next_char(p);
      int cells=g_unichar_iswide(c)?2:1;
      if(cells>width)break;
      if(g_unichar_iscntrl(c)||g_unichar_type(c)==G_UNICODE_FORMAT)putchar('?');
      else fwrite(p,1,q-p,stdout);
      width-=cells;p=q;
   }
   return initial-width;
}
static void line(int row,int cols,bool focus,const char *prefix,const char *value) {
   printf("\033[%d;%dH%s",row+oy,ox,focus?"\033[7m":"\033[0m");
   if(!overlay)fputs("\033[2K",stdout);
   int remain=cols-1-clipped(prefix,cols-1);
   if(remain>0)remain-=clipped(value,remain);
   if(overlay)while(remain-->0)putchar(' ');
   fputs("\033[0m",stdout);
}
static bool draw_form(rrui_form_t *form,int rows,int cols,bool has_focus) {
   if(!form)return false;
   if(!hits)hits=g_array_new(FALSE,FALSE,sizeof(hit_t));
   g_array_set_size(hits,0);
   if(!overlay)fputs("\033[0m\033[r\033[?25l\033[H\033[2J",stdout);
   if(rows<4||cols<12){clipped("Terminal too small",cols-1);fflush(stdout);return true;}
   size_t focus=rrui_form_focus(form),count=rrui_form_count(form);
   if(focus<scroll)scroll=focus;
   /* Keep focus in view: containers don't consume lines; list rows do. */
   for(;;) {
      int used=0;
      for(size_t i=scroll;i<focus&&i<count;i++) {
         rrui_field_t x;rrui_form_field(form,i,&x);
         if(!x.visible||x.widget->kind==RRUI_ROW||x.widget->kind==RRUI_COLUMN)continue;
         used+=x.widget->kind==RRUI_ENTRY?2:1;
         if(x.widget->kind==RRUI_LIST)used+=MIN(x.count,(size_t)MAX(1,MIN((int)x.widget->height_rows,rows/2)));
      }
      if(used<rows-3||scroll>=focus||focus>=count)break;
      scroll++;
   }
   int row=1;
   for(size_t i=scroll;i<count&&row<rows;i++) {
      rrui_field_t x;rrui_form_field(form,i,&x);
      const rrui_widget_t *w=x.widget;
      if(!x.visible||w->kind==RRUI_ROW||w->kind==RRUI_COLUMN)continue;
      bool selected=has_focus&&i==focus;
      if(w->kind==RRUI_LIST) {
         line(row++,cols,selected,"  ",w->label);
         size_t span=MAX(1,MIN((int)w->height_rows,rows/2));
         span=MIN(span,(size_t)MAX(0,rows-row));
         size_t start=x.selected!=SIZE_MAX&&x.selected>=span&&span?x.selected-span+1:0;
         for(size_t j=start;j<x.count&&j-start<span&&row<rows;j++) {
            hit_t h={i,j,row};g_array_append_val(hits,h);
            line(row++,cols,selected&&j==x.selected,j==x.selected?" > ":"   ",rrui_form_list_label(form,i,j));
         }
         continue;
      }
      if(x.interactive){hit_t h={i,SIZE_MAX,row};g_array_append_val(hits,h);}
      if(w->kind==RRUI_ENTRY) {
         /* Label and value on separate rows when space is scarce. */
         line(row++,cols,selected,"  ",w->label);
         if(row<rows) {
            hit_t h={i,SIZE_MAX,row};g_array_append_val(hits,h);
            size_t cursor=x.cursor;const char *p=x.value+cursor;
            int width=MAX(1,MIN(cols-5,w->width_chars?(int)w->width_chars:cols-5));
            int cells=0;
            while(p>x.value) {
               const char *q=g_utf8_prev_char(p);int n=w->masked?1:(g_unichar_iswide(g_utf8_get_char(q))?2:1);
               if(cells+n>=width)break;
               cells+=n;p=q;
            }
            char *masked=w->masked?g_strnfill(g_utf8_strlen(p,-1),'*'):NULL;
            line(row,cols,selected,"    ",masked?masked:p);
            (void)cells;
            g_free(masked);row++;
         }
      } else if(w->kind==RRUI_CHECKBOX)line(row++,cols,selected,x.checked?" [x] ":" [ ] ",w->label);
      else if(w->kind==RRUI_BUTTON)line(row++,cols,selected," [ ] ",w->label);
      else line(row++,cols,false,"  ",w->label);
   }
   if(!overlay)line(rows,cols,false,"","Tab / Shift-Tab  Arrows  Enter  Esc");
   fflush(stdout);return true;
}
bool rrui_tui_draw(int rows,int cols) {
   if(!active)return false;
   overlay=false;ox=1;oy=0;scroll=modal_scroll;hits=modal_hits;
   bool ok=draw_form(active,rows,cols,true);
   modal_scroll=scroll;modal_hits=hits;hits=NULL;return ok;
}
void rrui_tui_draw_panels(int rows,int cols) {
   workspace_rows=rows;workspace_cols=cols;
   if(active||rows<8||cols<14)return;
   overlay=true;
   for(size_t i=0;i<npanels;i++) {
      panel_t *p=&panels[i];rrui_rect_t r=p->requested;
      r.width=MIN(r.width,cols-1);r.height=MIN(r.height,rows-3);
      r.x=CLAMP(r.x,1,cols-r.width+1);r.y=CLAMP(r.y,2,rows-r.height);
      p->rect=r;ox=r.x;oy=r.y-1;
      for(int j=1;j<=r.height;j++)line(j,r.width+1,false,"","");
      rrui_field_t root={0};rrui_form_field(p->form,0,&root);
      line(1,r.width+1,focused==p->form,"[ ",root.widget?root.widget->label:"Panel");
      printf("\033[%d;%dH[+]",r.y+r.height-1,r.x+r.width-3);
      oy=r.y;scroll=p->scroll;hits=p->hits;
      draw_form(p->form,r.height-1,r.width+1,focused==p->form);
      p->scroll=scroll;p->hits=hits;hits=NULL;
   }
   overlay=false;ox=1;oy=0;
   if(focused)fputs("\033[?25l",stdout);
   fflush(stdout);
}
static bool directional(const TermKeyKey *k,int *dx,int *dy,bool *chat) {
   if(!(k->modifiers&TERMKEY_KEYMOD_CTRL)||k->modifiers&TERMKEY_KEYMOD_ALT)return false;
   int n=0;*dx=*dy=0;*chat=false;
   if(k->type==TERMKEY_TYPE_KEYSYM) {
      switch(k->code.sym) {
         case TERMKEY_SYM_LEFT:n=4;break;case TERMKEY_SYM_RIGHT:n=6;break;
         case TERMKEY_SYM_UP:n=8;break;case TERMKEY_SYM_DOWN:n=2;break;
         default:if(k->code.sym>=TERMKEY_SYM_KP1&&k->code.sym<=TERMKEY_SYM_KP9)n=k->code.sym-TERMKEY_SYM_KP0;break;
      }
   } else if(k->type==TERMKEY_TYPE_UNICODE&&k->code.codepoint>='1'&&k->code.codepoint<='9')n=k->code.codepoint-'0';
   if(!n)return false;
   *chat=n==5;*dx=(n-1)%3-1;*dy=1-(n-1)/3;return true;
}
bool rrui_tui_key(TermKey *tk,const TermKeyKey *key) {
   if(!active&&npanels) {
      int dx,dy;bool chat;
      if(directional(key,&dx,&dy,&chat)) {
         if(chat)rrui_tui_focus_chat();
         else {
            rrui_rect_t rects[33];size_t from=panel_index(focused);
            for(size_t j=0;j<npanels;j++)rects[j]=panels[j].rect;
            rects[npanels]=(rrui_rect_t){1,2,MAX(1,workspace_cols/2),MAX(1,workspace_rows-3)};
            if(from==SIZE_MAX)from=npanels;
            size_t next=rrui_workspace_nearest(rects,npanels+1,from,dx,dy);
            if(next==npanels)rrui_tui_focus_chat();else if(next!=SIZE_MAX)raise_panel(next);
         }
         tui_redraw_request();return true;
      }
      if(key->type==TERMKEY_TYPE_MOUSE) {
         TermKeyMouseEvent ev;int b,r,c;
         if(termkey_interpret_mouse(tk,key,&ev,&b,&r,&c)!=TERMKEY_RES_KEY)return true;
         if(dragging) {
            size_t j=panel_index(dragging);
            if(ev==TERMKEY_MOUSE_RELEASE){dragging=NULL;return true;}
            if(j!=SIZE_MAX&&ev==TERMKEY_MOUSE_DRAG){
               rrui_rect_t a=panels[j].rect;
               if(resizing){a.width=MAX(12,c-a.x+1);a.height=MAX(4,r-a.y+1);}
               else {a.x=c-drag_x;a.y=r-drag_y;}
               rrui_tui_panel_geometry(dragging,a);
            }
            return true;
         }
         if(ev!=TERMKEY_MOUSE_PRESS||b!=1)return focused!=NULL;
         size_t j=npanels;
         while(j) {j--;rrui_rect_t a=panels[j].rect;
            if(c<a.x||c>=a.x+a.width||r<a.y||r>=a.y+a.height)continue;
            raise_panel(j);panel_t *p=&panels[npanels-1];
            if(r==a.y||(r==a.y+a.height-1&&c>=a.x+a.width-3)){
               dragging=p->form;resizing=r!=a.y;drag_x=c-a.x;drag_y=r-a.y;tui_redraw_request();return true;
            }
            if(p->hits)for(guint k=0;k<p->hits->len;k++){
               hit_t h=g_array_index(p->hits,hit_t,k);
               if(h.row+a.y!=r)continue;
               if(rrui_form_set_focus(p->form,h.field)){
                  if(h.item!=SIZE_MAX)rrui_form_select(p->form,h.field,h.item);
                  else {rrui_field_t z;if(rrui_form_field(p->form,h.field,&z)&&(z.widget->kind==RRUI_BUTTON||z.widget->kind==RRUI_CHECKBOX))rrui_form_activate(p->form);}
               }
               break;
            }
            tui_redraw_request();return true;
         }
         rrui_tui_focus_chat();return false;
      }
      if(focused&&key->type==TERMKEY_TYPE_KEYSYM&&key->code.sym==TERMKEY_SYM_ESCAPE){rrui_tui_focus_chat();return true;}
   }
   rrui_form_t *f=active?active:focused;
   if(!f)return false;
   GArray *local_hits=active?modal_hits:NULL;
   size_t i=rrui_form_focus(f);rrui_field_t x={0};rrui_form_field(f,i,&x);
   if(key->type==TERMKEY_TYPE_MOUSE) {
      TermKeyMouseEvent ev;int b,r,c;TermKeyKey copy=*key;
      if(local_hits&&termkey_interpret_mouse(tk,&copy,&ev,&b,&r,&c)==TERMKEY_RES_KEY&&ev==TERMKEY_MOUSE_PRESS&&b==1) {
         for(guint j=0;j<local_hits->len;j++) {
            hit_t h=g_array_index(local_hits,hit_t,j);
            if(h.row!=r)continue;
            if(!rrui_form_set_focus(f,h.field))break;
            if(h.item!=SIZE_MAX)rrui_form_select(f,h.field,h.item);
            else {rrui_field_t y;if(rrui_form_field(f,h.field,&y)&&(y.widget->kind==RRUI_BUTTON||y.widget->kind==RRUI_CHECKBOX))rrui_form_activate(f);}
            break;
         }
      }
   } else if(key->type==TERMKEY_TYPE_KEYSYM) {
      switch(key->code.sym) {
         case TERMKEY_SYM_ESCAPE:rrui_form_cancel(f);break;
         case TERMKEY_SYM_TAB:rrui_form_next(f,key->modifiers&TERMKEY_KEYMOD_SHIFT?-1:1);break;
         case TERMKEY_SYM_KPENTER:
         case TERMKEY_SYM_ENTER:rrui_form_activate(f);break;
         case TERMKEY_SYM_SPACE:if(x.widget&&x.widget->kind==RRUI_ENTRY)rrui_form_insert(f,' ');else rrui_form_activate(f);break;
         case TERMKEY_SYM_LEFT:if(x.widget&&x.widget->kind==RRUI_ENTRY)rrui_form_edit(f,-1,false);else rrui_form_next(f,-1);break;
         case TERMKEY_SYM_RIGHT:if(x.widget&&x.widget->kind==RRUI_ENTRY)rrui_form_edit(f,1,false);else rrui_form_next(f,1);break;
         case TERMKEY_SYM_BACKSPACE:rrui_form_edit(f,-1,true);break;
         case TERMKEY_SYM_DELETE:rrui_form_edit(f,1,true);break;
         case TERMKEY_SYM_UP:
         case TERMKEY_SYM_DOWN:
            if(x.widget&&x.widget->kind==RRUI_LIST&&x.count) {
               size_t n=x.selected==SIZE_MAX?0:x.selected;
               if(x.selected!=SIZE_MAX){if(key->code.sym==TERMKEY_SYM_UP&&n)n--;else if(key->code.sym==TERMKEY_SYM_DOWN&&n+1<x.count)n++;}
               rrui_form_select(f,i,n);
            } else rrui_form_next(f,key->code.sym==TERMKEY_SYM_UP?-1:1);
            break;
         default:break;
      }
   } else if(key->type==TERMKEY_TYPE_UNICODE&&!(key->modifiers&(TERMKEY_KEYMOD_CTRL|TERMKEY_KEYMOD_ALT))) {
      if(key->code.codepoint==' '&&x.widget&&x.widget->kind!=RRUI_ENTRY)rrui_form_activate(f);
      else if(key->code.codepoint=='\t')rrui_form_next(f,key->modifiers&TERMKEY_KEYMOD_SHIFT?-1:1);
      else rrui_form_insert(f,key->code.codepoint);
   }
   tui_redraw_request();return true;
}
