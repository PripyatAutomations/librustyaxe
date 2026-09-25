// tui.keys.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Socket backend for io subsys
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <termios.h>
#include <unistd.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librustyaxe/termkey.h>
#include <glib.h>
#include <glib-unix.h>
#include <fcntl.h>

extern int tui_window_swap(int c, int key);
extern int handle_alt_left(int c, int key);
extern int handle_alt_right(int c, int key);

static struct termios orig_termios;
char input_buf[TUI_INPUTLEN];
int tui_input_len = 0;
int tui_cursor_pos = 0;
static char input_history[HISTORY_LINES][TUI_INPUTLEN];
static int history_count = 0;
static int history_index = -1;

const char *history_prev(void)
{
   if (history_count == 0)
   {
      return NULL;
   }

   if (history_index < 0)
   {
      history_index = history_count - 1;
   }
   else if (history_index > 0)
   {
      history_index--;
   }

   return input_history[history_index];
}

const char *history_next(void)
{
   if (history_count == 0 || history_index < 0)
   {
      return NULL;
   }
   history_index++;

   if (history_index >= history_count)
   {
      history_index = -1;

      return "";
   }

   return input_history[history_index];
}

void history_add(const char *line)
{
   if (!line || !*line)
   {
      return;
   }

   if (history_count >= HISTORY_LINES)
   {
      memmove(input_history, input_history + 1, sizeof(input_history[0]) * (HISTORY_LINES - 1));
      history_count--;
   }
   strlcpy(input_history[history_count++], line, TUI_INPUTLEN);
   input_history[history_count - 1][TUI_INPUTLEN - 1] = '\0';
   history_index = history_count;
}

// --- PgUp / PgDn handlers with partial last page support ---
int handle_pgup(int count, int key)
{
   tui_window_t *w = tui_active_window();

   if (!w)
   {
      return 0;
   }
   int page = tui_rows() - 3; // screen minus status+input

   if (page < 1)
   {
      page = 1;
   }
   int max_scroll = (w->log_count > page) ? (w->log_count - page) : 0;

   if (w->scroll_offset + page > max_scroll)
   {
      w->scroll_offset = max_scroll; // stop at top of buffer
   }
   else
   {
      w->scroll_offset += page;
   }
   tui_redraw_screen();

   return 0;
}

int handle_pgdn(int count, int key)
{
   tui_window_t *w = tui_active_window();

   if (!w)
   {
      return 0;
   }
   int page = tui_rows() - 3;

   if (page < 1)
   {
      page = 1;
   }

   if (w->scroll_offset - page < 0)
   {
      w->scroll_offset = 0; // stop at bottom of buffer
   }
   else
   {
      w->scroll_offset -= page;
   }
   tui_redraw_screen();

   return 0;
}

int handle_ptt_button(int count, int key)
{
   tui_window_t *w = tui_active_window();

   if (!w)
   {
      return 0;
   }
   tui_print(w, "* F13 (PTT) pressed!");

   return 0;
}

void tui_raw_mode(bool enabled)
{
   if (enabled)
   {
      struct termios raw;
      tcgetattr(STDIN_FILENO, &orig_termios);

      raw = orig_termios;
      raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
      raw.c_iflag &= ~(IXON | ICRNL);
      raw.c_oflag &= ~(OPOST);
      raw.c_cc[VMIN] = 1;
      raw.c_cc[VTIME] = 0;

      cfmakeraw(&raw);
      tcsetattr(STDIN_FILENO, TCSANOW, &raw);
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
   }
   else
   {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
   }
}

extern bool irc_input_cb(const char *input);
void handle_enter_key(tui_window_t *win, int cursor)
{
   //   Log(LOG_CRIT, "tui.keys", "ENTER: %s", input_buf);
   if (!win)
   {
      return;
   }
   input_buf[tui_input_len] = '\0';

   if (tui_input_len > 0)
   {
      history_add(input_buf);

      if (tui_readline_cb)
      {
         tui_readline_cb(input_buf);
      }
      else
      {
         Log(LOG_DEBUG, "tui.keys", "no tui_readline_cb");
      }
      tui_input_len = 0;
      input_buf[0] = '\0';
   }
   cursor = 0;
   tui_update_input_line();
}

//////////////
bool (*tui_readline_cb)(const char *input) = NULL;

static TermKey *tk = NULL;
static guint stdin_watch_id = 0;

#define TUI_HOTKEY_MAX 32
struct tui_hotkey_binding {
   unsigned key;
   unsigned modifiers;
   tui_hotkey_cb_t callback;
   void *user_data;
};
static struct tui_hotkey_binding hotkeys[TUI_HOTKEY_MAX];

