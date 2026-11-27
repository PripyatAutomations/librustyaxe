/* SPDX-License-Identifier: MIT */
#include <librustyui/tree.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
   rrui_tree_t *t=rrui_tree_new();rrui_tree_row_t row;
   assert(rrui_tree_add(t,"a",NULL,"A",0,true));
   assert(rrui_tree_add(t,"b","a","B",0,false));
   assert(rrui_tree_add(t,"c","b","C",0,false));
   assert(!rrui_tree_add(t,"c","a","duplicate",0,false));
   assert(!rrui_tree_add(t,"d","missing","orphan",0,false));
   assert(rrui_tree_count(t)==2);
   assert(rrui_tree_select(t,"b"));
   assert(rrui_tree_horizontal(t,1));assert(rrui_tree_count(t)==3);
   assert(rrui_tree_horizontal(t,1));assert(!strcmp(rrui_tree_selected(t),"c"));
   assert(rrui_tree_row(t,2,&row)&&row.depth==2);
   assert(rrui_tree_toggle(t,"a"));assert(rrui_tree_count(t)==1);
   assert(!strcmp(rrui_tree_selected(t),"a"));
   rrui_tree_clear(t);assert(rrui_tree_add(t,"a",NULL,"A",0,true));
   assert(rrui_tree_add(t,"b","a","B",0,false));
   assert(rrui_tree_count(t)==1);
   rrui_tree_free(t);puts("PASS: tree hierarchy, selection, expansion and snapshot persistence");return 0;
}
