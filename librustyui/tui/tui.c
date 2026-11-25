#include <glib.h>
// tui.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Generic multi-screen ('windows') text user interface stuff
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <errno.h>
#include <librustyaxe/core.h>
#include <librustyui/tui.h>
#include <librustyui/theme.h>
#include "tui.internal.h"

extern void tui_keys_init(void); // tui.keys.c
extern void tui_keys_fini(void); // tui.keys.c

extern char input_buf[TUI_INPUTLEN];
extern int tui_input_len;
extern int tui_cursor_pos;

// Is the TUI enabled?
bool tui_is_enabled = true;
static bool cfg_tui_use_mouse;

// These get updated by update_term_size() on SIGWINCH (terminal resize) and in
// tui_init()
static int term_rows = 24;      // default lines
static int term_cols = 80;      // default width
static char status_line[STATUS_LEN];
static char *(*topline_renderer)(tui_window_t *win);
static const char *(*prompt_renderer)(tui_window_t *win);
static volatile sig_atomic_t tui_redraw_pending = 0;
static bool tui_finalized = false;
static bool tui_atexit_registered = false;
static bool tui_sigwinch_installed = false;
static void (*previous_sigwinch_handler)(int) = SIG_DFL;
static pthread_once_t tui_render_lock_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t tui_render_mutex;

static void tui_render_lock_init(void) {
   pthread_mutexattr_t attr;
   pthread_mutexattr_init(&attr);
   pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
   pthread_mutex_init(&tui_render_mutex, &attr);
   pthread_mutexattr_destroy(&attr);
}

void tui_render_lock(void) {
   pthread_once(&tui_render_lock_once, tui_render_lock_init);
   pthread_mutex_lock(&tui_render_mutex);
}

void tui_render_unlock(void) {
   pthread_mutex_unlock(&tui_render_mutex);
}

void tui_set_topline_renderer(char *(*renderer) (tui_window_t *win)) {
   tui_render_lock();
   topline_renderer = renderer;
   tui_render_unlock();
}

void tui_set_prompt_renderer(const char *(*renderer) (tui_window_t *win)) {
   tui_render_lock();
   prompt_renderer = renderer;
   tui_render_unlock();
}

// Keep the top row on one physical line. Preserve SGR colors, but do not
// permit embedded controls to move the cursor. Count UTF-8 bytes conservatively
// against the column budget and never cut a multibyte character in half.
static void tui_print_topline(const char *text, int columns) {
   rrui_tui_theme_style(RRUI_THEME_TEXT,RRUI_THEME_HEADER);
   const unsigned char *p = (const unsigned char *)text;
   while (p && *p && columns > 0) {
      if (p[0] == '\033' && p[1] == '[') {
         const unsigned char *end = p + 2;
         while (*end && !(*end >= '@' && *end <= '~')) {
            end++;
         }

         if (!*end) {
            break;
         }

         if (*end == 'm') {
            fwrite(p, 1, end - p + 1, stdout);
         }
         p = end + 1;
         continue;
      }

      if (*p < 32 || *p == 127) {
         putchar(' ');
         p++;
         columns--;
         continue;
      }
      size_t len = 1;

      if (*p >= 0xc2 && *p <= 0xf4) {
         size_t expected = *p < 0xe0 ? 2 : (*p < 0xf0 ? 3 : 4);
         while (len < expected && p[len] && (p[len] & 0xc0) == 0x80) {
            len++;
         }

         if (len != expected) {
            p += len;
            continue;
         }
      }

      if (len > (size_t)columns) {
         break;
      }
      fwrite(p, 1, len, stdout);
      p += len;
      columns -= (int)len;
   }
   rrui_tui_theme_style(RRUI_THEME_TEXT,RRUI_THEME_HEADER);
   while(columns-->0)putchar(' ');
   fputs("\033[0m",stdout);
}

// SSH_TTY is set when we're running over an SSH session: remote terminals
// are bandwidth-sensitive, so the clock drops the seconds (HH:MM only) and
// the client only repaints when the screen actually changes.
bool tui_over_ssh = false;

bool tui_is_over_ssh(void) {
   return tui_over_ssh;
}

