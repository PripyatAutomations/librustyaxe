/* SPDX-License-Identifier: MIT */
#include <librustyui/form.h>
#include <glib.h>
#include <string.h>

typedef struct { char *key,*label; uint32_t color; bool header; } item_t;
typedef struct {
   rrui_widget_t w;
   char *value;
   bool checked,visible,interactive;
   size_t cursor,selected,top;
   bool reveal_selection;
   GPtrArray *items;
} field_t;
struct rrui_form {
   GPtrArray *fields;
   size_t focus;
   rrui_action_fn action,selection;
   void *user;
};
static void item_free(gpointer p) {
   item_t *i=p; g_free(i->key); g_free(i->label); g_free(i);
}
static void field_free(gpointer p) {
   field_t *f=p;
   g_free((void *)f->w.id);g_free((void *)f->w.parent);
   g_free((void *)f->w.label);g_free((void *)f->w.action);
   g_free((void *)f->w.value);g_free(f->value);
   g_ptr_array_unref(f->items);g_free(f);
}
static field_t *at(const rrui_form_t *f,size_t n) {
   return f&&n<f->fields->len?g_ptr_array_index(f->fields,n):NULL;
}
static field_t *find(const rrui_form_t *f,const char *id) {
   if(!id)return NULL;
   for(size_t i=0;i<rrui_form_count(f);i++)if(!strcmp(at(f,i)->w.id,id))return at(f,i);
   return NULL;
}
size_t rrui_form_count(const rrui_form_t *f){return f?f->fields->len:0;}
rrui_form_t *rrui_form_new(const rrui_layout_t *l,rrui_action_fn action,void *user) {
   if(!l||!rrui_layout_count(l))return NULL;
   rrui_form_t *f=g_new0(rrui_form_t,1);
   f->fields=g_ptr_array_new_with_free_func(field_free);
   f->focus=SIZE_MAX;f->action=action;f->user=user;
   for(size_t i=0;i<rrui_layout_count(l);i++) {
      const rrui_widget_t *w=rrui_layout_at(l,i);
      field_t *x=g_new0(field_t,1);x->w=*w;
      x->w.id=g_strdup(w->id);x->w.parent=g_strdup(w->parent);
      x->w.label=g_strdup(w->label);x->w.action=g_strdup(w->action);
      x->w.value=g_strdup(w->value);x->value=g_strdup(w->value);
      x->checked=w->checked;x->visible=w->visible;x->cursor=strlen(x->value);
      if (w->kind==RRUI_DIGITS) {
         if (*x->value) {
            char *padded=g_strdup_printf("%0*" G_GUINT64_FORMAT,(int)w->width_chars,g_ascii_strtoull(x->value,NULL,10));
            g_free(x->value);x->value=padded;
         }
         x->cursor=w->width_chars-1;
      }
      bool enabled=w->enabled;
      for(field_t *p=find(f,w->parent);p;p=find(f,p->w.parent)) {
         x->visible=x->visible&&p->w.visible;enabled=enabled&&p->w.enabled;
      }
      x->interactive=x->visible&&enabled&&(w->kind==RRUI_ENTRY||w->kind==RRUI_DIGITS||w->kind==RRUI_CHECKBOX||
                         w->kind==RRUI_LIST||w->kind==RRUI_BUTTON||w->kind==RRUI_STEPPER||w->kind==RRUI_SLIDER);
      x->items=g_ptr_array_new_with_free_func(item_free);x->selected=SIZE_MAX;
      g_ptr_array_add(f->fields,x);
      if(x->interactive&&f->focus==SIZE_MAX)f->focus=i;
   }
   return f;
}
static void (*detach_hook)(rrui_form_t *);
void rrui_form_set_detach_hook(void (*hook)(rrui_form_t *)) { detach_hook=hook; }
void rrui_form_free(rrui_form_t *f) {
   if(!f)return;
   if (detach_hook) { detach_hook(f); }g_ptr_array_unref(f->fields);g_free(f);
}
bool rrui_form_field(const rrui_form_t *f,size_t i,rrui_field_t *out) {
   field_t *x=at(f,i);if(!x||!out)return false;
   *out=(rrui_field_t){&x->w,x->value,x->checked,x->interactive,x->visible,
                       x->cursor,x->items->len,x->selected};return true;
}
size_t rrui_form_focus(const rrui_form_t *f){return f?f->focus:SIZE_MAX;}
bool rrui_form_set_focus(rrui_form_t *f,size_t i) {
   field_t *x=at(f,i);if(!x||!x->interactive)return false;f->focus=i;return true;
}
bool rrui_form_next(rrui_form_t *f,int dir) {
   size_t n=rrui_form_count(f);if(!n)return false;
   size_t i=f->focus<n?f->focus:(dir<0?0:n-1);
   for(size_t k=0;k<n;k++) {
      i=dir<0?(i?i-1:n-1):(i+1)%n;
      if(rrui_form_set_focus(f,i))return true;
   }
   return false;
}
const char *rrui_form_value(const rrui_form_t *f,const char *id) {
   field_t *x=find(f,id);return x?x->value:NULL;
}
static bool valid_text(const char *s) {
   if(!s||!g_utf8_validate(s,-1,NULL))return false;
   for(const char *p=s;*p;p=g_utf8_next_char(p))if(g_unichar_iscntrl(g_utf8_get_char(p)))return false;
   return true;
}
bool rrui_form_set_value(rrui_form_t *f,const char *id,const char *v) {
   field_t *x=find(f,id);
   if(!x||(x->w.kind!=RRUI_ENTRY&&x->w.kind!=RRUI_DIGITS&&x->w.kind!=RRUI_LABEL&&x->w.kind!=RRUI_STEPPER&&x->w.kind!=RRUI_SLIDER)||!valid_text(v)||g_utf8_strlen(v,-1)>(glong)x->w.max_length)return false;
   if((x->w.kind==RRUI_STEPPER||x->w.kind==RRUI_SLIDER)&&*v){
      char *end=NULL;gint64 n=g_ascii_strtoll(v,&end,10);
      if(!end||*end||n<x->w.minimum||n>x->w.maximum)return false;
   }
   if (x->w.kind==RRUI_DIGITS) {
      if (strspn(v,"0123456789")!=strlen(v)) { return false; }
      char *copy=*v?g_strdup_printf("%0*" G_GUINT64_FORMAT,(int)x->w.width_chars,g_ascii_strtoull(v,NULL,10)):g_strdup("");
      g_free(x->value);x->value=copy;
      x->cursor=MIN(x->cursor,x->w.width_chars-1);return true;
   }
   char *copy=g_strdup(v);g_free(x->value);x->value=copy;x->cursor=strlen(copy);return true;
}
bool rrui_form_adjust(rrui_form_t *f,int direction) {
   field_t *x=at(f,rrui_form_focus(f));
   if(!x||!x->interactive||(x->w.kind!=RRUI_STEPPER&&x->w.kind!=RRUI_SLIDER)||!*x->value||!direction)return false;
   gint64 old=g_ascii_strtoll(x->value,NULL,10);
   gint64 n=CLAMP(old+(direction<0?-(gint64)x->w.step:(gint64)x->w.step),x->w.minimum,x->w.maximum);
   if(n==old)return false;
   char *value=g_strdup_printf("%" G_GINT64_FORMAT,n);
   g_free(x->value);x->value=value;x->cursor=strlen(value);
   if(*x->w.action&&f->action)f->action(f,x->w.id,x->w.action,f->user);
   return true;
}
bool rrui_form_set_orientation(rrui_form_t *f,const char *id,bool vertical) {
   field_t *x=find(f,id);
   if(!x||x->w.kind!=RRUI_SLIDER)return false;
   x->w.vertical=vertical;
   return true;
}
bool rrui_form_slider_position(rrui_form_t *f,unsigned position,unsigned span) {
   field_t *x=at(f,rrui_form_focus(f));
   if(!x||!x->interactive||x->w.kind!=RRUI_SLIDER||!*x->value||!span||position>span)return false;
   gint64 range=(gint64)x->w.maximum-x->w.minimum;
   /* Floating interpolation avoids overflow at extreme integer ranges. */
   gint64 offset=(gint64)((double)range*position/span+0.5);
   offset=position==span?range:MIN(range,((offset+x->w.step/2)/x->w.step)*x->w.step);
   gint64 value=x->w.minimum+offset;
   if(value==g_ascii_strtoll(x->value,NULL,10))return false;
   char *text=g_strdup_printf("%" G_GINT64_FORMAT,value);
   g_free(x->value);x->value=text;x->cursor=strlen(text);
   if(*x->w.action&&f->action)f->action(f,x->w.id,x->w.action,f->user);
   return true;
}
bool rrui_form_checked(const rrui_form_t *f,const char *id){field_t *x=find(f,id);return x&&x->checked;}
bool rrui_form_set_checked(rrui_form_t *f,const char *id,bool b) {
   field_t *x=find(f,id);if(!x||x->w.kind!=RRUI_CHECKBOX)return false;x->checked=b;return true;
}
bool rrui_form_list_clear(rrui_form_t *f,const char *id) {
   field_t *x=find(f,id);if(!x||x->w.kind!=RRUI_LIST)return false;
   g_ptr_array_set_size(x->items,0);x->selected=SIZE_MAX;
   /* Preserve desired key until matching item is appended, or host selects. */
   return true;
}
bool rrui_form_list_append_color(rrui_form_t *f,const char *id,const char *key,const char *label,uint32_t color) {
   field_t *x=find(f,id);if(!x||x->w.kind!=RRUI_LIST||!key||!*key||!valid_text(label))return false;
   for(guint i=0;i<x->items->len;i++)if(!strcmp(((item_t *)g_ptr_array_index(x->items,i))->key,key))return false;
   item_t *it=g_new0(item_t,1);it->key=g_strdup(key);it->label=g_strdup(label);it->color=color<=0xffffff?color:UINT32_MAX;
   if(!strcmp(x->value,key))x->selected=x->items->len;
   g_ptr_array_add(x->items,it);
   return true;
}
bool rrui_form_list_append(rrui_form_t *f,const char *id,const char *key,const char *label) {
   return rrui_form_list_append_color(f,id,key,label,UINT32_MAX);
}
bool rrui_form_list_header(rrui_form_t *f,const char *id,const char *key,const char *label){
   if(!rrui_form_list_append_color(f,id,key,label,0x8baab8))return false;
   field_t *x=find(f,id);
   item_t *it=g_ptr_array_index(x->items,x->items->len-1);it->header=true;
   if(x->selected==x->items->len-1)x->selected=SIZE_MAX;
   return true;
}
bool rrui_form_list_selectable(const rrui_form_t *f,size_t i,size_t j){
   field_t *x=at(f,i);
   return x&&x->w.kind==RRUI_LIST&&j<x->items->len&&!((item_t *)g_ptr_array_index(x->items,j))->header;
}
bool rrui_form_list_move(rrui_form_t *f,size_t i,int dir,size_t steps,bool wrap){
   field_t *x=at(f,i);
   if(!x||x->w.kind!=RRUI_LIST||!x->items->len||!dir||!steps)return false;
   size_t count=x->items->len,n=x->selected,chosen=n;
   for(size_t k=0;k<count && steps;k++){
      if(n>=count)n=dir>0?0:count-1;
      else if(dir>0){if(n+1==count&&!wrap)break;n=(n+1)%count;}
      else {if(!n&&!wrap)break;n=n?n-1:count-1;}
      if(rrui_form_list_selectable(f,i,n)){chosen=n;steps--;}
   }
   return chosen<count&&rrui_form_select(f,i,chosen);
}
uint32_t rrui_form_list_color(const rrui_form_t *f,size_t i,size_t j) {
   field_t *x=at(f,i);return x&&j<x->items->len?((item_t *)g_ptr_array_index(x->items,j))->color:UINT32_MAX;
}
const char *rrui_form_list_label(const rrui_form_t *f,size_t i,size_t j) {
   field_t *x=at(f,i);return x&&j<x->items->len?((item_t *)g_ptr_array_index(x->items,j))->label:NULL;
}
bool rrui_form_select(rrui_form_t *f,size_t i,size_t j) {
   field_t *x=at(f,i);if(!x||!rrui_form_list_selectable(f,i,j))return false;
   item_t *it=g_ptr_array_index(x->items,j);g_free(x->value);x->value=g_strdup(it->key);x->selected=j;x->reveal_selection=true;return true;
}
void rrui_form_on_selection(rrui_form_t *f,rrui_action_fn fn){if(f)f->selection=fn;}
void rrui_form_selection_changed(rrui_form_t *f,size_t field,size_t previous){
   field_t *x=at(f,field);
   if(x&&f->selection&&x->w.kind==RRUI_LIST&&x->selected!=previous&&
      rrui_form_list_selectable(f,field,x->selected))
      f->selection(f,x->w.id,"selection",f->user);
}
/* Keep viewport independent from selection. Only explicit navigation follows
 * the selected row; scrolling an inactive list must not change its value. */
