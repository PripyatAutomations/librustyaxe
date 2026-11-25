/* SPDX-License-Identifier: MIT */
#include <librustyui/tui.workspace.h>
#include <assert.h>
#include <stdio.h>
int main(void){
   rrui_rect_t r[]={{1,1,20,10},{30,1,20,10},{1,20,20,10},{30,20,20,10}};
   assert(rrui_workspace_nearest(r,4,0,1,0)==1);
   assert(rrui_workspace_nearest(r,4,0,0,1)==2);
   assert(rrui_workspace_nearest(r,4,0,1,1)==3);
   assert(rrui_workspace_nearest(r,4,3,-1,-1)==0);
   assert(rrui_workspace_nearest(r,4,0,-1,0)==SIZE_MAX);
   puts("PASS: directional workspace focus");return 0;
}