/* Return bytes and display cells for a complete UTF-8 character. */
static int tui_cell(const char *s,size_t *bytes) {
   gunichar ch=g_utf8_get_char_validated(s,-1);
   if(ch==(gunichar)-1||ch==(gunichar)-2){*bytes=1;return 1;}
   *bytes=(size_t)(g_utf8_next_char(s)-s);
   if(g_unichar_iszerowidth(ch))return 0;
   return g_unichar_iswide(ch)?2:1;
}
static int tui_line_rows(const char *s, int width) {
   int rows = 1;
   int col = 0;

   if (!s || width <= 0) {
      return 1;
   }

   while (*s) {
      if (*s == '\033' && *(s + 1) == '[') {
         s += 2;

         while (*s && !(*s >= '@' && *s <= '~')) {
            s++;
         }

         if (*s) {
            s++;
         }
         continue;
      }

      if (*s == '\n') {
         rows++;
         col = 0;
         s++;
         continue;
      }

      size_t bytes;int cells=tui_cell(s,&bytes);
      if(col&&col+cells>width){rows++;col=0;}
      col+=cells;s+=bytes;
   }
   return rows;
}

/* Preserve the visible rows while reading older scrollback. Ring eviction is clamped by the normal renderer when the oldest visible row has expired. */
void tui_scrollback_appended(tui_window_t *window, const char *line) {
   if (window->scroll_offset > 0) {
      window->scroll_offset += tui_line_rows(line, tui_cols());
   }
}

// Read the terminal size and update our size to match
static void update_term_size(void) {
   struct winsize ws;

   // Test runners and redirected output have no terminal window to query.
   if (!isatty(STDOUT_FILENO)) {
      return;
   }

   if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
      term_rows = ws.ws_row;
      term_cols = ws.ws_col;
   } else {
      fprintf(stderr, "failed TIOCGWINSZ %d: %s\n", errno, strerror(errno));
   }
}

// Move cursor to row,col (1-based!)
static void term_move(int row, int col) {
   printf("\033[%d;%dH", row, col);
}

// Clear to end of line
static void term_clrtoeol(void) {
   printf("\033[K");
}

static void clear_line(int row) {
   printf("\033[%d;1H\033[2K", row);   // move to row, clear entire line
}

// Handle window size changes
static void sigwinch_handler(int signum) {
   (void)signum;
   // A signal handler may interrupt stdio while a redraw is in progress.
   // Defer the ioctl and printf-based redraw to the normal event loop.
   tui_redraw_pending = 1;
}

////////////////
// Public API //
////////////////
int tui_rows(void) {
   return term_rows;
}

int tui_cols(void) {
   return term_cols;
}

char *s_status_offline = NULL;

static void tui_atexit_cleanup(void) {
   tui_fini();
}

bool tui_init(void) {
   tui_finalized = false;
   tui_is_enabled = true;

   if (!tui_atexit_registered) {
      tui_atexit_registered = (atexit(tui_atexit_cleanup) == 0);
   }
   update_term_size();
   cfg_tui_colors = cfg_get_bool("tui.use-color", true);
   cfg_tui_use_mouse = cfg_get_bool("tui.use-mouse", true);

   // Don't unset it if -T is used
   if (!tui_over_ssh) {
      // Detect an SSH session: HH:MM clock & fewer repaints (see tui_over_ssh)
      tui_over_ssh = (getenv("SSH_TTY") != NULL);
   }
   // set SIGnal WINdow CHange handler
   previous_sigwinch_handler = signal(SIGWINCH, sigwinch_handler);
   tui_sigwinch_installed = (previous_sigwinch_handler != SIG_ERR);

   // set default status line
   char *s_status_offline = tui_colorize_string("\00314[\00304OFFLINE\00314]\017");
   snprintf(status_line, STATUS_LEN, "%s", s_status_offline);
   free(s_status_offline);

   // set up windowing
   tui_window_init();

   // force raw input mode
   tui_raw_mode(true);

   tui_keys_init();

   tui_window_update_topline("Chikin r tasty");
   // draw the initial screen
   tui_redraw_screen();

   // Enable mouse support
   if (cfg_tui_use_mouse) {
      printf("\033[?1000h");    /* mouse button reporting */
      printf("\033[?1002h");    /* button + drag reporting */
      printf("\033[?1006h");    /* SGR extended mouse encoding */
      fflush(stdout);
   }
   printf("\033[%d;1H", term_rows);
   fflush(stdout);

   return false;
}

