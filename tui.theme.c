// tui.theme.c
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
#include <ctype.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

bool cfg_tui_colors = true;

static const ansi_entry_t ansi_table[] = {
   {
      "reset", "\033[0m"
   },
// Attributes
   {
      "bold", "\033[1m"
   },
   {
      "dim", "\033[2m"
   },
   {
      "italic", "\033[3m"
   },
   {
      "underline", "\033[4m"
   },
   {
      "blink", "\033[5m"
   },
   {
      "reverse", "\033[7m"
   },
   {
      "hidden", "\033[8m"
   },
   {
      "strike", "\033[9m"
   },
   {
      "bold-off", "\033[22m"
   },                                                         // turns off bold/dim
   {
      "dim-off", "\033[22m"
   },                                                        // same as bold-off
   {
      "italic-off", "\033[23m"
   },                                                           // turns off italic
   {
      "underline-off", "\033[24m"
   },                                                              // turns off underline
   {
      "blink-off", "\033[25m"
   },                                                          // turns off blink
   {
      "reverse-off", "\033[27m"
   },                                                            // turns off
                                                                 // reverse/inverse
   {
      "hidden-off", "\033[28m"
   },                                                           // turns off hidden
   {
      "strike-off", "\033[29m"
   },                                                           // turns off
                                                                // strike-through
   {
      NULL, NULL
   }
};

/* XXX: How do we deal with this properly?
 *  static struct tui_theme_data[] = {
 *  { "bgcolor",     "bright-black" },
 *  { "chans",    "bright-magenta" },
 *  { "clock.digit", "cyan" },
 *  { "clock.seperator",   "bright-black" },
 *  { "msg.connected",  "bright-green" },
 *  { "msg.offline", "bright-red" },
 *  { "nicks",    "bright-cyan" },
 *  { "normal",      "white" },
 *  {
 *  };
 */

static const char *ansi_code(const char *tag) {
   for (int i = 0 ; ansi_table[i].tag ; i++) {
      if (strcmp(tag, ansi_table[i].tag) == 0) {
         return ansi_table[i].code;
      }
   }

   return NULL;
}

char *tui_colorize_string(const char *in) {
   if (!in) {
      return NULL;
   }
   // Convert IRC controls to terminal ANSI before handling TUI style tags.
   char *irc = strpbrk(in, "\003\002\021\026\035\036\037\017") ? irc_to_tui_colors(in) : NULL;
   bool converted_irc = irc != NULL;
   if (irc) in = irc;
   size_t len = strlen(in);
   char *out = malloc(len * 8 + 64);  // enough for ANSI codes

   if (!out) {
      free(irc);
      return NULL;
   }
   const char *p = in;
   char *o = out;

   while (*p) {
      if ( (unsigned char)*p == 0x1b ) {
         /* fwdsp/GStreamer may emit ANSI CSI styling. The TUI owns the terminal stream,
          * so consume those sequences instead of allowing them to corrupt the line
          * renderer. */
         p++;

         if (*p == '[') {
            const char *sequence_start = p - 1;
            p++;
            while (*p && !isalpha((unsigned char)*p)) p++;
            if (*p) p++;
            if (converted_irc && cfg_tui_colors) {
               size_t sequence_length = (size_t)(p - sequence_start);
               memcpy(o, sequence_start, sequence_length);
               o += sequence_length;
            }
         }
      } else if (*p == '{') {
         const char *end = strchr(p, '}');

         if (!end) {
            *o++ = *p++;
            continue;
         }
         size_t key_len = end - (p + 1);
         char key[64];

         if (key_len >= sizeof(key) ) {
            key_len = sizeof(key) - 1;
         }
         memcpy(key, p + 1, key_len);
         key[key_len] = '\0';

         bool tag_handled = false;

         const char *code = ansi_code(key);
         if (code && cfg_tui_colors) o += sprintf(o, "%s", code);
         if (code) tag_handled = true;

         if (tag_handled) {
            p = end + 1;
         } else {
            // Preserve ordinary brace-delimited text verbatim.
            *o++ = *p++;
         }
      } else {
         *o++ = *p++;
      }
   }
   *o = '\0';

   free(irc);
   return out;
}