bool tui_hotkey_register(unsigned key, unsigned modifiers, tui_hotkey_cb_t callback,
   void *user_data) {
   if (!callback) return false;
   for (unsigned i = 0; i < TUI_HOTKEY_MAX; i++) {
      if (hotkeys[i].callback && hotkeys[i].key == key && hotkeys[i].modifiers == modifiers) {
         hotkeys[i] = (struct tui_hotkey_binding){ key, modifiers, callback, user_data };
         return true;
      }
   }
   for (unsigned i = 0; i < TUI_HOTKEY_MAX; i++) {
      if (!hotkeys[i].callback) {
         hotkeys[i] = (struct tui_hotkey_binding){ key, modifiers, callback, user_data };
         return true;
      }
   }
   return false;
}

bool tui_hotkey_unregister(unsigned key, unsigned modifiers, tui_hotkey_cb_t callback,
   void *user_data) {
   for (unsigned i = 0; i < TUI_HOTKEY_MAX; i++) {
      if (hotkeys[i].callback == callback && hotkeys[i].user_data == user_data &&
          hotkeys[i].key == key && hotkeys[i].modifiers == modifiers) {
         hotkeys[i].callback = NULL;
         return true;
      }
   }
   return false;
}

bool tui_hotkey_dispatch(tui_window_t *win, unsigned key, unsigned modifiers) {
   for (unsigned i = 0; i < TUI_HOTKEY_MAX; i++) {
      if (hotkeys[i].callback && hotkeys[i].key == key &&
          (modifiers & hotkeys[i].modifiers) == hotkeys[i].modifiers) {
         return hotkeys[i].callback(win, key, modifiers, hotkeys[i].user_data);
      }
   }
   return false;
}

// GLib fd source: called when stdin is readable
static gboolean stdin_ev_cb(gint fd, GIOCondition condition, gpointer data);

void tui_keys_init(void)
{
   tk = termkey_new(STDIN_FILENO, TERMKEY_FLAG_CTRLC | TERMKEY_FLAG_RAW);
   termkey_set_canonflags(tk, TERMKEY_CANON_DELBS);
   termkey_set_flags(tk, termkey_get_flags(tk) | TERMKEY_FLAG_NOTERMIOS);

   // stdin must be non-blocking for the GLib fd source
   fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);

   stdin_watch_id = g_unix_fd_add(STDIN_FILENO, G_IO_IN, stdin_ev_cb, NULL);
}