extern bool rrui_tui_draw(int rows,int cols);
extern void rrui_tui_draw_panels(int rows,int cols);
extern bool rrui_tui_modal_active(void);
extern int rrui_tui_chat_width(int,int,bool);
extern bool rrui_tui_main_focused(void);
#include <librustyui/form.h>
bool tui_fini(void) {
   tui_render_lock();

   if (tui_finalized) {
      tui_render_unlock();

      return false;
   }
   tui_finalized = true;
   tui_is_enabled = false;
   rrui_tui_close(NULL);

   if (tui_sigwinch_installed) {
      signal(SIGWINCH, previous_sigwinch_handler);
      tui_sigwinch_installed = false;
   }

   /* termkey must stop before termios is restored.  Older shutdown ordering restored cooked mode first, then termkey restored its raw-mode snapshot. */
   tui_keys_fini();
   tui_raw_mode(false);

   /* Restore modes which can make the caller's shell appear broken.  Disable mouse/focus/paste/application modes even when configuration says they were off:
    * these reset sequences are harmless and make partial init safe. */
   fputs("\033[0m"              /* reset colors and attributes */
      "\033[?25h"               /* show cursor */
      "\033[0 q"                /* default cursor style */
      "\033[?1l"                /* normal cursor keys */
      "\033>"                   /* normal keypad */
      "\033[?1000l"             /* mouse click reporting */
      "\033[?1002l"             /* mouse drag reporting */
      "\033[?1003l"             /* all-motion mouse reporting */
      "\033[?1004l"             /* focus reporting */
      "\033[?1006l"             /* SGR mouse reporting */
      "\033[?2004l"             /* bracketed paste */
      "\033[?6l"                /* absolute cursor origin */
      "\033[?7h"                /* automatic margins */
      "\033[r"                  /* full-screen scrolling region */
      "\033[999;1H"             /* place output below the TUI */
      "\033[2K\r\n", stdout);
   fflush(stdout);

   tui_window_fini();
   tui_render_unlock();

   return false;
}

// While deferred, tui_vprint() skips the per-line full redraw; the caller
// flushes once when done printing a batch (i.e. /help)
int tui_redraw_defer_count = 0;

void tui_redraw_defer(void) {
   tui_render_lock();
   tui_redraw_defer_count++;
   tui_render_unlock();
}

void tui_redraw_flush(void) {
   tui_render_lock();

   if (tui_redraw_defer_count > 0) {
      tui_redraw_defer_count = 0;
   }
   tui_redraw_if_pending();
   tui_render_unlock();
}

void tui_redraw_request(void) {
   tui_redraw_pending = 1;
}

bool tui_redraw_if_pending(void) {
   if (!tui_is_enabled || !tui_redraw_pending) {
      return false;
   }
   tui_redraw_screen();

   return true;
}

