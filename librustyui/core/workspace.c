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

int rrui_workspace_tiles(int rows,int cols,int width,const int *minimum,
                         const int *weight,size_t n,rrui_rect_t *out) {
   if(!out||!minimum||!weight||n>32)return cols;
   for(size_t i=0;i<n;i++)out[i]=(rrui_rect_t){0};
   if(cols<64||rows<12||!n)return cols;
   if(width<24)width=24;
   if(width>cols-40)width=cols-40;
   int available=rows-3,used=0,total=0;size_t visible=0;
   for(;visible<n;visible++){
      int m=minimum[visible]<6?6:minimum[visible];
      if(m>available-used)break;
      used+=m;total+=weight[visible]>0&&weight[visible]<=1000?weight[visible]:1;
   }
   if(!visible)return cols;
   int y=2,remaining=available,minimum_left=used;
   for(size_t i=0;i<visible;i++){
      int w=weight[i]>0&&weight[i]<=1000?weight[i]:1;
      int m=minimum[i]<6?6:minimum[i];
      minimum_left-=m;
      int height=(int)((long long)remaining*w/total);
      if(height<m)height=m;
      if(height>remaining-minimum_left)height=remaining-minimum_left;
      out[i]=(rrui_rect_t){cols-width+1,y,width,height};
      y+=height;remaining-=height;total-=w;
   }
   return cols-width;
}

bool rrui_workspace_split(const rrui_rect_t *r,const int *minimum,size_t n,
                          size_t upper,int boundary,int *weights) {
   if(!r||!minimum||!weights||n<2||n>32||upper>=n-1)return false;
   for(size_t i=0;i<n;i++)if(r[i].width<=0||r[i].height<=0)return false;
   const rrui_rect_t *a=&r[upper],*b=&r[upper+1];
   if(a->x!=b->x||a->width!=b->width||a->y+a->height!=b->y)return false;
   int amin=minimum[upper]<6?6:minimum[upper];
   int bmin=minimum[upper+1]<6?6:minimum[upper+1];
   int total=a->height+b->height;
   if(total<amin+bmin)return false;
   int height=boundary<a->y+amin?amin:boundary>b->y+b->height-bmin?total-bmin:boundary-a->y;
   for(size_t i=0;i<n;i++)weights[i]=r[i].height;
   weights[upper]=height;weights[upper+1]=total-height;
   return true;
}