size_t rrui_form_list_view(rrui_form_t *f,size_t i,size_t span,bool follow) {
   field_t *x=at(f,i);if(!x||x->w.kind!=RRUI_LIST||!span)return 0;
   size_t count=x->items->len,limit=count>span?count-span:0;
   x->top=MIN(x->top,limit);
   if((follow||x->reveal_selection)&&x->selected<count){
      if(x->selected<x->top)x->top=x->selected;
      else if(x->selected-x->top>=span)x->top=x->selected-span+1;
   }
   x->reveal_selection=false;
   return x->top;
}
bool rrui_form_list_scroll(rrui_form_t *f,size_t i,size_t span,int delta) {
   field_t *x=at(f,i);if(!x||x->w.kind!=RRUI_LIST||!span)return false;
   size_t top=rrui_form_list_view(f,i,span,false);
   size_t limit=x->items->len>span?x->items->len-span:0;
   if(delta<0)x->top=top-MIN(top,(size_t)(-(int64_t)delta));
   else x->top=top+MIN(limit-top,(size_t)delta);
   return top!=x->top;
}
static field_t *digit_field(rrui_form_t *f) {
   field_t *x=at(f,rrui_form_focus(f));
   return x && x->interactive && x->w.kind==RRUI_DIGITS && *x->value?x:NULL;
}
bool rrui_form_digit_select(rrui_form_t *f,size_t position) {
   field_t *x=digit_field(f);
   if (!x || position>=x->w.width_chars) { return false; }
   x->cursor=position;return true;
}
bool rrui_form_digit_zero(rrui_form_t *f) {
   field_t *x=digit_field(f);
   if (!x || x->value[x->cursor]=='0') { return false; }
   x->value[x->cursor]='0';
   return rrui_form_activate(f);
}
bool rrui_form_digit_adjust(rrui_form_t *f,int direction) {
   field_t *x=digit_field(f);
   if (!x || !direction) { return false; }
   guint64 place=1,limit=1;
   for (unsigned i=0;i<x->w.width_chars;i++) {
      limit*=10;
      if (i>x->cursor) { place*=10; }
   }
   guint64 old=g_ascii_strtoull(x->value,NULL,10);
   guint64 next=direction>0?MIN(limit-1,old+place):old>=place?old-place:0;
   if (next==old) { return false; }
   char *value=g_strdup_printf("%0*" G_GUINT64_FORMAT,(int)x->w.width_chars,next);
   g_free(x->value);x->value=value;
   return rrui_form_activate(f);
}
bool rrui_form_insert(rrui_form_t *f,uint32_t c) {
   field_t *digits=digit_field(f);
   if (digits) {
      if (c<'0' || c>'9') { return false; }
      bool changed=digits->value[digits->cursor]!=(char)c;
      digits->value[digits->cursor]=(char)c;
      digits->cursor=(digits->cursor+1)%digits->w.width_chars;
      /* A host callback may refresh or replace the form; do not access
       * the field after invoking it. */
      if (changed) { rrui_form_activate(f); }
      return true;
   }
   field_t *x=at(f,rrui_form_focus(f));
   if(!x||x->w.kind!=RRUI_ENTRY||!x->interactive||!c||!g_unichar_validate(c)||g_unichar_iscntrl(c)||
      g_utf8_strlen(x->value,-1)>=(glong)x->w.max_length)return false;
   char buf[7]={0};int n=g_unichar_to_utf8(c,buf);
   GString *s=g_string_new(x->value);g_string_insert_len(s,x->cursor,buf,n);
   g_free(x->value);x->value=g_string_free(s,FALSE);x->cursor+=n;return true;
}
bool rrui_form_edit(rrui_form_t *f,int dir,bool erase) {
   field_t *digits=digit_field(f);
   if (digits) {
      if (erase) {
         if (digits->value[digits->cursor]!='0') {
            digits->value[digits->cursor]='0';
            rrui_form_activate(f);
         }
         return true;
      }
      digits->cursor=(digits->cursor+digits->w.width_chars+(dir<0?-1:1))%digits->w.width_chars;
      return true;
   }
   field_t *x=at(f,rrui_form_focus(f));if(!x||x->w.kind!=RRUI_ENTRY||!x->interactive)return false;
   char *p=x->value+x->cursor,*q;
   if(dir<0){if(!x->cursor)return false;q=g_utf8_prev_char(p);}
   else {if(!*p)return false;q=g_utf8_next_char(p);}
   if(erase){char *a=dir<0?q:p,*b=dir<0?p:q;memmove(a,b,strlen(b)+1);x->cursor=a-x->value;}
   else x->cursor=q-x->value;
   return true;
}
bool rrui_form_move_word(rrui_form_t *f,int dir) {
   field_t *x=at(f,rrui_form_focus(f));if(!x||x->w.kind!=RRUI_ENTRY||!x->interactive)return false;
   const char *start=x->value,*p=start+x->cursor,*old=p;
   if(dir<0){
      while(p>start&&g_unichar_isspace(g_utf8_get_char(g_utf8_prev_char(p))))p=g_utf8_prev_char(p);
      while(p>start&&!g_unichar_isspace(g_utf8_get_char(g_utf8_prev_char(p))))p=g_utf8_prev_char(p);
   }else{
      while(*p&&!g_unichar_isspace(g_utf8_get_char(p)))p=g_utf8_next_char(p);
      while(*p&&g_unichar_isspace(g_utf8_get_char(p)))p=g_utf8_next_char(p);
   }
   x->cursor=p-start;return p!=old;
}
bool rrui_form_activate(rrui_form_t *f) {
   field_t *x=at(f,rrui_form_focus(f));if(!x||!x->interactive)return false;
   if(x->w.kind==RRUI_STEPPER||x->w.kind==RRUI_SLIDER)return false;
   if(x->w.kind==RRUI_LIST&&!rrui_form_list_selectable(f,f->focus,x->selected))return false;
   if(x->w.kind==RRUI_CHECKBOX)x->checked=!x->checked;
   if(*x->w.action&&f->action)f->action(f,x->w.id,x->w.action,f->user);
   return true;
}
void rrui_form_cancel(rrui_form_t *f) {
   if(!f)return;
   if (detach_hook) { detach_hook(f); }
   if(f->action)f->action(f,"","dialog.close",f->user);
}
