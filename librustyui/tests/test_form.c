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
   assert(!rrui_form_move_word(f,-1));
   assert(rrui_form_move_word(f,1));assert(!rrui_form_move_word(f,1));
   assert(rrui_form_move_word(f,-1));
   assert(!rrui_form_set_value(f,"e","\033[2J"));
   assert(rrui_form_next(f,1)&&rrui_form_focus(f)==2);
   assert(rrui_form_activate(f)&&rrui_form_checked(f,"c"));
   assert(rrui_form_list_append_color(f,"l","one","One",0x123456));assert(rrui_form_list_color(f,3,0)==0x123456);assert(rrui_form_list_color(f,3,99)==UINT32_MAX);assert(rrui_form_list_append(f,"l","two","Two"));
   assert(rrui_form_select(f,3,1));assert(!strcmp(rrui_form_value(f,"l"),"two"));
   assert(rrui_form_list_clear(f,"l"));assert(rrui_form_list_append(f,"l","two","Two"));
   rrui_field_t field;assert(rrui_form_field(f,3,&field)&&field.selected==0);
   for(int j=0;j<20;j++){char key[16];snprintf(key,sizeof(key),"item%d",j);assert(rrui_form_list_append(f,"l",key,key));}
   assert(rrui_form_select(f,3,0));
   assert(rrui_form_list_view(f,3,5,true)==0);
   assert(rrui_form_list_scroll(f,3,5,3));
   assert(rrui_form_list_view(f,3,5,false)==3);
   assert(rrui_form_field(f,3,&field)&&field.selected==0);
   assert(rrui_form_list_scroll(f,3,5,100));
   assert(rrui_form_list_view(f,3,5,false)==16);
   assert(!rrui_form_list_scroll(f,3,5,1));
   assert(rrui_form_list_view(f,3,5,true)==0);
   assert(rrui_form_select(f,3,10));
   assert(rrui_form_list_view(f,3,5,true)==6);
   assert(rrui_form_select(f,3,9));
   assert(rrui_form_list_view(f,3,5,true)==6);
   assert(rrui_form_list_clear(f,"l"));
   assert(rrui_form_list_header(f,"l","@group","Group"));
   assert(rrui_form_list_append(f,"l","a","A"));
   assert(rrui_form_list_header(f,"l","@other","Other"));
   assert(rrui_form_list_append(f,"l","b","B"));
   assert(!rrui_form_select(f,3,0));
   assert(rrui_form_list_move(f,3,1,1,false)&&!strcmp(rrui_form_value(f,"l"),"a"));
   assert(rrui_form_list_move(f,3,1,1,false)&&!strcmp(rrui_form_value(f,"l"),"b"));
   assert(rrui_form_list_move(f,3,1,1,true)&&!strcmp(rrui_form_value(f,"l"),"a"));
   assert(rrui_form_set_focus(f,4));assert(rrui_form_activate(f)&&called==1);
   assert(rrui_form_next(f,1)&&rrui_form_focus(f)==1);
   rrui_form_free(f);
   l=rrui_layout_new(RRUI_TUI);
   assert(rrui_layout_load(l,
      "ui.add(dict {kind=\"dialog\";id=\"d\";});"
      "ui.add(dict {kind=\"stepper\";id=\"s\";parent=\"d\";min=0;max=10;step=3;value=\"8\";});"
      "ui.add(dict {kind=\"slider\";id=\"v\";parent=\"d\";min=0;max=100;step=5;value=\"20\";orientation=\"vertical\";});"));
   f=rrui_form_new(l,NULL,NULL);rrui_layout_free(l);
   assert(rrui_form_adjust(f,1)&&!strcmp(rrui_form_value(f,"s"),"10"));
   assert(!rrui_form_adjust(f,1));assert(rrui_form_adjust(f,-1));
   assert(!strcmp(rrui_form_value(f,"s"),"7"));
   assert(rrui_form_set_value(f,"s",""));assert(!rrui_form_adjust(f,1));
   assert(!rrui_form_set_value(f,"s","11"));
   assert(rrui_form_set_focus(f,2));
   assert(rrui_form_field(f,2,&field)&&field.widget->vertical);
   assert(rrui_form_set_orientation(f,"v",false));
   assert(rrui_form_field(f,2,&field)&&!field.widget->vertical&&!strcmp(field.value,"20"));
   assert(rrui_form_slider_position(f,1,3)&&!strcmp(rrui_form_value(f,"v"),"35"));
   assert(rrui_form_slider_position(f,3,3)&&!strcmp(rrui_form_value(f,"v"),"100"));
   rrui_form_free(f);
   l=rrui_layout_new(RRUI_TUI);
   assert(rrui_layout_load(l,"ui.add(dict {kind=\"panel\";id=\"p\";});"
      "ui.add(dict {kind=\"digits\";id=\"hz\";parent=\"p\";width_chars=8;value=\"14074000\";});"));
   f=rrui_form_new(l,NULL,NULL);rrui_layout_free(l);
   assert(rrui_form_digit_select(f,0));
   assert(rrui_form_edit(f,-1,false));
   assert(rrui_form_field(f,1,&field)&&field.cursor==7);
   assert(rrui_form_edit(f,1,false));
   assert(rrui_form_field(f,1,&field)&&field.cursor==0);
   assert(rrui_form_digit_select(f,4));
   assert(rrui_form_digit_adjust(f,1)&&!strcmp(rrui_form_value(f,"hz"),"14075000"));
   assert(rrui_form_digit_zero(f)&&!strcmp(rrui_form_value(f,"hz"),"14070000"));
   assert(rrui_form_insert(f,'9')&&!strcmp(rrui_form_value(f,"hz"),"14079000"));
   assert(!rrui_form_insert(f,'x'));
   assert(rrui_form_set_value(f,"hz","123")&&!strcmp(rrui_form_value(f,"hz"),"00000123"));
   assert(rrui_form_field(f,1,&field)&&field.cursor==5);
   assert(!rrui_form_set_value(f,"hz","-1"));
   assert(rrui_form_set_value(f,"hz","")&&!rrui_form_digit_adjust(f,1));
   rrui_form_free(f);puts("PASS: form snapshot, UTF-8 editing, focus, keyed lists and actions");return 0;
}
