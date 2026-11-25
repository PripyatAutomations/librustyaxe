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
   int minimum[]={8,6,6},weight[]={2,1,1};rrui_rect_t tiles[3];
   assert(rrui_workspace_tiles(30,100,34,minimum,weight,3,tiles)==66);
   assert(tiles[0].x==67&&tiles[0].y==2&&tiles[0].width==34);
   assert(tiles[1].y==tiles[0].y+tiles[0].height);
   assert(tiles[2].y+tiles[2].height==28+1);
   assert(rrui_workspace_tiles(24,60,34,minimum,weight,3,tiles)==60);
   assert(!tiles[0].width);
   puts("PASS: directional workspace focus");return 0;
}