char *irc_to_tui_colors(const char *in) {
   if (!in) {
      return NULL;
   }
   static const char *foreground[] = {
      "97", "30", "34", "32", "91", "38;5;94", "35", "38;5;208",
      "93", "92", "36", "96", "94", "95", "90", "37"
   };
   static const char *background[] = {
      "107", "40", "44", "42", "101", "48;5;94", "45", "48;5;208",
      "103", "102", "46", "106", "104", "105", "100", "47"
   };
   char *out = malloc(strlen(in) * 32 + 64);

   if (!out) {
      return NULL;
   }
   const unsigned char *input_cursor = (const unsigned char *)in;
   char *output_cursor = out;
   bool bold = false, underline = false, reverse = false, italic = false;
   bool strikethrough = false;
   while (*input_cursor) {
      unsigned char control = *input_cursor++;
      if (control == 0x03) {
         int foreground_index = -1, background_index = -1;
         if (isdigit(*input_cursor)) {
            foreground_index = *input_cursor++ - '0';
            if (isdigit(*input_cursor)) foreground_index = foreground_index * 10 + (*input_cursor++ - '0');
         }
         if (*input_cursor == ',' && isdigit(input_cursor[1])) {
            input_cursor++;
            background_index = *input_cursor++ - '0';
            if (isdigit(*input_cursor)) background_index = background_index * 10 + (*input_cursor++ - '0');
         }
         if (foreground_index < 0 && background_index < 0) {
            output_cursor += sprintf(output_cursor, "\033[39;49m");
         } else {
            if (foreground_index >= 0 && foreground_index < 16)
               output_cursor += sprintf(output_cursor, "\033[%sm", foreground[foreground_index]);
            if (background_index >= 0 && background_index < 16)
               output_cursor += sprintf(output_cursor, "\033[%sm", background[background_index]);
         }
      } else {
         switch (control) {
            case 0x02: output_cursor += sprintf(output_cursor, bold ? "\033[22m" : "\033[1m"); bold = !bold; break;
            case 0x0f:
               output_cursor += sprintf(output_cursor, "\033[0m");
               bold = underline = reverse = italic = strikethrough = false;
               break;
            case 0x16: output_cursor += sprintf(output_cursor, reverse ? "\033[27m" : "\033[7m"); reverse = !reverse; break;
            case 0x1d: output_cursor += sprintf(output_cursor, italic ? "\033[23m" : "\033[3m"); italic = !italic; break;
            case 0x1e: output_cursor += sprintf(output_cursor, strikethrough ? "\033[29m" : "\033[9m"); strikethrough = !strikethrough; break;
            case 0x1f: output_cursor += sprintf(output_cursor, underline ? "\033[24m" : "\033[4m"); underline = !underline; break;
            case 0x11: break; // Terminal text is already monospace.
            default: *output_cursor++ = (char)control; break;
         }
      }
   }
   *output_cursor = '\0';

   return out;
}

void tui_vprint(tui_window_t *win, const char *fmt, va_list ap) {
   tui_render_lock();

   if (!tui_is_enabled || !win || !fmt) {
      tui_render_unlock();

      return;
   }

   char msgbuf[2048];

   va_list aq;
   va_copy(aq, ap);
   vsnprintf(msgbuf, sizeof(msgbuf), fmt, aq);
   va_end(aq);

   char *colored = tui_colorize_string(msgbuf);

   if (!colored) {
      tui_render_unlock();

      return;
   }

   char *line = strdup(colored);
   free(colored);

   if (!line) {
      tui_render_unlock();

      return;
   }

   if (win->buffer[win->log_head]) {
      free(win->buffer[win->log_head]);
   }

   win->buffer[win->log_head] = line;
   win->log_head = (win->log_head + 1) % LOG_LINES;

   if (win->log_count < LOG_LINES) {
      win->log_count++;
   }

   /* Only active-window output changes the visible scrollback.  In particular, logging
    * performed by a top/status-line renderer must not start a nested full-screen redraw
    * while the current frame is being painted.  Inactive windows will be rendered when
    * they are focused. */
   if ( win == tui_active_window() ) {
      tui_redraw_request();

      if (tui_redraw_defer_count == 0) {
         tui_redraw_if_pending();
      }
   }
   tui_render_unlock();
}

void tui_print(tui_window_t *win, const char *fmt, ...) {
   if (!tui_is_enabled || !win || !fmt) {
      return;
   }

   va_list ap;
   va_start(ap, fmt);
   tui_vprint(win, fmt, ap);
   va_end(ap);
}

char *strip_mirc_formatting(const char *input) {
   if (!input) {
      return NULL;
   }
   size_t len = strlen(input);
   char *out = malloc(len + 1);

   if (!out) {
      return NULL;
   }
   const char *p = input;
   char *q = out;

   while (*p) {
      unsigned char c = *p;

      if (c == 0x02  // Bold
          || c == 0x1D // Italic
          || c == 0x1F // Underline
          || c == 0x0F // Reset
          || c == 0x11 // Reverse (mIRC specific)
          ) {
         p++;  // skip formatting
      } else if (c == 0x03) {
         // Color
         p++;

         // skip up to two digits for foreground
         if (isdigit( (unsigned char)*p ) ) {
            p++;
         }

         if (isdigit( (unsigned char)*p ) ) {
            p++;
         }

         // optionally skip comma and up to two digits for background
         if (*p == ',') {
            p++;

            if (isdigit( (unsigned char)*p ) ) {
               p++;
            }

            if (isdigit( (unsigned char)*p ) ) {
               p++;
            }
         }
      } else {
         *q++ = *p++;
      }
   }
   *q = '\0';

   return out;
}
