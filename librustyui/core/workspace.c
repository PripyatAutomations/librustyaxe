/* SPDX-License-Identifier: MIT */
#include <librustyui/tui.workspace.h>
#include <math.h>
size_t rrui_workspace_nearest(const rrui_rect_t *r,size_t n,size_t from,int dx,int dy) {
   if(!r||from>=n||(!dx&&!dy)||dx < -1||dx>1||dy < -1||dy>1)return SIZE_MAX;
   double x=(double)r[from].x+r[from].width/2.0,y=(double)r[from].y+r[from].height/2.0;
   size_t best=SIZE_MAX;double score=0;
   for(size_t i=0;i<n;i++) {
      if(i==from||r[i].width<=0||r[i].height<=0)continue;
      double a=(double)r[i].x+r[i].width/2.0-x,b=(double)r[i].y+r[i].height/2.0-y;
      double along=a*dx+b*dy,cross=fabs(a*dy-b*dx);
      if(along<=0||(dx&&dy&&(a*dx<=0||b*dy<=0)))continue;
      double s=hypot(a,b)+2*cross;
      if(best==SIZE_MAX||s<score){best=i;score=s;}
   }
   return best;
}