void tui_redraw_screen(void) {
   tui_render_lock();

   if (!tui_is_enabled) {
      tui_render_unlock();

      return;
   }
   tui_redraw_pending = 0;
   update_term_size();
   if(rrui_tui_draw(term_rows,term_cols)){tui_render_unlock();return;}

   rrui_tui_theme_style(RRUI_THEME_TEXT,RRUI_THEME_WORKSPACE);
   printf("\033[H\033[2J");     // clear screen

   tui_window_t *w = tui_active_window();

   if (!w) {
      tui_render_unlock();

      return;
   }
   int chat_cols=rrui_tui_chat_width(term_rows,term_cols,w->view_draw!=NULL);
   // --- Top status line ---
   printf("\033[1;1H");

   char *topline = topline_renderer ? topline_renderer(w) : NULL;

   if (!topline) {
      topline = tui_colorize_string(w->status_line);
   }
   putchar(' ');
   tui_print_topline(topline, term_cols > 1 ? term_cols - 1 : 0);
   free(topline);

   /* Structured tabs may own the full viewport below the top line. */
   if (w->view_draw) {
      for (int r = 2 ; r <= (w->view_full_height ? term_rows : term_rows - 2) ; r++) {
         clear_line(r);
      }

      w->view_draw(w, term_rows, term_cols, w->view_data);
      fputs("\033[0m", stdout);

      if (w->view_full_height) {
         rrui_tui_draw_panels(term_rows,term_cols);
         fflush(stdout);
         tui_render_unlock();

         return;
      }
      goto draw_footer;
   }
   // --- Main pane --- (small terminals retain the compact layout)
   bool framed=term_rows>=9&&chat_cols>=20;
   int main_width=chat_cols;
   if(framed)chat_cols-=2;
   int log_end=framed?term_rows-3:term_rows-1;
   int log_start=framed?3:2;
   int log_area_rows=MAX(1,log_end-log_start);
   int total_rows = 0;

   // Find the total number of physical terminal rows occupied by the
   // messages currently in the ring buffer.
   int oldest = (w->log_head + LOG_LINES - w->log_count) % LOG_LINES;

   for (int i = 0 ; i < w->log_count ; i++) {
      int idx = (oldest + i) % LOG_LINES;

      if (w->buffer[idx]) {
         total_rows += tui_line_rows(w->buffer[idx], chat_cols);
      }
   }

   // scroll_offset is now measured in physical terminal rows.
   int max_scroll = total_rows > log_area_rows ? total_rows - log_area_rows : 0;

   if (w->scroll_offset < 0) {
      w->scroll_offset = 0;
   } else if (w->scroll_offset > max_scroll) {
      w->scroll_offset = max_scroll;
   }
   // This is the physical row, counting from the oldest message, that
   // should appear at the top of the log area.
   int target_row = total_rows - log_area_rows - w->scroll_offset;

   if (target_row < 0) {
      target_row = 0;
   }
   // Find the message containing target_row.
   int start = oldest;
   int skip_rows = target_row;

   for (int i = 0 ; i < w->log_count ; i++) {
      int idx = (oldest + i) % LOG_LINES;

      if (!w->buffer[idx]) {
         continue;
      }

      int message_rows = tui_line_rows(w->buffer[idx], chat_cols);

      if (skip_rows < message_rows) {
         start = idx;
         break;
      }

      skip_rows -= message_rows;
   }

   int row = log_start;

   // Render messages starting at 'start'.  skip_rows tells us how many
   // physical wrapped rows to skip inside the first message.
   bool first_message = true;

   for (int i = 0 ; i < w->log_count && row < log_end ; i++) {
      int idx = (start + i) % LOG_LINES;

      if (!w->buffer[idx]) {
         continue;
      }

      const char *p = w->buffer[idx];
      char active_sgr[64] = ""; // last SGR seen, so wrapped rows re-apply it
      int rows_done = 0;

      while (*p && row < log_end) {
         int col = 0;
         const char *line_start = p;
         const char *last_break = p;
         char start_sgr[64];memcpy(start_sgr,active_sgr,sizeof(start_sgr));

         while (*p && col < chat_cols) {
            if (*p == '\033' && *(p + 1) == '[') {
               const char *estart = p;

               p += 2;

               while (*p && !(*p >= '@' && *p <= '~')) {
                  p++;
               }

               if (*p) {
                  p++;
               }

               // Remember the last complete SGR sequence; a wrapped row must
               // re-apply the color in effect where the previous row ended
               if (p[-1] == 'm' && (size_t)(p - estart) < sizeof(active_sgr)) {
                  size_t elen = p - estart;

                  memcpy(active_sgr, estart, elen);
                  active_sgr[elen] = '\0';
                  last_break=p;
               }

               continue;
            }

            if (*p == '\n') {
               break;
            }

            size_t bytes;int cells=tui_cell(p,&bytes);
            if(col&&col+cells>chat_cols)break;
            col+=cells;p+=bytes;last_break=p;
         }
         bool explicit_newline = (*p == '\n');

         // If this is the first message and we need to skip physical rows,
         // discard this wrapped row without printing it.
         if (first_message && skip_rows > 0) {
            skip_rows--;
            if(explicit_newline)p++;
         } else {
            printf("\033[%d;%dH", row++,framed?2:1);
            rrui_tui_theme_style(RRUI_THEME_TEXT,RRUI_THEME_WORKSPACE);

            // Continuation of a wrapped row: restore the active color first
            if (rows_done > 0 && start_sgr[0]) {
               fputs(start_sgr, stdout);
            }

            if (explicit_newline) {
               fwrite(line_start, 1, p - line_start, stdout);
               p++;
            } else {
               fwrite(line_start, 1, last_break - line_start, stdout);
            }

            // Reset attributes so colors don't bleed into the next line
            rrui_tui_theme_style(RRUI_THEME_TEXT,RRUI_THEME_WORKSPACE);
            if(framed){while(col++<chat_cols)putchar(' ');}else term_clrtoeol();
         }

         rows_done++;

         // Once we've rendered/skipped the first physical row, normal
         // rendering continues.
         if (first_message && skip_rows == 0) {
            first_message = false;
         }
      }
   }

   rrui_tui_theme_style(RRUI_THEME_TEXT,RRUI_THEME_WORKSPACE);
   // Fill remaining log space with blanks
   while (row < log_end) {
      printf("\033[%d;%dH", row++,framed?2:1);
      if(framed){for(int j=0;j<chat_cols;j++)putchar(' ');}else term_clrtoeol();
   }
   if(framed){
      rrui_tui_theme_style(RRUI_THEME_BORDER,RRUI_THEME_WORKSPACE);
      printf("\033[2;1H┌");
      rrui_tui_theme_style(rrui_tui_main_focused()?RRUI_THEME_WORKSPACE:RRUI_THEME_TEXT,rrui_tui_main_focused()?RRUI_THEME_ACTIVE_HEADER:RRUI_THEME_HEADER);
      fputs(" Main ",stdout);
      for(int j=6;j<chat_cols;j++)putchar(' ');
      rrui_tui_theme_style(RRUI_THEME_BORDER,RRUI_THEME_WORKSPACE);
      fputs("┐",stdout);
      for(int r=3;r<term_rows-1;r++){
         printf("\033[%d;1H│\033[%d;%dH│",r,r,main_width);
      }
      printf("\033[%d;1H└",term_rows-2);
      for(int j=0;j<chat_cols;j++)fputs("─",stdout);
      fputs("┘",stdout);
      fputs("\033[0m",stdout);
   }
draw_footer:
   // --- Bottom status line ---
   rrui_tui_theme_style(RRUI_THEME_TEXT,RRUI_THEME_HEADER);
   printf("\033[%d;1H", term_rows - 1);
   printf("%-*s", term_cols, status_line);
   fputs("\033[0m",stdout);

   tui_redraw_clock();
   tui_update_input_line();
   rrui_tui_draw_panels(term_rows,term_cols);
   tui_render_unlock();
}

