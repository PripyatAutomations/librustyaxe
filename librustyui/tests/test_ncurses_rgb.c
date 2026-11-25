/* SPDX-License-Identifier: MIT */
/* Optional interactive probe; never run as part of unattended make test. */
#define _XOPEN_SOURCE_EXTENDED 1
#include <ncurses.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wchar.h>
#if !defined(NCURSES_EXT_COLORS) || !NCURSES_EXT_COLORS
int main(void) {
   puts("FAIL: ncurses headers lack extended color support");
   return 1;
}
#else
static int failures;
static int tested;
static int check_pair(int pair,int fg,int bg) {
   int a=-1,b=-1;
   tested++;
   if(init_extended_pair(pair,fg,bg)==ERR ||
      extended_pair_content(pair,&a,&b)==ERR || a!=fg || b!=bg){
      failures++;
      return ERR;
   }
   return OK;
}
static int swatch(int y,int x,int pair,int fg,int bg,const wchar_t *label) {
   if(check_pair(pair,fg,bg)==ERR)return ERR;
   cchar_t cell;
   /* opts carries the full int pair number, not a truncated short. */
   if(setcchar(&cell,L"█",A_NORMAL,0,&pair)==ERR){failures++;return ERR;}
   wchar_t buf[CCHARW_MAX];
   attr_t attr;
   short legacy;
   int actual=-1;
   if(getcchar(&cell,buf,&attr,&legacy,&actual)==ERR || actual!=pair){
      failures++;return ERR;
   }
   if(move(y,x)==ERR || add_wch(&cell)==ERR ||
      attr_set(A_NORMAL,0,&pair)==ERR || addwstr(label)==ERR ||
      attr_set(A_NORMAL,0,NULL)==ERR){failures++;return ERR;}
   cchar_t readback;
   actual=-1;
   if(mvwin_wch(stdscr,y,x,&readback)==ERR ||
      getcchar(&readback,buf,&attr,&legacy,&actual)==ERR ||
      actual!=pair){failures++;return ERR;}
   return OK;
}
int main(void) {
   if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO)){
      fputs("Run in an interactive terminal (not redirected).\n",stderr);
      return 2;
   }
   setlocale(LC_ALL,"");
   const char *term=getenv("TERM"),*ct=getenv("COLORTERM");
   SCREEN *screen=newterm(NULL,stdout,stdin);
   if(!screen){fprintf(stderr,"FAIL: newterm for TERM=%s\n",term?term:"(unset)");return 1;}
   cbreak();noecho();keypad(stdscr,TRUE);
   int color_ok=has_colors()&&start_color()!=ERR;
   int colors=color_ok?COLORS:0,pairs=color_ok?COLOR_PAIRS:0;
   int direct=colors>=16777216;
   int wide=0,high=0,stress=0,visual=0;
   if(color_ok&&direct&&pairs>16&&LINES>=12&&COLS>=48){
      mvprintw(0,0,"RustyUI ncursesw direct RGB probe");
      mvprintw(1,0,"Expected: distinct red, green, blue and themed BG");
      int base=0x111920,text=0xc5d6db;
      swatch(3,1,1,0xff0000,base,L" RED     ");
      swatch(3,14,2,0x00ff00,base,L" GREEN   ");
      swatch(3,27,3,0x0000ff,base,L" BLUE    ");
      swatch(5,1,4,text,base,L" workspace #111920 ");
      swatch(6,1,5,text,0x20343e,L" input #20343e     ");
      swatch(7,1,6,0x111920,0x91d5da,L" active title      ");
      wide=swatch(8,1,7,0xe4c477,base,L" Unicode: 漢字 é Ω ")==OK;
      /* Test pair IDs beyond signed short and the classic 8-bit limit. */
      if(pairs>40000)
         high=swatch(9,1,40000,0xf07878,base,L" pair 40000        ")==OK;
      else failures++;
      int max=pairs-1<8192?pairs-1:8192;
      for(int i=256;i<=max;i++){
         int fg=(i*7919)&0xffffff,bg=(i*104729)&0xffffff;
         if(check_pair(i,fg,bg)==OK)stress++;
      }
      mvprintw(LINES-2,0,"Do colors match labels? y/n (q = unsure)");
      refresh();
      int ch=getch();visual=ch=='y'||ch=='Y'?1:ch=='n'||ch=='N'?-1:0;
   }
   endwin();delscreen(screen);
   printf("ncurses=%s TERM=%s COLORTERM=%s\n",curses_version(),term?term:"(unset)",ct?ct:"(unset)");
   printf("COLORS=%d COLOR_PAIRS=%d direct_rgb=%s\n",colors,pairs,direct?"yes":"no");
   printf("pair_checks=%d failures=%d stress_pairs=%d wide_cell=%s high_pair=%s visual=%s\n",
      tested,failures,stress,wide?"PASS":"not passed",high?"PASS":"not passed",
      visual==1?"PASS":visual==-1?"FAIL":"not verified");
   if(!direct)puts("Try TERM=xterm-direct if installed and your emulator supports truecolor.");
   if(!tested&&direct)puts("Need terminal at least 48x12 and enough color pairs.");
   if(!failures&&tested&&wide&&high&&visual==1){puts("PASS: direct RGB and extended wide-character pairs");return 0;}
   puts("Direct RGB not fully verified");return 1;
}
#endif
