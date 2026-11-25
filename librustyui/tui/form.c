/* SPDX-License-Identifier: MIT */
#include <librustyui/form.h>
#include <librustyui/theme.h>
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
typedef struct {rrui_form_t *form;rrui_rect_t requested,rect;size_t scroll;GArray *hits;bool tiled;int minimum,weight;unsigned order;} panel_t;
static panel_t panels[32];
static size_t npanels;
static int tile_width=34,chat_width;
static unsigned tile_order;
static bool divider_drag;
static rrui_form_t *split_upper,*split_lower;
static int split_grab_offset,divider_grab_offset;
static rrui_form_t *focused;
static int ox=1,oy=0;
static bool overlay;
static uint32_t row_color=UINT32_MAX;
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
bool rrui_tui_tile_open(rrui_form_t *f,int minimum,int weight) {
   tui_render_lock();
   bool ok=rrui_tui_panel_open(f,(rrui_rect_t){1,2,24,6});
   if(ok){panel_t *p=&panels[npanels-1];p->tiled=true;p->minimum=minimum;p->weight=weight;p->order=++tile_order;}
   tui_render_unlock();return ok;
}
void rrui_tui_tile_width(int width){tile_width=MAX(24,width);tui_redraw_request();}
bool rrui_tui_panel_focused(const rrui_form_t *f){return focused==f&&!active;}
int rrui_tui_chat_width(int rows,int cols,bool structured){
   size_t ids[32],n=0;int minimum[32],weight[32];rrui_rect_t rects[32];
   workspace_rows=rows;workspace_cols=cols;
   for(size_t i=0;i<npanels;i++)if(panels[i].tiled){
      size_t j=n++;while(j&&panels[ids[j-1]].order>panels[i].order){ids[j]=ids[j-1];j--;}ids[j]=i;
   }
   for(size_t i=0;i<n;i++){minimum[i]=panels[ids[i]].minimum;weight[i]=panels[ids[i]].weight;}
   chat_width=rrui_workspace_tiles(structured?0:rows,cols,tile_width,minimum,weight,n,rects);
   for(size_t i=0;i<n;i++){panel_t *p=&panels[ids[i]];p->rect=rects[i];if(!p->rect.width&&focused==p->form)focused=NULL;}
   return chat_width;
}
void rrui_tui_focus_chat(void){focused=NULL;tui_redraw_request();}
bool rrui_tui_modal_active(void){return active!=NULL;}