static gboolean stdin_ev_cb(gint fd, GIOCondition condition, gpointer data)
{
   TermKeyResult res;
   TermKeyKey key;

   termkey_advisereadable(tk);

   while ((res = termkey_getkey(tk, &key)) != TERMKEY_RES_NONE)
   {
      if (res == TERMKEY_RES_EOF)
      {
         break;
      }

      if (res == TERMKEY_RES_AGAIN)
      {
         return TRUE;
      }
      tui_window_t *win = tui_active_window();

      if (!win)
      {
         continue;
      }
      int handled = 0;

      // --- compute 'c' for debug / line editing ---
      int c = 0;

      if (key.type == TERMKEY_TYPE_UNICODE)
      {
         c = key.code.codepoint;
      }
      else if (key.type == TERMKEY_TYPE_KEYSYM)
      {
         switch (key.code.sym)
         {
         case TERMKEY_SYM_BACKSPACE:
         {
            c = 0x08;
            break;
         }
         case TERMKEY_SYM_DELETE:
         {
            c = TERMKEY_SYM_DELETE;
            break;
         }
         case TERMKEY_SYM_ENTER:
         {
            c = '\n';
            break;
         }
         default:
         {
            c = key.code.sym;
            break;
         }
         }
      }

      // --- log the key for debugging ---
      //      Log(LOG_DEBUG, "tui.key", "key: type=%d code=%d mod=%d c=%d", key.type,
      // key.code.codepoint, key.modifiers, c);
      // --- Hotkeys / special keys ---
      // Ctrl-Space is reported as a control character by some terminals and
      // as a literal space with the CTRL modifier by others.
      if (key.type == TERMKEY_TYPE_UNICODE && (key.modifiers & TERMKEY_KEYMOD_CTRL) &&
          (key.code.codepoint == 0 || key.code.codepoint == ' ')) {
         handled = tui_hotkey_dispatch(win, key.code.codepoint, key.modifiers);
      }
      if (key.type == TERMKEY_TYPE_KEYSYM)
      {
         switch (key.code.sym)
         {
         case TERMKEY_SYM_ENTER:
         {
            if (key.modifiers & TERMKEY_KEYMOD_ALT) {
               handled = tui_hotkey_dispatch(win, key.code.sym, key.modifiers);
            } else {
               handle_enter_key(win, 0);
               tui_input_len = 0;
               tui_cursor_pos = 0;
               memset(input_buf, 0, sizeof(input_buf));
               handled = 1;
            }
            break;
         }

         case TERMKEY_SYM_TAB:
         {
            if (!(key.modifiers & (TERMKEY_KEYMOD_CTRL | TERMKEY_KEYMOD_ALT | TERMKEY_KEYMOD_SHIFT)))
            {
               handled = tui_do_completion(win);
            }
            break;
         }

         case TERMKEY_SYM_PAGEUP:
         {
            handled = handle_pgup(1, key.code.sym);
            break;
         }

         case TERMKEY_SYM_PAGEDOWN:
         {
            handled = handle_pgdn(1, key.code.sym);
            break;
         }

         case TERMKEY_SYM_HOME:
         {
            tui_cursor_pos = 0;
            handled = 1;
            break;
         }

         case TERMKEY_SYM_END:
         {
            tui_cursor_pos = tui_input_len;
            handled = 1;
            break;
         }

         case TERMKEY_SYM_LEFT:
         {
            if (key.modifiers & TERMKEY_KEYMOD_CTRL)
            {
               // move cursor to start of previous word
               while (tui_cursor_pos > 0 && input_buf[tui_cursor_pos - 1] == ' ')
               {
                  tui_cursor_pos--;
               }
               while (tui_cursor_pos > 0 && input_buf[tui_cursor_pos - 1] != ' ')
               {
                  tui_cursor_pos--;
               }
               handled = 1;
            }
            else if (key.modifiers & TERMKEY_KEYMOD_ALT)
            {
               handled = handle_alt_left(1, key.code.sym);
            }
            else
            {
               if (tui_cursor_pos > 0)
               {
                  tui_cursor_pos--;
               }
               handled = 1;
            }
            break;
         }

         case TERMKEY_SYM_RIGHT:
         {
            if (key.modifiers & TERMKEY_KEYMOD_CTRL)
            {
               // move cursor to start of next word
               while (tui_cursor_pos < tui_input_len && input_buf[tui_cursor_pos] != ' ')
               {
                  tui_cursor_pos++;
               }
               while (tui_cursor_pos < tui_input_len && input_buf[tui_cursor_pos] == ' ')
               {
                  tui_cursor_pos++;
               }
               handled = 1;
            }
            else if (key.modifiers & TERMKEY_KEYMOD_ALT)
            {
               handled = handle_alt_right(1, key.code.sym);
            }
            else
            {
               if (tui_cursor_pos < tui_input_len)
               {
                  tui_cursor_pos++;
               }
               handled = 1;
            }
            break;
         }

         case TERMKEY_SYM_UP:
         {
            const char *prev = history_prev();

            if (prev)
            {
               strlcpy(input_buf, prev, TUI_INPUTLEN);
               tui_input_len = strlen(input_buf);
               input_buf[tui_input_len] = '\0';
               tui_cursor_pos = tui_input_len;
            }
            handled = 1;
            break;
         }

         case TERMKEY_SYM_DOWN:
         {
            const char *next = history_next();

            if (next)
            {
               strlcpy(input_buf, next, TUI_INPUTLEN);
               tui_input_len = strlen(next);
               input_buf[tui_input_len] = '\0';
               tui_cursor_pos = tui_input_len;
            }
            handled = 1;
            break;
         }
         default:
         {
            if ((key.modifiers & TERMKEY_KEYMOD_ALT) &&
                key.code.sym >= '0' && key.code.sym <= '9')
            {
               handled = tui_window_swap(1, key.code.sym - '0');
            }
            break;
         }
         }
      }

      /* Escape-prefixed letters are reported as Alt+Unicode by termkey.
       * Continue the Alt-1..Alt-0 window sequence with the QWERTY home row:
       * Esc-Q selects window 11 through Esc-P selecting window 20. */
      if (!handled && key.type == TERMKEY_TYPE_UNICODE &&
          (key.modifiers & TERMKEY_KEYMOD_ALT)) {
         const char *window_keys = "qwertyuiop";
         const char *match = strchr(window_keys, (int)key.code.codepoint);
         if (match) {
            int window_id = 11 + (int)(match - window_keys);
            (void)tui_window_focus_id(window_id);
            handled = 1;
            tui_redraw_screen();
         }
      }

      // --- Ctrl line editing ---
      if (!handled && (key.modifiers & TERMKEY_KEYMOD_CTRL))
      {
         char insert = 0;

         switch (c)
         {
         case '_':
         {
            insert = 0x1F; // Underline (alternate)
            break;
         }
         case 'A':
         case 'a':
         case 0x01:                 // Ctrl-A
         {
            tui_cursor_pos = 0;
            handled = 1;
            break;
         }
         case 'B':
         case 'b':
         {
            insert = 0x02; // Bold
            break;
         }
         case 'C':
         case 'c':
         {
            insert = 0x03; // Color code
            break;
         }
         case 'E':
         case 'e':
         case 0x05:                 // Ctrl-E
         {
            tui_cursor_pos = tui_input_len;
            handled = 1;
            break;
         }
         case 'I':
         case 'i':
         {
            insert = 0x1D; // Italic
            break;
         }
         case 'O':
         case 'o':
         {
            insert = 0x0F; // Reset
            break;
         }
         case 'R':
         case 'r':
         {
            insert = 0x16; // Reverse (not well supported)
            break;
         }
         case 'U':
         case 'u':
         {
            tui_input_len = 0;
            tui_cursor_pos = 0;
            input_buf[0] = '\0';
            handled = 1;
            break;
         }
         case 'W':
         case 'w':
         case 0x17:                 // Ctrl-W
         {
            if (tui_cursor_pos > 0)
            {
               int i = tui_cursor_pos - 1;
               while (i >= 0 && input_buf[i] == ' ')
               {
                  i--;
               }
               while (i >= 0 && input_buf[i] != ' ')
               {
                  i--;
               }
               int start = i + 1;
               memmove(&input_buf[start], &input_buf[tui_cursor_pos], tui_input_len - tui_cursor_pos + 1);
               tui_input_len -= (tui_cursor_pos - start);
               tui_cursor_pos = start;
            }
            handled = 1;
            break;
         }
         case 'D':
         case 'd':
         case 0x04:                 // Ctrl-D: delete forward
         {
            if (tui_cursor_pos < tui_input_len) {
               memmove(&input_buf[tui_cursor_pos], &input_buf[tui_cursor_pos + 1],
                  tui_input_len - tui_cursor_pos);
               tui_input_len--;
            }
            handled = 1;
            break;
         }
         case 'K':
         case 'k':
         case 0x0b:                 // Ctrl-K: kill to end of line
         {
            input_buf[tui_cursor_pos] = '\0';
            tui_input_len = tui_cursor_pos;
            handled = 1;
            break;
         }
         case 'X':
         case 'x':
         {
            // Switch to the next server
            break;
         }
         case 0x08:
         {
            // Ctrl-H
            if (tui_cursor_pos > 0)
            {
               memmove(&input_buf[tui_cursor_pos - 1], &input_buf[tui_cursor_pos], tui_input_len - tui_cursor_pos + 1);
               tui_cursor_pos--;
               tui_input_len--;
            }
            handled = 1;
            break;
         }
         }

         if (insert && tui_input_len < TUI_INPUTLEN - 1)
         {
            memmove(&input_buf[tui_cursor_pos + 1], &input_buf[tui_cursor_pos], tui_input_len - tui_cursor_pos + 1);
            input_buf[tui_cursor_pos] = insert;
            tui_cursor_pos++;
            tui_input_len++;
            handled = 1;
         }
      }

      // --- Backspace / Delete / Enter ---
      if (!handled)
      {
         switch (c)
         {
         case '\n':
         {
            handle_enter_key(win, 0);
            tui_input_len = tui_cursor_pos = 0;
            input_buf[0] = '\0';
            handled = 1;
            break;
         }
         case 0x08:
         case 0x7f:
         {
            if (tui_cursor_pos > 0)
            {
               memmove(&input_buf[tui_cursor_pos - 1], &input_buf[tui_cursor_pos], tui_input_len - tui_cursor_pos + 1);
               tui_cursor_pos--;
               tui_input_len--;
            }
            handled = 1;
            break;
         }
         case '1':
         case '2':
         case '3':
         case '4':
         case '5':
         case '6':
         case '7':
         case '8':
         case '9':
         case '0':
         {
            if (key.modifiers & TERMKEY_KEYMOD_ALT)
            {
               tui_window_swap(1, c);
               handled = 1;
            }
            break;
         }
         case TERMKEY_SYM_DELETE:
         {
            if (tui_cursor_pos < tui_input_len)
            {
               memmove(&input_buf[tui_cursor_pos], &input_buf[tui_cursor_pos + 1], tui_input_len - tui_cursor_pos);
               tui_input_len--;
            }
            handled = 1;
            break;
         }
         }
      }

      // --- Insert printable Unicode ---
      if (!handled && key.type == TERMKEY_TYPE_UNICODE && c >= 0x20)
      {
         if (tui_input_len < TUI_INPUTLEN - 1)
         {
            memmove(&input_buf[tui_cursor_pos + 1], &input_buf[tui_cursor_pos], tui_input_len - tui_cursor_pos + 1);
            input_buf[tui_cursor_pos++] = c;
            tui_input_len++;
         }
         handled = 1;
      }

      // --- Redraw after any modification ---
      if (handled)
      {
         tui_update_input_line();
      }
   }

   return G_SOURCE_CONTINUE;
}
