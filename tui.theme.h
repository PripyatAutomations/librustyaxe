//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__librustyaxe_tui_theme_h)
#define __librustyaxe_tui_theme_h

typedef struct {
   const char *tag;
   const char *code;
} ansi_entry_t;

typedef struct tui_theme_data {
   char         *name;
   ansi_entry_t *ansi_entry;
} tui_theme_data_t;

extern bool cfg_tui_colors;

// Convert IRC color/style controls and TUI style tags to ANSI -- MUST BE FREED!
extern char *tui_colorize_string(const char *input);

// Render a string with escaped ${variables} and IRC controls
extern char *tui_render_string(dict *data, const char *title, const char *fmt, ...);

// Convert IRC color/style controls to terminal ANSI -- MUST BE FREED!
extern char *irc_to_tui_colors(const char *in);
extern char *strip_mirc_formatting(const char *input);

#endif // !defined(__librustyaxe_tui_theme_h)
