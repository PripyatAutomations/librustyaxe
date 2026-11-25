//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Private terminal renderer interfaces. Part of RustyRig; MIT/GPL as built.
#ifndef RUSTYAXE_TUI_INTERNAL_H
#define RUSTYAXE_TUI_INTERNAL_H
#include <librustyaxe/tui.h>
void tui_scrollback_appended(tui_window_t *window, const char *line);
#endif