void tui_redraw_topline(void) {
   tui_render_lock();
   if(rrui_tui_modal_active()){tui_render_unlock();return;}

   if (!tui_is_enabled) {
      tui_render_unlock();

      return;
   }
   update_term_size();
   tui_window_t *w = tui_active_window();

   if (!w) {
      tui_render_unlock();

      return;
   }

   char *topline = topline_renderer ? topline_renderer(w) : NULL;

   if (!topline) {
      topline = tui_colorize_string(w->status_line);
   }
   printf("\033[s\033[1;1H");
   putchar(' ');
   tui_print_topline(topline, term_cols > 1 ? term_cols - 1 : 0);
   printf("\033[u");
   free(topline);
   fflush(stdout);
   tui_render_unlock();
}

void tui_redraw_statusline(void) {
   tui_render_lock();
   if(rrui_tui_modal_active()){tui_render_unlock();return;}

   if (!tui_is_enabled) {
      tui_render_unlock();

      return;
   }
   update_term_size();
   tui_window_t *active = tui_active_window();

   if (active && active->view_draw && active->view_full_height) {
      tui_render_unlock();

      return;
   }
   rrui_tui_theme_style(RRUI_THEME_TEXT,RRUI_THEME_HEADER);
   printf("\033[s\033[%d;1H%-*s\033[0m\033[u", term_rows - 1, term_cols, status_line);
   tui_redraw_clock();
   fflush(stdout);
   tui_render_unlock();
}

