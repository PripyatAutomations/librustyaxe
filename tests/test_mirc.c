#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <librustyaxe/core.h>
int main(void) {
   cfg_tui_colors = true;
   char *text = tui_colorize_string("\00304123,45\017");
   assert(!strcmp(text, "\033[91m123,45\033[0m")); free(text);
   text = tui_colorize_string("\0031,3hello\003");
   assert(!strcmp(text, "\033[30m\033[42mhello\033[39;49m")); free(text);
   text = tui_colorize_string("\037on\037off \002bold\002plain\017");
   assert(strstr(text, "\033[24moff") && strstr(text, "\033[22mplain")); free(text);
   text = tui_colorize_string("\036strike\036 \021mono");
   assert(strstr(text, "\033[9mstrike\033[29m mono")); free(text);
   text = irc_to_tui_colors("\00304, text");
   assert(!strcmp(text, "\033[91m, text")); free(text);
   cfg_tui_colors = false;
   text = tui_colorize_string("\00304Radio\017 \037text\037");
   assert(!strcmp(text, "Radio text")); free(text);
   puts("PASS: mIRC colors, numeric text, style toggles and disabled colors");
}
