/* SPDX-License-Identifier: MIT */
#include <librustyui/theme.h>
#include <librustyui/tui.theme.h>
#include <glib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
static rrui_theme_t theme;
static bool initialized;
void rrui_tui_theme_set(const rrui_theme_t *t){if(t)theme=*t;else rrui_theme_default(&theme);initialized=true;}
void rrui_tui_theme_rgb(uint32_t rgb,bool bg){
   if(!cfg_tui_colors||rgb>0xffffff)return;
   unsigned r=rgb>>16,g=(rgb>>8)&255,b=rgb&255;
   const char *ct=g_getenv("COLORTERM"),*term=g_getenv("TERM");
   if(ct&&(!strcmp(ct,"truecolor")||!strcmp(ct,"24bit")))
      printf("\033[%d;2;%u;%u;%um",bg?48:38,r,g,b);
   else if(term&&strstr(term,"256color")){
      /* Actual xterm cube levels are not evenly spaced. Include grayscale
       * entries, which are especially important for dark UI surfaces. */
      static const int level[]={0,95,135,175,215,255};
      unsigned best=16;long dist=LONG_MAX;
      for(unsigned i=16;i<256;i++){
         int pr,pg,pb;
         if(i<232){unsigned j=i-16;pr=level[j/36];pg=level[(j/6)%6];pb=level[j%6];}
         else pr=pg=pb=8+10*(int)(i-232);
         long dr=(long)r-pr,dg=(long)g-pg,db=(long)b-pb;
         long d=dr*dr+dg*dg+db*db;if(d<dist){dist=d;best=i;}
      }
      printf("\033[%d;5;%um",bg?48:38,best);
   }
   else {
      static const unsigned pal[16]={0x000000,0x800000,0x008000,0x808000,0x000080,0x800080,0x008080,0xc0c0c0,
         0x808080,0xff0000,0x00ff00,0xffff00,0x0000ff,0xff00ff,0x00ffff,0xffffff};
      unsigned best=0;long dist=LONG_MAX;
      for(unsigned i=0;i<16;i++){
         long dr=(long)r-(pal[i]>>16),dg=(long)g-((pal[i]>>8)&255),db=(long)b-(pal[i]&255);
         long d=dr*dr+dg*dg+db*db;if(d<dist){best=i;dist=d;}
      }
      printf("\033[%um",(bg?40u:30u)+(best&7)+(best>=8?60u:0u));
   }
}
void rrui_tui_theme_style(rrui_theme_role_t fg,rrui_theme_role_t bg){
   if(!initialized)rrui_tui_theme_set(NULL);
   fputs("\033[0m",stdout);
   if((unsigned)fg<RRUI_THEME_COUNT)rrui_tui_theme_rgb(theme.rgb[fg],false);
   if((unsigned)bg<RRUI_THEME_COUNT)rrui_tui_theme_rgb(theme.rgb[bg],true);
}

void rrui_tui_theme_role(rrui_theme_role_t role,bool bg){
   if(!initialized)rrui_tui_theme_set(NULL);
   if((unsigned)role<RRUI_THEME_COUNT)rrui_tui_theme_rgb(theme.rgb[role],bg);
}