void tui_redraw_clock(void) {
   tui_render_lock();
   if(rrui_tui_modal_active()){tui_render_unlock();return;}

   if (!tui_is_enabled || (tui_active_window() && tui_active_window()->view_full_height)) {
      tui_render_unlock();

      return;
   }
   int width = term_cols;

   // Get current time
   time_t t = time(NULL);
   struct tm tm;
   localtime_r(&t, &tm);

   // Move cursor to second-to-last row, far-right minus visible width + 1
   int clock_visible_len = (tui_over_ssh ? 7 : 10);   // [HH:MM] vs [HH:MM:SS]
   int col = width - clock_visible_len;   // 0-based index for the first char

   if (col > 0) {
      char clock_tagged[128];

      if (tui_over_ssh) {
         // SSH session: HH:MM only, so we can redraw far less often
         snprintf(clock_tagged, sizeof(clock_tagged), "\00314[\00310%02d\00314:\00310%02d\00314]\017", tm.tm_hour, tm.tm_min);
      } else {
         snprintf(clock_tagged, sizeof(clock_tagged), "\00314[\00310%02d\00314:\00310%02d\00314:\00310%02d\00314]\017", tm.tm_hour, tm.tm_min, tm.tm_sec);
      }
      char *clock_colored = tui_colorize_string(clock_tagged);

      printf("\033[s");         // save cursor
      printf("\033[%d;%dH%s\033[0m", term_rows - 1, col + 1, clock_colored);
      printf("\033[u");         // restore cursor
      free(clock_colored);
   }
   fflush(stdout);
   tui_render_unlock();
}

bool tui_update_status(tui_window_t *win, const char *fmt, ...) {
   tui_render_lock();

   if (!tui_is_enabled) {
      tui_render_unlock();

      return true;
   }

   if (fmt) {
      char tmpbuf[513];
      va_list ap;
      va_start(ap, fmt);
      memset(tmpbuf, 0, sizeof(tmpbuf));
      vsnprintf(tmpbuf, sizeof(tmpbuf) - 1, fmt, ap);
      va_end(ap);

      dict *vars = dict_new();

      if (win) {
         dict_add(vars, "win.title", (char *)(win->title ? win->title : "status"));
         char scroll_val[16];
         snprintf(scroll_val, sizeof(scroll_val), "%d", win->scroll_offset);
         dict_add(vars, "win.scroll", scroll_val);
      }
      // render string with ${var} expansion + color
      char *colored = tui_render_string(vars, NULL, "%s", tmpbuf);
      dict_free(vars);

      // update the status line
      strlcpy(status_line, colored, sizeof(status_line));
      free(colored);
      status_line[sizeof(status_line) - 1] = '\0';
   }
   tui_redraw_statusline();

   tui_render_unlock();

   return false;
}

// This will take a dict with variables for us to escape anywhere we see
// ${variable}
// The result is rendered with IRC color/style controls.
char *tui_render_string(dict *data, const char *title, const char *fmt, ...) {
   if (!fmt) {
      return NULL;
   }
   char *processed = malloc(TUI_STRING_LEN);

   if (!processed) {
      fprintf(stderr, "OOM in tui_render_string\n");

      return NULL;
   }
   // Step 1: expand printf escapes
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(processed, TUI_STRING_LEN, fmt, ap);
   va_end(ap);

   // Step 2: expand ${var} and ${var:default} using dict
   char *expanded = malloc(TUI_STRING_LEN);

   if (!expanded) {
      free(processed);
      fprintf(stderr, "OOM in tui_render_string\n");

      return NULL;
   }
   memset(expanded, 0, TUI_STRING_LEN);

   const char *src = processed;
   char *dst = expanded;

   while (*src && (dst - expanded) < (TUI_STRING_LEN - 1)) {
      if (src[0] == '$' && src[1] == '{') {
         const char *end = strchr(src + 2, '}');

         if (end) {
            size_t varlen = end - (src + 2);
            char varspec[256];

            if (varlen >= sizeof(varspec)) {
               varlen = sizeof(varspec) - 1;
            }
            memcpy(varspec, src + 2, varlen);
            varspec[varlen] = '\0';

            // Split on ':' → varname:default
            char *colon = strchr(varspec, ':');
            const char *varname = varspec;
            const char *fallback = NULL;

            if (colon) {
               *colon = '\0';
               fallback = colon + 1;
            }
            const char *val = dict_get(data, varname, NULL);

            if (!val && fallback) {
               val = fallback;
            }

            if (val) {
               size_t vlen = strlen(val);

               if ((dst - expanded) + vlen < TUI_STRING_LEN - 1) {
                  memcpy(dst, val, vlen);
                  dst += vlen;
               }
            }
            src = end + 1;
            continue;
         }
      }
      *dst++ = *src++;
   }
   *dst = '\0';

   char *colorized = tui_colorize_string(expanded);

   free(processed);
   free(expanded);

   return colorized;            // caller must free
}

