/*
 * librustyaxe/color.c: shared color parsing/conversion helpers.
 *
 *    This is part of rustyrig-fw.
 * https://github.com/pripyatautomations/rustyrig-fw
 *
 * Do not pay money for this, except donations to the project, if you wish to.
 * The software is not for sale. It is freely available, always.
 *
 * Licensed under MIT license, if built without mongoose or GPL if built with.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <librustyaxe/core.h>
#include <librustyaxe/color.h>

// The 16 base ANSI foreground colors, in ascending ANSI code order
static const struct {
   const char *name;
   int r, g, b;
} base16[] = {
   { "black",           0,   0,   0 },
   { "red",           205,   0,   0 },
   { "green",           0, 205,   0 },
   { "yellow",        205, 205,   0 },
   { "blue",            0,   0, 238 },
   { "magenta",       205,   0, 205 },
   { "cyan",            0, 205, 205 },
   { "white",         229, 229, 229 },
   { "bright-black",  127, 127, 127 },
   { "bright-red",    255,   0,   0 },
   { "bright-green",    0, 255,   0 },
   { "bright-yellow", 255, 255,   0 },
   { "bright-blue",    92,  92, 255 },
   { "bright-magenta",255,   0, 255 },
   { "bright-cyan",     0, 255, 255 },
   { "bright-white",  255, 255, 255 },
};

// Parse 1-2 hex digits. Returns digits consumed, 0 on failure.
// A single digit expands to the doubled form (#f == #ff), per CSS convention.
static int hexdigit(const char *p, int *out) {
   int hi = -1, lo = -1;

   if (!p || !*p) {
      return 0;
   }
   char c = tolower( (unsigned char)p[0]);
   if (c >= '0' && c <= '9') hi = c - '0';
   else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
   else return 0;

   if (p[1]) {
      char c2 = tolower( (unsigned char)p[1]);
      if (c2 >= '0' && c2 <= '9') lo = c2 - '0';
      else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10;
   }

   if (lo >= 0) {
      *out = hi * 16 + lo;
      return 2;
   }
   *out = hi * 16 + hi;
   return 1;
}

bool color_parse_hex(const char *hex, int *r, int *g, int *b) {
   if (!hex || hex[0] != '#') {
      return false;
   }

   size_t len = strlen(hex);

   // Must be exactly #rgb (short form, digits doubled) or #rrggbb; reject
   // anything else (mixed lengths like #ab2ef, garbage, etc)
   if (len != 4 && len != 7) {
      return false;
   }

   int n, rv = 0, gv = 0, bv = 0;
   const char *p = hex + 1;

   if (len == 4) {
      // Short form #rgb: each single digit is doubled (a -> aa). Parse one
      // char at a time; hexdigit() is greedy and would consume pairs.
      int d;
      for (int i = 0; i < 3; i++) {
         char c = tolower( (unsigned char)p[i]);
         if (c >= '0' && c <= '9') d = c - '0';
         else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
         else return false;
         d += d * 0x10;   // double the digit
         if (i == 0) rv = d;
         else if (i == 1) gv = d;
         else bv = d;
      }
      p += 3;
   } else {
      n = hexdigit(p, &rv);
      if (!n) return false;
      p += n;
      n = hexdigit(p, &gv);
      if (!n) return false;
      p += n;
      n = hexdigit(p, &bv);
      if (!n) return false;
      p += n;
   }

   if (*p != '\0') {
      return false;
   }

   if (r) *r = rv;
   if (g) *g = gv;
   if (b) *b = bv;
   return true;
}

int color_rgb_to_ansi256(int r, int g, int b) {
   if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
      return -1;
   }

   // 24-step grayscale ramp (232-255) when channels are near-equal. Avoid
   // the ends, where the 16 base colors and cube are better matches.
   if (abs(r - g) < 8 && abs(g - b) < 8) {
      int avg = (r + g + b) / 3;

      if (avg >= 4 && avg <= 248) {
         return 232 + ( (avg - 4) * 24 / 245);
      }
   }

   // 6x6x6 cube (16-231). xterm cube levels: 0,95,135,175,215,255
   static const int levels[6] = { 0, 95, 135, 175, 215, 255 };
   int idx[3] = { 0, 0, 0 };
   int ch[3] = { r, g, b };

   // Nearest cube level per channel
   for (int c = 0; c < 3; c++) {
      int bestdist = 1 << 30;
      for (int i = 0; i < 6; i++) {
         int d = abs(ch[c] - levels[i]);
         if (d < bestdist) {
            bestdist = d;
            idx[c] = i;
         }
      }
   }

   return 16 + 36 * idx[0] + 6 * idx[1] + idx[2];
}

const char *color_nearest_named(int r, int g, int b) {
   long bestdist = -1;
   const char *best = NULL;

   for (size_t i = 0; i < sizeof(base16) / sizeof(base16[0]); i++) {
      long dr = r - base16[i].r;
      long dg = g - base16[i].g;
      long db = b - base16[i].b;
      // Weighted euclidean distance (approximates human perception)
      long dist = 2 * dr * dr + 4 * dg * dg + 3 * db * db;

      if (bestdist < 0 || dist < bestdist) {
         bestdist = dist;
         best = base16[i].name;
      }
   }
   return best;
}

bool color_tag_parse(const char *key, char *hex, size_t hexlen,
                     char *fb, size_t fblen, bool *is_bg) {
   if (!key || !*key) {
      return false;
   }

   const char *p = key;
   if (fb && fblen) fb[0] = '\0';

   // Optional bg- prefix
   bool bg = false;
   if (strncmp(p, "bg-", 3) == 0) {
      bg = true;
      p += 3;
   }

   if (*p != '#') {
      return false;
   }

   // Split off the ":fallback" suffix
   char spec[32];
   const char *colon = strchr(p, ':');

   if (colon) {
      size_t slen = (size_t)(colon - p);
      if (slen >= sizeof(spec)) {
         return false;
      }
      memcpy(spec, p, slen);
      spec[slen] = '\0';
      if (fb && fblen) {
         snprintf(fb, fblen, "%s", colon + 1);
      }
   } else {
      snprintf(spec, sizeof(spec), "%s", p);
   }

   // Canonicalize to #rrggbb (short form expands via digit doubling)
   int r, g, b;
   if (!color_parse_hex(spec, &r, &g, &b)) {
      return false;
   }
   if (hex && hexlen >= 8) {
      snprintf(hex, hexlen, "#%02x%02x%02x", r, g, b);
   }
   if (is_bg) *is_bg = bg;
   return true;
}
