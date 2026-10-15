/* IRC formatting syntax shared by native renderers; no frontend state. */
#ifndef LIBRUSTYAXE_IRC_FORMAT_H
#define LIBRUSTYAXE_IRC_FORMAT_H
#include <stdbool.h>
#include <stddef.h>

typedef struct {
   bool hex, reset, background;
   unsigned foreground, bg;
} irc_color_t;

/* Includes the control byte. Invalid/incomplete parameters remain visible. */
size_t irc_color_parse(const char *input, irc_color_t *color);
const char *irc_color_rgb(unsigned index); /* 99 selects the default. */
unsigned irc_color_ansi_extended(unsigned index); /* Indices 16..98. */
#endif