void tui_window_update_topline(const char *line) {
   tui_render_lock();

   if (!tui_is_enabled || !line) {
      tui_render_unlock();

      return;
   }
   // Move cursor to the top-left
   printf("\033[1;1H");

   // Clear the line
   printf("\033[2K");

   // Print the new content
   printf("%s", line);

   // Make sure output is flushed
   fflush(stdout);
   tui_render_unlock();
}

/* Return the byte offset of a visible column in an ANSI-rendered string. Slicing the rendered string by column used to split escape sequences when the input
 * line scrolled, leaving the terminal cursor and colors corrupted. */
static size_t ansi_column_offset(const char *text, int column) {
   size_t offset = 0;
   int visible = 0;

   if (!text || column <= 0) {
      return 0;
   }
   while (text[offset] && visible < column) {
      if ((unsigned char)text[offset] == 0x1b) {
         offset++;

         if (text[offset] == '[') {
            offset++;
            while (text[offset] && !((text[offset] >= '@') && (text[offset] <= '~'))) {
               offset++;
            }

            if (text[offset]) {
               offset++;
            }
         }
         continue;
      }
      offset++;
      visible++;
   }
   return offset;
}

static void ansi_copy_columns(const char *text, int columns, char *out, size_t out_size) {
   size_t in = 0;
   size_t used = 0;
   int visible = 0;

   if (!text || !out || out_size == 0) {
      return;
   }
   while (text[in] && used + 1 < out_size && visible < columns) {
      if ((unsigned char)text[in] == 0x1b) {
         size_t start = in++;

         if (text[in] == '[') {
            in++;
            while (text[in] && !((text[in] >= '@') && (text[in] <= '~'))) {
               in++;
            }

            if (text[in]) {
               in++;
            }
         }
         while (start < in && used + 1 < out_size) {
            out[used++] = text[start++];
         }
         continue;
      }
      out[used++] = text[in++];
      visible++;
   }
   out[used] = '\0';
}

