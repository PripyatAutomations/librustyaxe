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
   int split_min[]={6,10},split_weight[]={2,1};rrui_rect_t split[2];
   assert(rrui_workspace_tiles(33,100,34,split_min,split_weight,2,split)==66);
   assert(split[0].height==20&&split[1].height==10);
   int adjusted[2];
   assert(rrui_workspace_split(split,split_min,2,0,17,adjusted));
   assert(adjusted[0]==15&&adjusted[1]==15);
   assert(rrui_workspace_tiles(33,100,34,split_min,adjusted,2,split)==66);
   assert(split[0].height==15&&split[1].height==15);
   assert(rrui_workspace_split(split,split_min,2,0,-100,adjusted));
   assert(adjusted[0]==6&&adjusted[1]==24);
   assert(rrui_workspace_split(split,split_min,2,0,100,adjusted));
   assert(adjusted[0]==20&&adjusted[1]==10);
   assert(!rrui_workspace_split(split,split_min,2,1,17,adjusted));
   puts("PASS: directional workspace focus and divider resizing");return 0;
}
