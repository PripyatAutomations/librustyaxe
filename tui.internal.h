// Private terminal renderer interfaces. Part of RustyRig; MIT/GPL as built.
#ifndef RUSTYAXE_TUI_INTERNAL_H
#define RUSTYAXE_TUI_INTERNAL_H
#include <librustyaxe/tui.h>
void tui_scrollback_appended(tui_window_t *window, const char *line);
#endif
