#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <librustyaxe/core.h>
int main(void) {
   cfg_tui_colors = true;
   char *text = tui_colorize_string("\00304123,45\017");
   assert(!strcmp(text, "\033[91m123,45\033[0m"));
   free(text);
   text = tui_colorize_string("\0031,3hello\003");
   assert(!strcmp(text, "\033[30m\033[42mhello\033[39;49m"));
   free(text);
   text = tui_colorize_string("\037on\037off \002bold\002plain\017");
   assert(strstr(text, "\033[24moff") && strstr(text, "\033[22mplain"));
   free(text);
   text = tui_colorize_string("\036strike\036 \021mono");
   assert(strstr(text, "\033[9mstrike\033[29m mono"));
   free(text);
   text = irc_to_tui_colors("\00304, text");
   assert(!strcmp(text, "\033[91m, text"));
   free(text);

   for (unsigned i = 0 ; i < 99 ; i++) {
      assert(irc_color_rgb(i) && strlen(irc_color_rgb(i)) == 7);
      char input[32];
      snprintf(input, sizeof(input), "\003%02u,98pixel", i);
      irc_color_t color;
      assert(irc_color_parse(input, &color) == 6);
      assert(!color.hex && !color.reset && color.foreground == i && color.background && color.bg == 98);
      text = irc_to_tui_colors(input);
      assert(strstr(text, "pixel") && !strstr(text, ",98"));
      free(text);
   }

   assert(!irc_color_rgb(99));
   text = irc_to_tui_colors("\00316,98art\00399,99default");
   assert(!strcmp(text, "\033[38;5;52m\033[48;5;231mart\033[39m\033[49mdefault"));
   free(text);
   text = irc_to_tui_colors("\004Ab12eF,102030hex\00352indexed");
   assert(!strcmp(text, "\033[38;2;171;18;239m\033[48;2;16;32;48mhex\033[38;5;196mindexed"));
   free(text);
   text = strip_mirc_formatting("\004aabbcc,102030hex\00316,98art\026\036\021\017!");
   assert(!strcmp(text, "hexart!"));
   free(text);
   text = strip_mirc_formatting("\003,12text\00304,tail\00412345,abcdef\004aabbcc,12345!");
   assert(!strcmp(text, ",12text,tail12345,abcdef,12345!"));
   free(text);

   /* Every truncated RGB prefix must be safe and leave incomplete digits visible. */
   for (size_t n = 0 ; n <= 6 ; n++) {
      char input[] = "\004aAbBcC";
      input[n + 1] = '\0';
      irc_color_t color;
      assert(irc_color_parse(input, &color) == (n < 6 ? 1u : 7u));
      assert(color.reset == (n < 6));
   }

   cfg_tui_colors = false;
   text = tui_colorize_string("\004aabbccRadio\017 \037text\037");
   assert(!strcmp(text, "Radio text"));
   free(text);
   puts("PASS: classic/extended/hex IRC colors, defaults, malformed codes, stripping and disabled colors");
}
