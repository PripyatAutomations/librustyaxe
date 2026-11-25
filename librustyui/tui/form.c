/* SPDX-License-Identifier: MIT */
#include <librustyui/form.h>
#include <librustyui/tui.h>
#include "termkey.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>

static rrui_form_t *active;
static size_t scroll;
typedef struct {size_t field,item;int row;} hit_t;
static GArray *hits;

bool rrui_tui_open(rrui_form_t *f) {
   if(!f||!tui_is_enabled||active)return false;
   active=f;scroll=0;tui_redraw_request();return true;
}
void rrui_tui_close(rrui_form_t *f) {
   if(active&&(!f||active==f)){active=NULL;if(hits){g_array_unref(hits);hits=NULL;}tui_redraw_request();}
}
/* Print plain terminal text only. Count wide characters conservatively. */
static void clipped(const char *s,int width) {
   if(!s)return;
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
}
static void line(int row,int cols,bool focus,const char *prefix,const char *value) {
   printf("\033[%d;1H\033[2K%s",row,focus?"\033[7m":"\033[0m");
   clipped(prefix,cols-1);
   int remain=cols-1-(int)strlen(prefix);
   if(remain>0)clipped(value,remain);
   fputs("\033[0m",stdout);
}
bool rrui_tui_draw(int rows,int cols) {
   if(!active)return false;
   if(!hits)hits=g_array_new(FALSE,FALSE,sizeof(hit_t));
   g_array_set_size(hits,0);
   fputs("\033[0m\033[r\033[?25l\033[H\033[2J",stdout);
   if(rows<4||cols<12){clipped("Terminal too small",cols-1);fflush(stdout);return true;}
   size_t focus=rrui_form_focus(active),count=rrui_form_count(active);
   if(focus<scroll)scroll=focus;
   /* Keep focus in view: containers don't consume lines; list rows do. */
   for(;;) {
      int used=0;
      for(size_t i=scroll;i<focus&&i<count;i++) {
         rrui_field_t x;rrui_form_field(active,i,&x);
         if(!x.visible||x.widget->kind==RRUI_ROW||x.widget->kind==RRUI_COLUMN)continue;
         used+=1;
         if(x.widget->kind==RRUI_LIST)used+=MIN(x.count,(size_t)MAX(1,MIN((int)x.widget->height_rows,rows/2)));
      }
      if(used<rows-3||scroll>=focus||focus>=count)break;
      scroll++;
   }
   int row=1;
   for(size_t i=scroll;i<count&&row<rows;i++) {
      rrui_field_t x;rrui_form_field(active,i,&x);
      const rrui_widget_t *w=x.widget;
      if(!x.visible||w->kind==RRUI_ROW||w->kind==RRUI_COLUMN)continue;
      bool selected=i==focus;
      if(w->kind==RRUI_LIST) {
         line(row++,cols,selected,"  ",w->label);
         size_t span=MAX(1,MIN((int)w->height_rows,rows/2));
         span=MIN(span,(size_t)MAX(0,rows-row));
         size_t start=x.selected!=SIZE_MAX&&x.selected>=span&&span?x.selected-span+1:0;
         for(size_t j=start;j<x.count&&j-start<span&&row<rows;j++) {
            hit_t h={i,j,row};g_array_append_val(hits,h);
            line(row++,cols,selected&&j==x.selected,j==x.selected?" > ":"   ",rrui_form_list_label(active,i,j));
         }
         continue;
      }
      if(x.interactive){hit_t h={i,SIZE_MAX,row};g_array_append_val(hits,h);}
      if(w->kind==RRUI_ENTRY) {
         char *label=g_strdup_printf("  %s: ",w->label);
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
            if(selected){printf("\033[%d;%dH",row,5+cells);}
            g_free(masked);row++;
         }
         g_free(label);
      } else if(w->kind==RRUI_CHECKBOX)line(row++,cols,selected,x.checked?" [x] ":" [ ] ",w->label);
      else if(w->kind==RRUI_BUTTON)line(row++,cols,selected," [ ] ",w->label);
      else line(row++,cols,false,"  ",w->label);
   }
   line(rows,cols,false,"","Tab / Shift-Tab  Arrows  Enter  Esc");
   fflush(stdout);return true;
}
bool rrui_tui_key(TermKey *tk,const TermKeyKey *key) {
   if(!active)return false;
   rrui_form_t *f=active;
   size_t i=rrui_form_focus(f);rrui_field_t x={0};rrui_form_field(f,i,&x);
   if(key->type==TERMKEY_TYPE_MOUSE) {
      TermKeyMouseEvent ev;int b,r,c;TermKeyKey copy=*key;
      if(hits&&termkey_interpret_mouse(tk,&copy,&ev,&b,&r,&c)==TERMKEY_RES_KEY&&ev==TERMKEY_MOUSE_PRESS&&b==1) {
         for(guint j=0;j<hits->len;j++) {
            hit_t h=g_array_index(hits,hit_t,j);
            if(h.row!=r)continue;
            rrui_form_set_focus(f,h.field);
            if(h.item!=SIZE_MAX)rrui_form_select(f,h.field,h.item);
            else {rrui_field_t y;if(rrui_form_field(f,h.field,&y)&&(y.widget->kind==RRUI_BUTTON||y.widget->kind==RRUI_CHECKBOX))rrui_form_activate(f);}
            break;
         }
      }
   } else if(key->type==TERMKEY_TYPE_KEYSYM) {
      switch(key->code.sym) {
         case TERMKEY_SYM_ESCAPE:rrui_form_cancel(f);break;
         case TERMKEY_SYM_TAB:rrui_form_next(f,key->modifiers&TERMKEY_KEYMOD_SHIFT?-1:1);break;
         case TERMKEY_SYM_ENTER:rrui_form_activate(f);break;
         case TERMKEY_SYM_LEFT:rrui_form_edit(f,-1,false);break;
         case TERMKEY_SYM_RIGHT:rrui_form_edit(f,1,false);break;
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
