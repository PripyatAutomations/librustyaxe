/* SPDX-License-Identifier: MIT */
#include <librustyui/ui.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
   rrui_layout_t *l = rrui_layout_new(RRUI_TUI);
   assert(l);
   const char *source =
      "ui.add({kind:\"dialog\",id:\"server\",label:\"Server\"});"
      "dict field = {kind:\"entry\",id:\"host\",parent:\"server\"};"
      "field .= {width_chars = 20; max_length = 180; label = \"Host\";};"
      "ui.add(field);"
      "if (ui.capability(\"graphical\")) { ui.add({kind:\"label\",id:\"graph\",parent:\"server\"}); }";
   if (!rrui_layout_load(l,source)) { fprintf(stderr,"%s\n",rrui_layout_error(l)); return 1; }
   assert(rrui_layout_count(l)==2);
   const rrui_widget_t *w=rrui_layout_find(l,"host");
   assert(w && w->width_chars==20 && w->max_length==180 && w->enabled && w->visible);
   assert(!strcmp(w->label,"Host"));
   assert(!rrui_layout_load(l,"ui.add({kind:\"entry\",id:\"bad\",parent:\"missing\"});"));
   assert(rrui_layout_find(l,"host")==w);
   assert(!rrui_layout_load(l,"ui.add({kind:\"dialog\",id:\"x\"}); ui.add({kind:\"panel\",id:\"x\"});"));
   assert(rrui_layout_count(l)==2);
   assert(!rrui_layout_load(l,"ui.add({kind:\"dialog\",id:\"x\",width_chars:-1});"));
   assert(!rrui_layout_load(l,"ui.add({kind:\"dialog\",id:\"x\"}); missing();"));
   assert(rrui_layout_count(l)==2);
   rrui_layout_free(l);
   l=rrui_layout_new(RRUI_GTK3);
   assert(rrui_layout_load(l,source));
   assert(rrui_layout_count(l)==3 && rrui_layout_find(l,"graph"));
   assert(!rrui_capability(RRUI_TUI,"spectrum"));
   rrui_layout_free(l);
   puts("PASS: shared RustyScript layout, property blocks, backend selection and atomic reload");
   return 0;
}
