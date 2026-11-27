#include "render.h"
/* SPDX-License-Identifier: MIT */
#include <librustyui/form.h>
#include <librustyui/theme.h>
#include <librustyui/tui.workspace.h>
#include <librustyui/tui.h>
#include "termkey.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static rrui_form_t *active;
static rrui_form_t *digit_click_form;
static size_t digit_click_field,digit_click_position;
static gint64 digit_click_time;
static size_t scroll;
typedef struct {size_t field,item;int row,left,right,minus,plus;unsigned slider_pos,slider_span;bool slider,digits;} hit_t;
static GArray *hits;
typedef struct {rrui_form_t *form;rrui_rect_t requested,rect;size_t scroll;GArray *hits;bool tiled,hidden;int minimum,weight;unsigned order;} panel_t;
static panel_t panels[32];
static size_t npanels;
static int tile_width=34,chat_width;
static rrui_tui_sidebar_policy_fn sidebar_policy;
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
typedef struct {tui_window_t *win;rrui_form_t *form;size_t scroll;GArray *hits;} tab_form_t;
static tab_form_t tabs[TUI_MAX_WINDOWS];
static tab_form_t *tab_find(tui_window_t *w){for(size_t i=0;i<G_N_ELEMENTS(tabs);i++)if(tabs[i].win==w&&w)return &tabs[i];return NULL;}
static size_t panel_index(rrui_form_t *f) {for(size_t i=0;i<npanels;i++)if(panels[i].form==f)return i;return SIZE_MAX;}
static void raise_panel(size_t i) {if(i>=npanels)return;panel_t p=panels[i];memmove(panels+i,panels+i+1,(npanels-i-1)*sizeof(p));panels[npanels-1]=p;focused=p.form;}
bool rrui_tui_panel_geometry(rrui_form_t *f,rrui_rect_t r) {size_t i=panel_index(f);if(i==SIZE_MAX||r.width<12||r.height<4)return false;panels[i].requested=r;tui_redraw_request();return true;}
bool rrui_tui_panel_open(rrui_form_t *f,rrui_rect_t r) {rrui_form_set_detach_hook(rrui_tui_close);if(!f||!tui_is_enabled||active==f||panel_index(f)!=SIZE_MAX||npanels>=32||r.width<12||r.height<4)return false;panels[npanels++]=(panel_t){.form=f,.requested=r,.rect=r,.order=++tile_order};tui_redraw_request();return true;}
bool rrui_tui_tile_open(rrui_form_t *f,int minimum,int weight) {
   tui_render_lock();
   bool ok=rrui_tui_panel_open(f,(rrui_rect_t){1,2,24,6});
   if(ok){panel_t *p=&panels[npanels-1];p->tiled=true;p->minimum=minimum;p->weight=weight;}
   tui_render_unlock();return ok;
}
void rrui_tui_tile_width(int width){tile_width=CLAMP(width,24,1000);tui_redraw_request();}
int rrui_tui_tile_width_get(void){return tile_width;}
int rrui_tui_tile_weight_get(const rrui_form_t *f){for(size_t i=0;i<npanels;i++)if(panels[i].form==f)return panels[i].weight;return 0;}
void rrui_tui_tile_weight_set(rrui_form_t *f,int w){size_t i=panel_index(f);if(i!=SIZE_MAX&&panels[i].tiled){panels[i].weight=CLAMP(w,1,1000);tui_redraw_request();}}
void rrui_tui_tile_visible(rrui_form_t *f,bool visible){
   size_t i=panel_index(f);if(i==SIZE_MAX||!panels[i].tiled||panels[i].hidden==!visible)return;
   panels[i].hidden=!visible;
   if(!visible){panels[i].rect=(rrui_rect_t){0};if(focused==f)focused=NULL;if(split_upper==f||split_lower==f)split_upper=split_lower=NULL;}
   tui_redraw_request();
}
bool rrui_tui_main_focused(void){return !focused&&!active;}
bool rrui_tui_panel_focused(const rrui_form_t *f){return focused==f&&!active;}
void rrui_tui_sidebar_policy(rrui_tui_sidebar_policy_fn policy) {
   sidebar_policy=policy;
   tui_redraw_request();
}
int rrui_tui_chat_width(int rows,int cols,bool structured){
   if (!structured && sidebar_policy && !sidebar_policy(tui_active_window())) {
      structured=true;
   }
   size_t ids[32],n=0;int minimum[32],weight[32];rrui_rect_t rects[32];
   workspace_rows=rows;workspace_cols=cols;
   for(size_t i=0;i<npanels;i++)if(panels[i].tiled&&!panels[i].hidden){
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
   rrui_form_set_detach_hook(rrui_tui_close);
   if(!f||!tui_is_enabled||active||panel_index(f)!=SIZE_MAX)return false;
   active=f;modal_scroll=0;tui_redraw_request();return true;
}
void rrui_tui_close(rrui_form_t *f) {
   if(active&&(!f||active==f)){active=NULL;if(modal_hits){g_array_unref(modal_hits);modal_hits=NULL;}tui_redraw_request();}
   if (!f || digit_click_form==f) { digit_click_form=NULL;digit_click_time=0; }
   for(size_t i=0;i<npanels;) {if(!f||panels[i].form==f){if(focused==panels[i].form)focused=NULL;if(split_upper==panels[i].form||split_lower==panels[i].form)split_upper=split_lower=NULL;if(dragging==panels[i].form)dragging=NULL;if(panels[i].hits)g_array_unref(panels[i].hits);memmove(panels+i,panels+i+1,(npanels-i-1)*sizeof(panel_t));npanels--;tui_redraw_request();}else i++;}
}
/* Print plain terminal text only. Count wide characters conservatively. */
static int clipped(const char *s,int width) {
   int initial=width;
   if(!s)return 0;
   for(const char *p=s;*p&&width>0;) {
      gunichar c=g_utf8_get_char_validated(p,-1);
      if(c==(gunichar)-1||c==(gunichar)-2){rrui_render_char('?');p++;width--;continue;}
      const char *q=g_utf8_next_char(p);
      int cells=g_unichar_iswide(c)?2:1;
      if(cells>width)break;
      if(g_unichar_iscntrl(c)||g_unichar_type(c)==G_UNICODE_FORMAT)rrui_render_char('?');
      else rrui_render_write(p,q-p);
      width-=cells;p=q;
   }
   return initial-width;
}
static void line(int row,int cols,bool focus,const char *prefix,const char *value) {
   rrui_render_printf("\033[%d;%dH",row+oy,ox);
   rrui_tui_theme_style(RRUI_THEME_TEXT,focus?RRUI_THEME_SELECTION:RRUI_THEME_PANEL);
   if(focus&&!cfg_tui_colors)rrui_render_write("\033[7m",strlen("\033[7m"));
   if(!overlay)rrui_render_write("\033[2K",strlen("\033[2K"));
   if(!focus&&row_color<=0xffffff)rrui_tui_theme_rgb(row_color,false);
   int remain=cols-1-clipped(prefix,cols-1);
   if(remain>0)remain-=clipped(value,remain);
   if(overlay)while(remain-->0)rrui_render_char(' ');
   rrui_render_write("\033[0m",strlen("\033[0m"));
}
/* Compact explicit horizontal/flow rows. Only simple controls are packed
 * here; other widget kinds retain their existing vertical renderer. */
static bool compact_control(const rrui_form_t *f,const rrui_widget_t *w){
   if(w->kind!=RRUI_BUTTON&&w->kind!=RRUI_CHECKBOX)return false;
   for(size_t i=0;i<rrui_form_count(f);i++){
      rrui_field_t p;
      if(rrui_form_field(f,i,&p)&&!strcmp(p.widget->id,w->parent))
         return p.widget->layout==RRUI_PACK_HORIZONTAL||p.widget->layout==RRUI_PACK_FLOW;
   }
   return false;
}
static int control_width(const rrui_widget_t *w){
   int cells=0;
   for(const char *p=w->label;*p;p=g_utf8_next_char(p))cells+=g_unichar_iswide(g_utf8_get_char(p))?2:1;
   return cells+(w->kind==RRUI_CHECKBOX?5:4);
}
/* Measure trailing controls so an expanding list cannot swallow its footer. */
static int form_height(rrui_form_t *f,size_t start,size_t end,int rows,int cols){
   int used=0,xoff=0;
   const char *parent=NULL;
   for(size_t i=start;i<end;i++){
      rrui_field_t x;if(!rrui_form_field(f,i,&x)||!x.visible)continue;
      const rrui_widget_t *w=x.widget;
      if(w->kind==RRUI_ROW||w->kind==RRUI_COLUMN||(overlay&&i==0))continue;
      if(compact_control(f,w)){
         int width=MIN(control_width(w),cols-1);
         if(!parent||strcmp(parent,w->parent)||xoff+width>cols-1){used++;xoff=0;}
         xoff+=width;parent=w->parent;continue;
      }
      parent=NULL;xoff=0;
      used+=w->kind==RRUI_ENTRY||w->kind==RRUI_DIGITS||(w->kind==RRUI_SLIDER&&!w->vertical)?2:1;
      if(w->kind==RRUI_SLIDER&&w->vertical)used+=MIN((int)(w->height_rows?w->height_rows:5),MAX(1,rows/2));
      if(w->kind==RRUI_LIST)used+=w->expand_y?1:MIN(x.count,(size_t)MAX(1,MIN((int)w->height_rows,rows/2)));
   }
   return used;
}
static bool draw_form(rrui_form_t *form,int rows,int cols,bool has_focus) {
   if(!form)return false;
   if(!hits)hits=g_array_new(FALSE,FALSE,sizeof(hit_t));
   g_array_set_size(hits,0);
   if(!overlay)rrui_render_write("\033[0m\033[r\033[?25l\033[H\033[2J",strlen("\033[0m\033[r\033[?25l\033[H\033[2J"));
   if(rows<4||cols<12){clipped("Terminal too small",cols-1);rrui_render_flush();return true;}
   size_t focus=rrui_form_focus(form),count=rrui_form_count(form);
   if(focus<scroll)scroll=focus;
   /* Keep focus in view: containers don't consume lines; list rows do. */
   for(;;) {
      int used=form_height(form,scroll,MIN(focus,count),rows,cols);
      if(used<rows-3||scroll>=focus||focus>=count)break;
      scroll++;
   }
   int row=1;
   for(size_t i=scroll;i<count&&row<rows;i++) {
      rrui_field_t x;rrui_form_field(form,i,&x);
      const rrui_widget_t *w=x.widget;
      if(!x.visible||w->kind==RRUI_ROW||w->kind==RRUI_COLUMN||(overlay&&i==0))continue;
      bool selected=has_focus&&i==focus;
      if(compact_control(form,w)){
         int xoff=0;
         do{
            rrui_field_t z;rrui_form_field(form,i,&z);
            int width=MIN(control_width(z.widget),cols-1);
            if(xoff&&xoff+width>cols-1){row++;xoff=0;}
            if(row>=rows)break;
            int oldox=ox;ox+=xoff;
            char *caption=z.widget->kind==RRUI_CHECKBOX?
               g_strdup_printf("[%c] %s",z.checked?'x':' ',z.widget->label):
               g_strdup_printf("[%s]",z.widget->label);
            line(row,width+1,has_focus&&i==focus,"",caption);
            g_free(caption);ox=oldox;
            if(z.interactive){
               hit_t h={.field=i,.item=SIZE_MAX,.row=row,.left=xoff+1,.right=xoff+width};
               g_array_append_val(hits,h);
            }
            xoff+=width;
            if(i+1>=count)break;
            rrui_field_t next;rrui_form_field(form,i+1,&next);
            if(!next.visible||strcmp(next.widget->parent,w->parent)||!compact_control(form,next.widget))break;
            i++;
         }while(true);
         row++;continue;
      }
      if(w->kind==RRUI_SLIDER) {
         gint64 range=(gint64)w->maximum-w->minimum;
         double fraction=*x.value&&range?CLAMP((g_ascii_strtoll(x.value,NULL,10)-(double)w->minimum)/range,0.,1.):0.;
         char *caption=g_strdup_printf("%s %s",w->label,*x.value?x.value:"--");
         if(w->vertical){
            line(row++,cols,selected,"  ",caption);
            int height=MIN((int)(w->height_rows?w->height_rows:5),MIN(MAX(1,rows/2),rows-row));
            for(int j=0;j<height;j++){
               int pos=height-1-j;
               const char *track=!*x.value?"│":pos==(int)(fraction*(height-1)+0.5)?"●":pos<fraction*(height-1)?"┃":"│";
               line(row,cols,selected,"    ",track);
               if(x.interactive&&height>1){
                  hit_t h={.field=i,.item=SIZE_MAX,.row=row,.left=4,.right=6,.slider=true,.slider_pos=(unsigned)pos,.slider_span=(unsigned)(height-1)};
                  g_array_append_val(hits,h);
               }
               row++;
            }
         }else{
            int width=MIN((int)(w->width_chars?w->width_chars:12),MAX(2,cols-5));
            line(row++,cols,selected,"  ",caption);
            if(row<rows){
               GString *track=g_string_new("");
               for(int j=0;j<width;j++)g_string_append(track,!*x.value?"─":j==(int)(fraction*(width-1)+0.5)?"●":j<fraction*(width-1)?"━":"─");
               line(row,cols,selected,"  ",track->str);
               g_string_free(track,TRUE);
               if(x.interactive){
                  hit_t h={.field=i,.item=SIZE_MAX,.row=row,.left=3,.right=width+2,.slider=true,.slider_span=(unsigned)(width-1)};
                  g_array_append_val(hits,h);
               }
               row++;
            }
         }
         g_free(caption);continue;
      }
      if(w->kind==RRUI_STEPPER) {
         /* Consecutive steppers under the same parent flow horizontally and
          * wrap to the next line when the terminal becomes narrow. */
         int xoff=0;
         do {
            rrui_field_t z;rrui_form_field(form,i,&z);
            const rrui_widget_t *zw=z.widget;
            char *value=g_strdup_printf("%s [-] %s [+]",zw->label,*z.value?z.value:"--");
            int width=(int)g_utf8_strlen(value,-1)+2;
            if(xoff&&xoff+width>cols-1){row++;xoff=0;}
            if(row>=rows){g_free(value);break;}
            int oldox=ox;ox+=xoff;
            int span=MIN(width,cols-1-xoff);
            if(span>0){
               rrui_render_printf("\033[%d;%dH",row+oy,ox);
               rrui_tui_theme_style(RRUI_THEME_TEXT,has_focus&&i==focus?RRUI_THEME_SELECTION:RRUI_THEME_PANEL);
               int remain=span-clipped(value,span);
               while(remain-->0)rrui_render_char(' ');
               rrui_render_write("\033[0m",strlen("\033[0m"));
               int minus=xoff+(int)g_utf8_strlen(zw->label,-1)+3;
               int plus=minus+5+(int)g_utf8_strlen(*z.value?z.value:"--",-1);
               hit_t h={.field=i,.item=SIZE_MAX,.row=row,.left=xoff+1,.right=xoff+span,.minus=minus,.plus=plus};
               if(z.interactive)g_array_append_val(hits,h);
            }
            ox=oldox;xoff+=width;g_free(value);
            if(i+1>=count)break;
            rrui_field_t next;rrui_form_field(form,i+1,&next);
            if(!next.visible||next.widget->kind!=RRUI_STEPPER||strcmp(next.widget->parent,w->parent))break;
            i++;
         }while(true);
         row++;
         continue;
      }
      if(w->kind==RRUI_LIST) {
         line(row++,cols,selected,"  ",w->label);
         int footer=form_height(form,i+1,count,rows,cols);
         size_t span=w->expand_y?(size_t)MAX(1,rows-row-footer):MAX(1,MIN((int)w->height_rows,rows/2));
         span=MIN(span,(size_t)MAX(0,rows-row));
         size_t start=rrui_form_list_view(form,i,span,false);
         for(size_t j=start;j<x.count&&j-start<span&&row<rows;j++) {
            hit_t h={i,j,row};g_array_append_val(hits,h);
            row_color=rrui_form_list_color(form,i,j);
            line(row++,cols,selected&&j==x.selected,j==x.selected?" > ":rrui_form_list_selectable(form,i,j)?"   ":" ",rrui_form_list_label(form,i,j));
            row_color=UINT32_MAX;
         }
         if(w->expand_y)while(row<rows-footer&&row<rows)line(row++,cols,false,"","");
         continue;
      }
      if(x.interactive){hit_t h={i,SIZE_MAX,row};g_array_append_val(hits,h);}
      if(w->kind==RRUI_DIGITS) {
         line(row++,cols,selected,"  ",w->label);
         if (row<rows) {
            line(row,cols,selected,"    ",*x.value?x.value:"--");
            if (*x.value && x.interactive) {
               hit_t h={.field=i,.item=SIZE_MAX,.row=row,.left=5,
                  .right=MIN(cols-1,4+(int)w->width_chars),.digits=true};
               g_array_append_val(hits,h);
               size_t length=strlen(x.value);
               for (size_t digit=0;digit<length && digit<(size_t)MAX(0,cols-5);digit++) {
                  rrui_render_printf("\033[%d;%dH",row+oy,ox+4+(int)digit);
                  if (selected && digit==x.cursor) {
                     rrui_tui_theme_style(RRUI_THEME_SELECTION,RRUI_THEME_TEXT);
                  } else {
                     rrui_tui_theme_style(RRUI_THEME_TEXT,((length-1-digit)/3)%2?RRUI_THEME_HEADER:RRUI_THEME_PANEL);
                  }
                  rrui_render_char(x.value[digit]);
               }
               rrui_render_write("\033[0m",4);
            }
            row++;
         }
      } else if(w->kind==RRUI_ENTRY) {
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
   rrui_render_flush();return true;
}
static void tab_draw(tui_window_t *w,int rows,int cols,void *unused) {
   (void)unused;
   tab_form_t *t=tab_find(w);if(!t)return;
   overlay=true;ox=1;oy=1;scroll=t->scroll;hits=t->hits;
   draw_form(t->form,rows-1,cols,true);
   t->scroll=scroll;t->hits=hits;hits=NULL;overlay=false;ox=1;oy=0;
}
bool rrui_tui_tab_attach(tui_window_t *w,rrui_form_t *f){
   if(!w||!f||w->view_draw||tab_find(w))return false;
   for(size_t i=0;i<G_N_ELEMENTS(tabs);i++)if(!tabs[i].win){
      tabs[i]=(tab_form_t){.win=w,.form=f};w->view_draw=tab_draw;w->view_full_height=true;
      tui_redraw_request();return true;
   }
   return false;
}
void rrui_tui_tab_detach(tui_window_t *w){
   tab_form_t *t=tab_find(w);if(!t)return;
   if(t->hits)g_array_unref(t->hits);
   *t=(tab_form_t){0};w->view_draw=NULL;w->view_full_height=false;
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
   rrui_render_write("\033[s",strlen("\033[s"));
   overlay=true;
   for(size_t i=0;i<npanels;i++) {
      panel_t *p=&panels[i];rrui_rect_t r=p->tiled?p->rect:p->requested;
      if(p->hidden||(p->tiled&&!r.width))continue;
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
      rrui_render_printf("\033[%d;%dH┌",r.y,r.x);
      for(int j=0;j<r.width-2;j++)rrui_render_write("─",strlen("─"));
      rrui_render_write("┐",strlen("┐"));
      for(int j=1;j<r.height-1;j++){
         rrui_render_printf("\033[%d;%dH│",r.y+j,r.x);
         rrui_render_printf("\033[%d;%dH│",r.y+j,r.x+r.width-1);
      }
      rrui_render_printf("\033[%d;%dH└",r.y+r.height-1,r.x);
      for(int j=0;j<r.width-5;j++)rrui_render_write("─",strlen("─"));
      rrui_render_write(p->tiled?"───┘":"[+]┘",strlen(p->tiled?"───┘":"[+]┘"));
      rrui_field_t root={0};rrui_form_field(p->form,0,&root);
      rrui_render_printf("\033[%d;%dH",r.y,r.x+1);
      rrui_tui_theme_style(selected?RRUI_THEME_WORKSPACE:RRUI_THEME_TEXT,selected?RRUI_THEME_ACTIVE_HEADER:RRUI_THEME_HEADER);
      rrui_render_char(' ');int used=clipped(root.widget?root.widget->label:"Panel",r.width-4);
      for(int k=used+1;k<r.width-2;k++)rrui_render_char(' ');
      rrui_render_write("\033[0m",strlen("\033[0m"));
   }
   overlay=false;ox=1;oy=0;
   rrui_render_write("\033[u",strlen("\033[u"));
   if(focused)rrui_render_write("\033[?25l",strlen("\033[?25l"));
   else rrui_render_write("\033[?25h",strlen("\033[?25h"));
   rrui_render_flush();
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
         default:if(k->code.sym>=TERMKEY_SYM_KP1&&k->code.sym<=TERMKEY_SYM_KP9)n=k->code.sym-TERMKEY_SYM_KP0;break;
      }
   } else if(k->type==TERMKEY_TYPE_UNICODE&&k->code.codepoint>='1'&&k->code.codepoint<='9')n=k->code.codepoint-'0';
   if(!n)return false;
   *chat=n==5;*dx=(n-1)%3-1;*dy=1-(n-1)/3;return true;
}
/* Stable creation order, not z-order: raising a panel must not change the cycle. */
static void cycle_panel(bool reverse) {
   size_t ids[32],n=0;
   for(size_t j=0;j<npanels;j++){
      if(panels[j].hidden||panels[j].rect.width<=0||panels[j].rect.height<6)continue;
      size_t k=n++;
      while(k&&panels[ids[k-1]].order>panels[j].order){ids[k]=ids[k-1];k--;}
      ids[k]=j;
   }
   size_t pos=0;
   for(size_t j=0;j<n;j++)if(panels[ids[j]].form==focused){pos=j+1;break;}
   pos=reverse?(pos+n)%(n+1):(pos+1)%(n+1);
   if(pos)raise_panel(ids[pos-1]);else rrui_tui_focus_chat();
   tui_redraw_request();
}
static size_t list_page(GArray *h,size_t field) {
   size_t n=0;
   if(h)for(guint j=0;j<h->len;j++){
      hit_t row=g_array_index(h,hit_t,j);
      if(row.field==field&&row.item!=SIZE_MAX)n++;
   }
   return n;
}
static void select_user(rrui_form_t *f,size_t field,size_t item,size_t span){
   rrui_field_t old={0};
   if(!rrui_form_field(f,field,&old))return;
   if(rrui_form_select(f,field,item)){
      rrui_form_list_view(f,field,span,true);
      rrui_form_selection_changed(f,field,old.selected);
   }
}
static void list_step(rrui_form_t *f,size_t i,int direction,size_t step) {
   rrui_field_t x;
   if(!rrui_form_field(f,i,&x)||x.widget->kind!=RRUI_LIST||!x.count)return;
   rrui_form_list_move(f,i,direction,step,false);
   rrui_form_selection_changed(f,i,x.selected);
}
static bool wheel_list(rrui_form_t *f,GArray *h,int row,int button) {
   if(!h)return false;
   for(guint j=0;j<h->len;j++){
      hit_t x=g_array_index(h,hit_t,j);
      if(x.row==row&&x.item!=SIZE_MAX){
         if(rrui_form_list_scroll(f,x.field,list_page(h,x.field),button==4?-3:3))tui_redraw_request();
         return true;
      }
   }
   return false;
}
static void digit_click(rrui_form_t *f,hit_t h,int column) {
   size_t position=(size_t)(column-h.left);
   if (!rrui_form_digit_select(f,position)) { return; }
   gint64 now=g_get_monotonic_time();
   bool twice=digit_click_form==f && digit_click_field==h.field &&
      digit_click_position==position && now-digit_click_time<=400000;
   digit_click_form=twice?NULL:f;
   digit_click_field=h.field;digit_click_position=position;digit_click_time=now;
   if (twice) { rrui_form_digit_zero(f); }
}
bool rrui_tui_key(TermKey *tk,const TermKeyKey *key) {
   tab_form_t *tab=!active?tab_find(tui_active_window()):NULL;
   if(tab){
      if(key->modifiers&TERMKEY_KEYMOD_ALT)return false;
      if(key->modifiers&TERMKEY_KEYMOD_CTRL&&key->type==TERMKEY_TYPE_KEYSYM&&key->code.sym==TERMKEY_SYM_TAB)return false;
      if(key->type==TERMKEY_TYPE_MOUSE){
         TermKeyMouseEvent ev;int b,r,c;TermKeyKey copy=*key;
         if(termkey_interpret_mouse(tk,&copy,&ev,&b,&r,&c)!=TERMKEY_RES_KEY)return true;
         if(ev!=TERMKEY_MOUSE_PRESS||r<2||!tab->hits)return true;
         r--;
         if(b==4||b==5){wheel_list(tab->form,tab->hits,r,b);return true;}
         if(b!=1)return true;
         for(guint j=0;j<tab->hits->len;j++){
            hit_t h=g_array_index(tab->hits,hit_t,j);
            if(h.row!=r||(h.right&&(c<h.left||c>h.right)))continue;
            rrui_form_t *f=tab->form;
            if(!rrui_form_set_focus(f,h.field))break;
            rrui_field_t z;rrui_form_field(f,h.field,&z);
            if(h.digits) { digit_click(f,h,c); }
            else if(h.slider)rrui_form_slider_position(f,z.widget->vertical?h.slider_pos:(unsigned)(c-h.left),h.slider_span);
            else if(h.minus||h.plus){if(abs(c-h.minus)<=1)rrui_form_adjust(f,-1);else if(abs(c-h.plus)<=1)rrui_form_adjust(f,1);}
            else if(h.item!=SIZE_MAX){select_user(f,h.field,h.item,list_page(tab->hits,h.field));}
            else if(z.widget->kind==RRUI_BUTTON||z.widget->kind==RRUI_CHECKBOX)rrui_form_activate(f);
            break;
         }
         tui_redraw_request();return true;
      }
      if(key->type==TERMKEY_TYPE_KEYSYM&&key->code.sym==TERMKEY_SYM_ESCAPE){tui_window_focus("Status");tui_redraw_request();return true;}
      /* Dispatch directly to the form below. Never masquerade as a modal:
       * actions may open an actual modal, and page size comes from this tab. */
   }
   if(!tab&&!active&&key->type==TERMKEY_TYPE_FUNCTION&&
      (key->code.number==6||key->code.number==18)&&
      !(key->modifiers&(TERMKEY_KEYMOD_CTRL|TERMKEY_KEYMOD_ALT))){
      cycle_panel(key->code.number==18||(key->modifiers&TERMKEY_KEYMOD_SHIFT));return true;
   }
   /* Global tab shortcuts must work even while a side panel owns focus. */
   if(!active&&(key->modifiers&TERMKEY_KEYMOD_ALT)&&key->type==TERMKEY_TYPE_KEYSYM&&
      (key->code.sym==TERMKEY_SYM_LEFT||key->code.sym==TERMKEY_SYM_RIGHT))return false;
   if(!tab&&!active&&npanels) {
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
         if(ev==TERMKEY_MOUSE_PRESS&&(b==4||b==5)){
            for(size_t j=npanels;j>0;j--){
               panel_t *p=&panels[j-1];rrui_rect_t a=p->rect;
               if(a.width>0&&a.height>0&&r>=a.y&&r<a.y+a.height&&c>=a.x&&c<a.x+a.width){
                  wheel_list(p->form,p->hits,r-a.y,b);return true;
               }
            }
            return false;
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
               if(h.row+a.y!=r||(h.right&&(c-a.x<h.left||c-a.x>h.right)))continue;
               if(rrui_form_set_focus(p->form,h.field)){
                  if(h.digits) { digit_click(p->form,h,c-a.x); }
                  else if(h.slider){rrui_field_t z;rrui_form_field(p->form,h.field,&z);rrui_form_slider_position(p->form,z.widget->vertical?h.slider_pos:(unsigned)(c-a.x-h.left),h.slider_span);}
                  else if(h.minus||h.plus){int col=c-a.x;if(abs(col-h.minus)<=1)rrui_form_adjust(p->form,-1);else if(abs(col-h.plus)<=1)rrui_form_adjust(p->form,1);}
                  else if(h.item!=SIZE_MAX){select_user(p->form,h.field,h.item,list_page(p->hits,h.field));}
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
   rrui_form_t *f=tab?tab->form:active?active:focused;
   if(!f)return false;
   size_t panel=panel_index(f);
   GArray *local_hits=tab?tab->hits:active?modal_hits:panel!=SIZE_MAX?panels[panel].hits:NULL;
   size_t i=rrui_form_focus(f);rrui_field_t x={0};rrui_form_field(f,i,&x);
   if(key->type==TERMKEY_TYPE_MOUSE) {
      TermKeyMouseEvent ev;int b,r,c;TermKeyKey copy=*key;
      if(!active)return true;
      if(local_hits&&termkey_interpret_mouse(tk,&copy,&ev,&b,&r,&c)==TERMKEY_RES_KEY&&ev==TERMKEY_MOUSE_PRESS) {
         if(b==4||b==5){wheel_list(f,local_hits,r,b);return true;}
         if(b!=1)return true;
         for(guint j=0;j<local_hits->len;j++) {
            hit_t h=g_array_index(local_hits,hit_t,j);
            if(h.row!=r||(h.right&&(c<h.left||c>h.right)))continue;
            if(!rrui_form_set_focus(f,h.field))break;
            if(h.digits) { digit_click(f,h,c); }
            else if(h.slider){rrui_field_t z;rrui_form_field(f,h.field,&z);rrui_form_slider_position(f,z.widget->vertical?h.slider_pos:(unsigned)(c-h.left),h.slider_span);}
            else if(h.minus||h.plus){if(abs(c-h.minus)<=1)rrui_form_adjust(f,-1);else if(abs(c-h.plus)<=1)rrui_form_adjust(f,1);}
            else if(h.item!=SIZE_MAX){select_user(f,h.field,h.item,list_page(local_hits,h.field));}
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
         case TERMKEY_SYM_LEFT:
         case TERMKEY_SYM_RIGHT:{
            int dir=key->code.sym==TERMKEY_SYM_LEFT?-1:1;
            if(x.widget&&x.widget->kind==RRUI_ENTRY){
               if(key->modifiers&TERMKEY_KEYMOD_CTRL)rrui_form_move_word(f,dir);
               else rrui_form_edit(f,dir,false);
            }else if(x.widget&&(x.widget->kind==RRUI_STEPPER||x.widget->kind==RRUI_SLIDER))rrui_form_adjust(f,dir);
            else if(!(key->modifiers&TERMKEY_KEYMOD_CTRL))rrui_form_next(f,dir);
            break;
         }
         case TERMKEY_SYM_HOME:
         case TERMKEY_SYM_END:
            if(x.widget&&x.widget->kind==RRUI_LIST&&x.count)
               {for(size_t k=0;k<x.count;k++)if(rrui_form_list_selectable(f,i,key->code.sym==TERMKEY_SYM_HOME?k:x.count-1-k)){select_user(f,i,key->code.sym==TERMKEY_SYM_HOME?k:x.count-1-k,list_page(local_hits,i));break;}}
            else if(x.widget&&x.widget->kind==RRUI_ENTRY)
               while(rrui_form_edit(f,key->code.sym==TERMKEY_SYM_HOME?-1:1,false)){}
            break;
         case TERMKEY_SYM_PAGEUP:
         case TERMKEY_SYM_PAGEDOWN:
            list_step(f,i,key->code.sym==TERMKEY_SYM_PAGEUP?-1:1,MAX((size_t)1,list_page(local_hits,i)));
            if(list_page(local_hits,i))rrui_form_list_view(f,i,list_page(local_hits,i),true);break;
         case TERMKEY_SYM_BACKSPACE:rrui_form_edit(f,-1,true);break;
         case TERMKEY_SYM_DELETE:rrui_form_edit(f,1,true);break;
         case TERMKEY_SYM_UP:
         case TERMKEY_SYM_DOWN:
            if(x.widget&&x.widget->kind==RRUI_LIST&&x.count) {
               rrui_field_t root={0};rrui_form_field(f,0,&root);
               bool wrap=active&&root.widget&&!strcmp(root.widget->id,"servers");
               rrui_form_list_move(f,i,key->code.sym==TERMKEY_SYM_UP?-1:1,1,wrap);if(list_page(local_hits,i))rrui_form_list_view(f,i,list_page(local_hits,i),true);rrui_form_selection_changed(f,i,x.selected);
            } else if(x.widget&&x.widget->kind==RRUI_SLIDER&&x.widget->vertical)rrui_form_adjust(f,key->code.sym==TERMKEY_SYM_UP?1:-1);
            else rrui_form_next(f,key->code.sym==TERMKEY_SYM_UP?-1:1);
            break;
         default:break;
      }
   } else if(key->type==TERMKEY_TYPE_UNICODE&&!(key->modifiers&(TERMKEY_KEYMOD_CTRL|TERMKEY_KEYMOD_ALT))) {
      if(x.widget&&(x.widget->kind==RRUI_STEPPER||x.widget->kind==RRUI_SLIDER)&&(key->code.codepoint=='+'||key->code.codepoint=='-'))
         rrui_form_adjust(f,key->code.codepoint=='+'?1:-1);
      else if(key->code.codepoint==' '&&x.widget&&x.widget->kind!=RRUI_ENTRY)rrui_form_activate(f);
      else if(key->code.codepoint=='\t')rrui_form_next(f,key->modifiers&TERMKEY_KEYMOD_SHIFT?-1:1);
      else rrui_form_insert(f,key->code.codepoint);
   }
   tui_redraw_request();return true;
}
