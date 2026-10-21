//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librustyaxe/irc.format.h>

/* https://modern.ircdocs.horse/formatting: fixed extended mIRC palette. */
static const char *const palette[] = {
   "#ffffff", "#000000", "#00007f", "#009300", "#ff0000", "#7f0000", "#9c009c", "#fc7f00",
   "#ffff00", "#00fc00", "#009393", "#00ffff", "#0000fc", "#ff00ff", "#7f7f7f", "#d2d2d2",
   "#470000", "#472100", "#474700", "#324700", "#004700", "#00472c", "#004747", "#002747",
   "#000047", "#2e0047", "#470047", "#47002a", "#740000", "#743a00", "#747400", "#517400",
   "#007400", "#007449", "#007474", "#004074", "#000074", "#4b0074", "#740074", "#740045",
   "#b50000", "#b56300", "#b5b500", "#7db500", "#00b500", "#00b571", "#00b5b5", "#0063b5",
   "#0000b5", "#7500b5", "#b500b5", "#b5006b", "#ff0000", "#ff8c00", "#ffff00", "#b2ff00",
   "#00ff00", "#00ffa0", "#00ffff", "#008cff", "#0000ff", "#a500ff", "#ff00ff", "#ff0098",
   "#ff5959", "#ffb459", "#ffff71", "#cfff60", "#6fff6f", "#65ffc9", "#6dffff", "#59b4ff",
   "#5959ff", "#c459ff", "#ff66ff", "#ff59bc", "#ff9c9c", "#ffd39c", "#ffff9c", "#e2ff9c",
   "#9cff9c", "#9cffdb", "#9cffff", "#9cd3ff", "#9c9cff", "#dc9cff", "#ff9cff", "#ff94d3",
   "#000000", "#131313", "#282828", "#363636", "#4d4d4d", "#656565", "#818181", "#9f9f9f",
   "#bcbcbc", "#e2e2e2", "#ffffff",
};

const char *irc_color_rgb(unsigned index) {
   return index < 99 ? palette[index] : NULL;
}

unsigned irc_color_ansi_extended(unsigned index) {
   static const unsigned char ansi[] = {
      52, 94, 100, 58, 22, 29, 23, 24, 17, 54, 53, 89,
      88, 130, 142, 64, 28, 35, 30, 25, 18, 91, 90, 125,
      124, 166, 184, 106, 34, 49, 37, 33, 19, 129, 127, 161,
      196, 208, 226, 154, 46, 86, 51, 75, 21, 171, 201, 198,
      203, 215, 227, 191, 83, 122, 87, 111, 63, 177, 207, 205,
      217, 223, 229, 193, 157, 158, 159, 153, 147, 183, 219, 212,
      16, 233, 235, 237, 239, 241, 244, 247, 250, 254, 231,

   };
   return index >= 16 && index <= 98 ? ansi[index - 16] : 0;
}

static size_t color_component(const char *p, bool hex, unsigned *value) {
   unsigned result = 0;
   size_t count = 0, limit = hex ? 6 : 2;
   for (; count < limit; count++) {
      unsigned char c = (unsigned char)p[count];
      unsigned digit;
      if (c >= '0' && c <= '9') {
         digit = c - '0';
      } else if (hex && c >= 'a' && c <= 'f') {
         digit = c - 'a' + 10;
      } else if (hex && c >= 'A' && c <= 'F') {
         digit = c - 'A' + 10;
      } else {
         break;
      }
      result = result * (hex ? 16 : 10) + digit;
   }
   if (hex && count != 6) {
      return 0;
   }
   *value = result;
   return count;
}

size_t irc_color_parse(const char *input, irc_color_t *color) {
   *color = (irc_color_t){0};
   if (!input || (*input != 0x03 && *input != 0x04)) {
      return 0;
   }
   color->hex = *input == 0x04;
   size_t foreground = color_component(input + 1, color->hex, &color->foreground);
   if (!foreground) {
      color->reset = true;
      return 1;
   }
   size_t used = 1 + foreground;
   if (input[used] == ',') {
      size_t background = color_component(input + used + 1, color->hex, &color->bg);
      if (background) {
         color->background = true;
         used += 1 + background;
      }
   }
   return used;
}