void tui_update_input_line(void) {
   tui_render_lock();
   if(rrui_tui_modal_active()){tui_render_unlock();return;}

   if (!tui_is_enabled) {
      tui_render_unlock();

      return;
   }
   tui_window_t *win = tui_active_window();

   if (!win || (win->view_draw && win->view_full_height)) {
      tui_render_unlock();

      return;
   }

   if (tui_input_len < 0) {
      tui_input_len = 0;
   }

   if (tui_input_len >= TUI_INPUTLEN) {
      tui_input_len = TUI_INPUTLEN - 1;
   }

   if (tui_cursor_pos < 0) {
      tui_cursor_pos = 0;
   }

   if (tui_cursor_pos > tui_input_len) {
      tui_cursor_pos = tui_input_len;
   }
   int main_width=rrui_tui_chat_width(term_rows,term_cols,win->view_draw!=NULL);
   bool framed=!win->view_draw&&term_rows>=9&&main_width>=20;
   int input_row=framed?term_rows-3:term_rows;
   int input_col=framed?2:1;
   int width=framed?main_width-2:term_cols;
   const char *prompt_title = prompt_renderer ? prompt_renderer(win) : NULL;

   if (!prompt_title) {
      prompt_title = win->title;
   }
   int prompt_len = visible_length(prompt_title) + 2; // title + '>' + space

   // --- build visible input buffer with placeholders ---
   char buf[2048];
   int buf_pos = 0;
   int screen_cols[tui_input_len + 1];
   memset(screen_cols,0,sizeof(screen_cols)); // map input char -> screen column
   int cols = 0;

   for (int i = 0 ; i < tui_input_len ; i++) {
      if(buf_pos>(int)sizeof(buf)-64)break;
      unsigned char c = input_buf[i];

      switch (c) {
         case 0x02: {
            // Ctrl-B toggle bold
            buf_pos += snprintf(&buf[buf_pos], sizeof(buf) - buf_pos, "{bold}B{bold-off}");
            break;
         }
         case 0x03: {
            // Ctrl-C toggle reverse
            buf_pos += snprintf(&buf[buf_pos], sizeof(buf) - buf_pos, "{reverse}C{reverse-off}");
            break;
         }
         case 0x04: {
            buf_pos += snprintf(&buf[buf_pos], sizeof(buf) - buf_pos, "{reverse}H{reverse-off}");
            break;
         }
         case 0x0F: {
            // Reset
            buf_pos += snprintf(&buf[buf_pos], sizeof(buf) - buf_pos, "{reset}");
            break;
         }
         case 0x16: {
            // Ctrl-V reverse toggle
            buf_pos += snprintf(&buf[buf_pos], sizeof(buf) - buf_pos, "{reverse}V{reverse-off}");
            break;
         }
         case 0x1D: {
            // Ctrl-] italic
            buf_pos += snprintf(&buf[buf_pos], sizeof(buf) - buf_pos, "{italic}I{italic-off}");
            break;
         }
         case 0x1F: {
            // Ctrl-_ underline toggle
            buf_pos += snprintf(&buf[buf_pos], sizeof(buf) - buf_pos, "{underline}_{underline-off}");
            break;
         }
         default: {
            if (c >= 0x20 && c <= 0x7E) {
               buf[buf_pos] = c;
               buf_pos++;
            } else {
               buf[buf_pos] = '?';
               buf_pos++;
            }
            break;
         }
      }

      cols++;                   // each input char counts as 1 column
      screen_cols[i] = cols;
   }

   buf[buf_pos] = '\0';

   // --- determine cursor_screen_pos ---
   int cursor_screen_pos = 0;

   if (tui_cursor_pos == 0) {
      cursor_screen_pos = 0;
   } else if (tui_cursor_pos < tui_input_len) {
      cursor_screen_pos = screen_cols[tui_cursor_pos - 1];
   } else if (tui_cursor_pos == tui_input_len && tui_input_len > 0) {
      cursor_screen_pos = screen_cols[tui_input_len - 1] + 1;
   }
   // --- colorize entire line ---
   char *colorized_line = tui_colorize_string(buf);

   // --- compute visible slice ---
   char slice[2048];
   int max_input_width = width - prompt_len - 1;

   if (max_input_width < 1) {
      max_input_width = 1;
   } else if ((size_t)max_input_width >= sizeof(slice)) {
      max_input_width = (int)sizeof(slice) - 1;
   }
   int start_col = 0;

   if (cursor_screen_pos > max_input_width) {
      start_col = cursor_screen_pos - max_input_width;
   }
   ansi_copy_columns(colorized_line + ansi_column_offset(colorized_line, start_col), max_input_width, slice, sizeof(slice));

   // --- prepare colored prompt ---
   char prompt[512];
   snprintf(prompt, sizeof(prompt), "\00311%s\00310>\017 ", prompt_title);
   char *color_prompt = tui_colorize_string(prompt);

   // --- redraw line ---
   printf("\033[%d;%dH",input_row,input_col);
   rrui_tui_theme_style(RRUI_THEME_TEXT,RRUI_THEME_INPUT);
   for(int j=0;j<width;j++)putchar(' ');
   printf("\033[%d;%dH",input_row,input_col);
   if(prompt_len<width)printf("%s%s",color_prompt,slice);
   else fputs("> ",stdout);   // prompt includes space now

   // if there's nothing typed by the user, skip forward one character, to leave
   // a space
   if (strlen(slice) <= 0) {
      prompt_len++;
   }
   // --- move cursor ---
   int cursor_screen = cursor_screen_pos - start_col + prompt_len;
   fputs("\033[0m",stdout);
   printf("\033[%d;%dH",input_row,input_col-1+CLAMP(cursor_screen,1,width));

   free(color_prompt);
   free(colorized_line);
   fflush(stdout);
   tui_render_unlock();
}