bool rrui_tui_open(rrui_form_t *f) {
   if(!f||!tui_is_enabled||active||panel_index(f)!=SIZE_MAX)return false;
   active=f;modal_scroll=0;tui_redraw_request();return true;
}
void rrui_tui_close(rrui_form_t *f) {
   if(active&&(!f||active==f)){active=NULL;if(modal_hits){g_array_unref(modal_hits);modal_hits=NULL;}tui_redraw_request();}
   for(size_t i=0;i<npanels;) {if(!f||panels[i].form==f){if(focused==panels[i].form)focused=NULL;if(split_upper==panels[i].form||split_lower==panels[i].form)split_upper=split_lower=NULL;if(dragging==panels[i].form)dragging=NULL;if(panels[i].hits)g_array_unref(panels[i].hits);memmove(panels+i,panels+i+1,(npanels-i-1)*sizeof(panel_t));npanels--;tui_redraw_request();}else i++;}
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
   printf("\033[%d;%dH",row+oy,ox);
   rrui_tui_theme_style(RRUI_THEME_TEXT,focus?RRUI_THEME_SELECTION:RRUI_THEME_PANEL);
   if(focus&&!cfg_tui_colors)fputs("\033[7m",stdout);
   if(!overlay)fputs("\033[2K",stdout);
   if(!focus&&row_color<=0xffffff)rrui_tui_theme_rgb(row_color,false);
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
         if(!x.visible||x.widget->kind==RRUI_ROW||x.widget->kind==RRUI_COLUMN||(overlay&&i==0))continue;
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
      if(!x.visible||w->kind==RRUI_ROW||w->kind==RRUI_COLUMN||(overlay&&i==0))continue;
      bool selected=has_focus&&i==focus;
      if(w->kind==RRUI_LIST) {
         line(row++,cols,selected,"  ",w->label);
         size_t span=w->expand_y?(size_t)MAX(1,rows-row):MAX(1,MIN((int)w->height_rows,rows/2));
         span=MIN(span,(size_t)MAX(0,rows-row));
         size_t start=x.selected!=SIZE_MAX&&x.selected>=span&&span?x.selected-span+1:0;
         for(size_t j=start;j<x.count&&j-start<span&&row<rows;j++) {
            hit_t h={i,j,row};g_array_append_val(hits,h);
            row_color=rrui_form_list_color(form,i,j);
            line(row++,cols,selected&&j==x.selected,j==x.selected?" > ":"   ",rrui_form_list_label(form,i,j));
            row_color=UINT32_MAX;
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
      else if(w->kind==RRUI_BUTTON)line(row++,cols,selected," > ",w->label);
      else if(w->kind==RRUI_LABEL&&*x.value)line(row++,cols,false,w->label,x.value);
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
   fputs("\033[s",stdout);
   overlay=true;
   for(size_t i=0;i<npanels;i++) {
      panel_t *p=&panels[i];rrui_rect_t r=p->tiled?p->rect:p->requested;
      if(p->tiled&&!r.width)continue;
      r.width=MIN(r.width,cols-1);r.height=MIN(r.height,rows-3);
      r.x=CLAMP(r.x,1,cols-r.width+1);r.y=CLAMP(r.y,2,rows-r.height);
      p->rect=r;
      bool selected=focused==p->form;
      ox=r.x;oy=r.y-1;
      for(int j=1;j<=r.height;j++)line(j,r.width+1,false,"","");
      /* Reserve both border columns and the title/bottom rows. */
      ox=r.x+1;oy=r.y;scroll=p->scroll;hits=p->hits;
      if(r.height>=6)
         draw_form(p->form,r.height-1,r.width-1,selected);
      else if(hits)
         g_array_set_size(hits,0);
      p->scroll=scroll;p->hits=hits;hits=NULL;
      /* Bright cyan for keyboard focus; ordinary cyan for inactive panels.
       * ANSI 16-color SGR works without true-color terminal support. */
      rrui_tui_theme_style(selected?RRUI_THEME_FOCUS:RRUI_THEME_BORDER,RRUI_THEME_PANEL);
      printf("\033[%d;%dH┌",r.y,r.x);
      for(int j=0;j<r.width-2;j++)fputs("─",stdout);
      fputs("┐",stdout);
      for(int j=1;j<r.height-1;j++){
         printf("\033[%d;%dH│",r.y+j,r.x);
         printf("\033[%d;%dH│",r.y+j,r.x+r.width-1);
      }
      printf("\033[%d;%dH└",r.y+r.height-1,r.x);
      for(int j=0;j<r.width-5;j++)fputs("─",stdout);
      fputs(p->tiled?"───┘":"[+]┘",stdout);
      rrui_field_t root={0};rrui_form_field(p->form,0,&root);
      printf("\033[%d;%dH",r.y,r.x+1);
      rrui_tui_theme_style(selected?RRUI_THEME_FOCUS:RRUI_THEME_TEXT,RRUI_THEME_HEADER);
      putchar(' ');int used=clipped(root.widget?root.widget->label:"Panel",r.width-4);
      for(int k=used+1;k<r.width-2;k++)putchar(' ');
      fputs("\033[0m",stdout);
   }
   overlay=false;ox=1;oy=0;
   fputs("\033[u",stdout);
   if(focused)fputs("\033[?25l",stdout);
   else fputs("\033[?25h",stdout);
   fflush(stdout);
}
/* Resize in tile order rather than z-order, preserving all other visible
 * tile heights. Store ratios as weights so terminal resize remains adaptive. */
static void split_resize(int boundary) {
   size_t ids[32],n=0,upper=SIZE_MAX;
   rrui_rect_t rects[32];int minimum[32],weights[32];
   for(size_t i=0;i<npanels;i++)if(panels[i].tiled&&panels[i].rect.width){
      size_t j=n++;while(j&&panels[ids[j-1]].order>panels[i].order){ids[j]=ids[j-1];j--;}ids[j]=i;
   }
   for(size_t i=0;i<n;i++){
      panel_t *p=&panels[ids[i]];rects[i]=p->rect;minimum[i]=p->minimum;
      if(p->form==split_upper)upper=i;
   }
   if(upper==SIZE_MAX||upper+1>=n||panels[ids[upper+1]].form!=split_lower){split_upper=split_lower=NULL;return;}
   if(rrui_workspace_split(rects,minimum,n,upper,boundary,weights)){
      for(size_t i=0;i<n;i++)panels[ids[i]].weight=weights[i];
      tui_redraw_request();
   }
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
            rects[npanels]=(rrui_rect_t){1,2,MAX(1,chat_width?chat_width:workspace_cols/2),MAX(1,workspace_rows-3)};
            if(from==SIZE_MAX)from=npanels;
            size_t next=rrui_workspace_nearest(rects,npanels+1,from,dx,dy);
            if(next==npanels)rrui_tui_focus_chat();else if(next!=SIZE_MAX)raise_panel(next);
         }
         tui_redraw_request();return true;
      }
      if(key->type==TERMKEY_TYPE_MOUSE) {
         TermKeyMouseEvent ev;int b,r,c;
         if(termkey_interpret_mouse(tk,key,&ev,&b,&r,&c)!=TERMKEY_RES_KEY)return true;
         if(split_upper){
            if(ev==TERMKEY_MOUSE_RELEASE)split_upper=split_lower=NULL;
            else if(ev==TERMKEY_MOUSE_DRAG)split_resize(r+split_grab_offset);
            return true;
         }
         if(divider_drag){
            if(ev==TERMKEY_MOUSE_RELEASE)divider_drag=false;
            else if(ev==TERMKEY_MOUSE_DRAG)rrui_tui_tile_width(CLAMP(workspace_cols-c+1-divider_grab_offset,24,MAX(24,workspace_cols-40)));
            return true;
         }
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
         /* Either column of the double vertical border is draggable.
          * Floating panels in front retain their normal mouse handling. */
         size_t j=npanels;
         while(j) {j--;rrui_rect_t a=panels[j].rect;
            if(a.width<=0||a.height<=0||r<a.y||r>=a.y+a.height)continue;
            if(panels[j].tiled&&(c==a.x||c==a.x-1)){
               divider_drag=true;divider_grab_offset=a.x-c;return true;
            }
            if(c<a.x||c>=a.x+a.width)continue;
            if(panels[j].tiled&&(r==a.y||r==a.y+a.height-1)){
               for(size_t k=0;k<npanels;k++){
                  panel_t *q=&panels[k];rrui_rect_t b=q->rect;
                  if(k==j||!q->tiled||!b.width||b.x!=a.x)continue;
                  if(r==a.y&&b.y+b.height==a.y){
                     split_upper=q->form;split_lower=panels[j].form;split_grab_offset=0;return true;
                  }
                  if(r==a.y+a.height-1&&a.y+a.height==b.y){
                     split_upper=panels[j].form;split_lower=q->form;split_grab_offset=1;return true;
                  }
               }
            }
            raise_panel(j);panel_t *p=&panels[npanels-1];
            if(!p->tiled&&(r==a.y||(r==a.y+a.height-1&&c>=a.x+a.width-4))){
               dragging=p->form;resizing=r!=a.y;drag_x=c-a.x;drag_y=r-a.y;tui_redraw_request();return true;
            }
            if(c>a.x&&c<a.x+a.width-1&&p->hits)for(guint k=0;k<p->hits->len;k++){
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
