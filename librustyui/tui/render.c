/* SPDX-License-Identifier: MIT */
/* Transitional presentation stream -> ncurses cell renderer.
 * This accepts only our drawing vocabulary, never executes arbitrary CSI. */
#define _XOPEN_SOURCE_EXTENDED 1
#include "render.h"
#include <glib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <ncurses.h>
static SCREEN *screen;
static GHashTable *pairs;
static int next_pair=1,fg=-1,bg=-1;
static attr_t attrs;
static int saved_y,saved_x;
static int cursor_visibility=1,applied_cursor=-1;
static unsigned frame_depth;
static bool dirty,wrap_pending;
static char seq[128];
static size_t seq_len;
static unsigned char utf[8];
static size_t utf_len,utf_need;
static const uint32_t ansi16[]={0x000000,0x800000,0x008000,0x808000,
   0x000080,0x800080,0x008080,0xc0c0c0,0x808080,0xff0000,0x00ff00,
   0xffff00,0x0000ff,0xff00ff,0x00ffff,0xffffff};
static uint32_t indexed_rgb(int n){
   static const int level[]={0,95,135,175,215,255};
   if(n<16)return ansi16[n];
   if(n>=232){unsigned v=8+10*(n-232);return v*0x010101u;}
   n-=16;return (uint32_t)(level[n/36]<<16|level[n/6%6]<<8|level[n%6]);
}
static int rgb_color(uint32_t rgb){
   if(COLORS>=16777216)return rgb<8?0:(int)rgb;
   int best=0;unsigned long distance=~0ul;
   for(int i=0;i<MIN(COLORS,256);i++){
      uint32_t c=indexed_rgb(i);
      long r=(long)(rgb>>16)-(c>>16);
      long g=(long)((rgb>>8)&255)-((c>>8)&255);
      long b=(long)(rgb&255)-(c&255);
      unsigned long d=r*r+g*g+b*b;
      if(d<distance){best=i;distance=d;}
   }
   return best;
}
static int indexed_color(int n){
   if(n<0||n>255)return -1;
   if(COLORS>=16777216)return n<8?n:rgb_color(indexed_rgb(n));
   return n<COLORS?n:rgb_color(indexed_rgb(n));
}
static int pair_id(void){
   if(!pairs||!COLORS||!COLOR_PAIRS)return 0;
   guint64 key=((guint64)(fg+1)<<32)|(guint32)(bg+1);
   gpointer found=g_hash_table_lookup(pairs,&key);
   if(found)return GPOINTER_TO_INT(found);
   /* Never redefine a pair while a screen cell may reference it.
    * Exhaustion degrades new combinations to pair zero until restart. */
   if(next_pair>=COLOR_PAIRS)return 0;
   int id=next_pair;
   if(init_extended_pair(id,fg,bg)==ERR)return 0;
   guint64 *stored=g_new(guint64,1);*stored=key;
   g_hash_table_insert(pairs,stored,GINT_TO_POINTER(id));
   next_pair++;
   return id;
}
static void style(void){
   int id=pair_id();
   wattr_set(stdscr,attrs,0,&id);
}
static void blank(int y,int x,int count){
   if(y<0||y>=LINES||x<0||x>=COLS)return;
   count=MIN(count,COLS-x);
   style();
   for(int i=0;i<count;i++){wmove(stdscr,y,x+i);waddch(stdscr,' ');}
   wmove(stdscr,y,x);
   dirty=true;
}
static void sgr(const int *p,size_t n){
   for(size_t i=0;i<n;i++){
      int k=p[i];
      if(k==0){attrs=0;fg=bg=-1;}
      else if(k==1)attrs|=A_BOLD;
      else if(k==2)attrs|=A_DIM;
      else if(k==3)attrs|=A_ITALIC;
      else if(k==4||k==21)attrs|=A_UNDERLINE;
      else if(k==5||k==6)attrs|=A_BLINK;
      else if(k==7)attrs|=A_REVERSE;
      else if(k==8)attrs|=A_INVIS;
      else if(k==22)attrs&=~(A_BOLD|A_DIM);
      else if(k==23)attrs&=~A_ITALIC;
      else if(k==24)attrs&=~A_UNDERLINE;
      else if(k==25)attrs&=~A_BLINK;
      else if(k==27)attrs&=~A_REVERSE;
      else if(k==28)attrs&=~A_INVIS;
      else if(k==39)fg=-1;
      else if(k==49)bg=-1;
      else if(k>=30&&k<=37)fg=indexed_color(k-30);
      else if(k>=40&&k<=47)bg=indexed_color(k-40);
      else if(k>=90&&k<=97)fg=indexed_color(k-90+8);
      else if(k>=100&&k<=107)bg=indexed_color(k-100+8);
      else if(k==38||k==48||k==58){
         int c=-1;
         if(i+2<n&&p[i+1]==5){c=indexed_color(p[i+2]);i+=2;}
         else if(i+4<n&&p[i+1]==2){
            if(p[i+2]<=255&&p[i+3]<=255&&p[i+4]<=255)
               c=rgb_color((uint32_t)(p[i+2]<<16|p[i+3]<<8|p[i+4]));
            i+=4;
         }else break;
         if(k==38)fg=c;else if(k==48)bg=c;
      }
   }
}
static void move_to(int y,int x){
   wmove(stdscr,CLAMP(y,0,LINES-1),CLAMP(x,0,COLS-1));
   wrap_pending=false;
}
static void csi(void){
   int p[64]={0};size_t n=1;
   bool priv=seq_len>3&&seq[2]=='?';
   for(size_t i=priv?3:2;i+1<seq_len;i++){
      char ch=seq[i];
      if(ch>='0'&&ch<='9'){if(p[n-1]>100000)return;p[n-1]=p[n-1]*10+ch-'0';}
      else if(ch==';'&&n<G_N_ELEMENTS(p))n++;
      else return;
   }
   int y,x;getyx(stdscr,y,x);
   char cmd=seq[seq_len-1];
   if(priv){
      if(p[0]==25&&(cmd=='h'||cmd=='l'))cursor_visibility=cmd=='h';
      return;
   }
   switch(cmd){
      case 'm':sgr(p,n);break;
      case 'H':case 'f':move_to(MAX(1,p[0])-1,MAX(1,p[1])-1);break;
      case 'A':move_to(y-MAX(1,p[0]),x);break;
      case 'B':move_to(y+MAX(1,p[0]),x);break;
      case 'C':move_to(y,x+MAX(1,p[0]));break;
      case 'D':move_to(y,x-MAX(1,p[0]));break;
      case 'G':move_to(y,MAX(1,p[0])-1);break;
      case 's':saved_y=y;saved_x=x;break;
      case 'u':move_to(saved_y,saved_x);break;
      case 'K':
         if(p[0]==2)blank(y,0,COLS);
         else if(p[0]==1)blank(y,0,x+1);
         else blank(y,x,COLS-x);
         move_to(y,x);break;
      case 'J':
         if(p[0]==2){for(int r=0;r<LINES;r++)blank(r,0,COLS);}
         else if(!p[0]){blank(y,x,COLS-x);for(int r=y+1;r<LINES;r++)blank(r,0,COLS);}
         move_to(y,x);break;
   }
}
static void glyph(gunichar ch){
   if(ch=='\r'){int y,x;getyx(stdscr,y,x);(void)x;move_to(y,0);return;}
   if(ch=='\n'){int y,x;getyx(stdscr,y,x);(void)x;move_to(y+1,0);return;}
   if(ch<32||ch==127)return;
   int y,x;getyx(stdscr,y,x);
   /* Prevent automatic margins from spilling a clipped widget onto the
    * next row. Explicit cursor positioning begins the next span. */
   if(wrap_pending)return;
   int width=g_unichar_iszerowidth(ch)?0:g_unichar_iswide(ch)?2:1;
   if(width>COLS-x)return;
   wchar_t text[2]={(wchar_t)ch,0};
   style();waddnwstr(stdscr,text,1);dirty=true;
   if(width&&x+width>=COLS){wmove(stdscr,y,COLS-1);wrap_pending=true;}
}
static void byte(unsigned char ch){
   if(seq_len){
      if(seq_len==1&&ch!='['){seq_len=0;return;}
      if(seq_len>=sizeof(seq)){seq_len=0;return;}
      seq[seq_len++]=(char)ch;
      if(seq_len>2&&ch>='@'&&ch<='~'){csi();seq_len=0;}
      return;
   }
   if(ch==27){seq[0]=27;seq_len=1;utf_len=0;return;}
   if(!utf_len){
      if(ch<128){glyph(ch);return;}
      utf_need=ch>=0xc2&&ch<=0xdf?2:ch>=0xe0&&ch<=0xef?3:ch>=0xf0&&ch<=0xf4?4:0;
      if(!utf_need){glyph(0xfffd);return;}
   }else if((ch&0xc0)!=0x80){utf_len=0;glyph(0xfffd);byte(ch);return;}
   utf[utf_len++]=ch;
   if(utf_len==utf_need){
      gunichar c=g_utf8_get_char_validated((const char *)utf,utf_len);
      glyph(c==(gunichar)-1||c==(gunichar)-2?0xfffd:c);utf_len=0;
   }
}
bool rrui_render_init(void){
   if(screen)return true;
   if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO))return false;
   setlocale(LC_CTYPE,"");
   screen=newterm(NULL,stdout,stdin);
   if(!screen)return false;
   if(has_colors()&&start_color()!=ERR){
      use_default_colors();
      pairs=g_hash_table_new_full(g_int64_hash,g_int64_equal,g_free,NULL);
   }
   scrollok(stdscr,FALSE);leaveok(stdscr,FALSE);
   next_pair=1;fg=bg=-1;attrs=0;frame_depth=0;seq_len=utf_len=0;
   cursor_visibility=1;applied_cursor=-1;dirty=true;wrap_pending=false;
   return true;
}
bool rrui_render_curses(void){return screen!=NULL;}
void rrui_render_resize(int rows,int cols){
   if(screen&&rows>0&&cols>0&&(rows!=LINES||cols!=COLS)){
      resizeterm(rows,cols);dirty=true;wrap_pending=false;
   }
}
void rrui_render_begin(void){if(screen)frame_depth++;}
void rrui_render_flush(void){
   if(!screen||frame_depth)return;
   if(applied_cursor!=cursor_visibility){curs_set(cursor_visibility);applied_cursor=cursor_visibility;}
   if(dirty){wnoutrefresh(stdscr);doupdate();dirty=false;}
}
void rrui_render_end(void){if(screen&&frame_depth)frame_depth--;rrui_render_flush();}
void rrui_render_fini(void){
   if(!screen)return;
   endwin();delscreen(screen);screen=NULL;
   if(pairs){g_hash_table_destroy(pairs);pairs=NULL;}
}
void rrui_render_write(const char *s,size_t n){
   if(!screen||!s)return;
   for(size_t i=0;i<n;i++)byte((unsigned char)s[i]);
}
void rrui_render_printf(const char *fmt,...){
   va_list ap;va_start(ap,fmt);char *s=g_strdup_vprintf(fmt,ap);va_end(ap);
   if(s){rrui_render_write(s,strlen(s));g_free(s);}
}
void rrui_render_char(int ch){unsigned char c=ch;rrui_render_write((const char *)&c,1);}
