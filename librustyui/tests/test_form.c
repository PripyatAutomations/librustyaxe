/* SPDX-License-Identifier: MIT */
#include <librustyui/form.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int called;
static void action(rrui_form_t *f,const char *id,const char *name,void *user) {
   (void)f;(void)user;assert(!strcmp(id,"save"));assert(!strcmp(name,"save"));called++;
}
int main(void) {
   rrui_layout_t *l=rrui_layout_new(RRUI_TUI);
   assert(rrui_layout_load(l,
      "ui.add(dict {kind=\"dialog\";id=\"d\";});"
      "ui.add(dict {kind=\"entry\";id=\"e\";parent=\"d\";max_length=2;});"
      "ui.add(dict {kind=\"checkbox\";id=\"c\";parent=\"d\";});"
      "ui.add(dict {kind=\"list\";id=\"l\";parent=\"d\";});"
      "ui.add(dict {kind=\"button\";id=\"save\";parent=\"d\";action=\"save\";});"
      "ui.add(dict {kind=\"button\";id=\"hidden\";parent=\"d\";visible=false;});"));
   rrui_form_t *f=rrui_form_new(l,action,NULL);assert(f);rrui_layout_free(l);
   assert(rrui_form_focus(f)==1);
   assert(rrui_form_insert(f,0x00e9));assert(rrui_form_insert(f,'x'));assert(!rrui_form_insert(f,'y'));
   assert(rrui_form_edit(f,-1,false));assert(rrui_form_edit(f,-1,true));assert(!strcmp(rrui_form_value(f,"e"),"x"));
   assert(!rrui_form_set_value(f,"e","\033[2J"));
   assert(rrui_form_next(f,1)&&rrui_form_focus(f)==2);
   assert(rrui_form_activate(f)&&rrui_form_checked(f,"c"));
   assert(rrui_form_list_append_color(f,"l","one","One",0x123456));assert(rrui_form_list_color(f,3,0)==0x123456);assert(rrui_form_list_color(f,3,99)==UINT32_MAX);assert(rrui_form_list_append(f,"l","two","Two"));
   assert(rrui_form_select(f,3,1));assert(!strcmp(rrui_form_value(f,"l"),"two"));
   assert(rrui_form_list_clear(f,"l"));assert(rrui_form_list_append(f,"l","two","Two"));
   rrui_field_t field;assert(rrui_form_field(f,3,&field)&&field.selected==0);
   assert(rrui_form_set_focus(f,4));assert(rrui_form_activate(f)&&called==1);
   assert(rrui_form_next(f,1)&&rrui_form_focus(f)==1);
   rrui_form_free(f);puts("PASS: form snapshot, UTF-8 editing, focus, keyed lists and actions");return 0;
}
