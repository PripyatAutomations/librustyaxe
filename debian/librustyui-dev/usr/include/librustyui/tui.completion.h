//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef TUI_COMPLETION_H
#define TUI_COMPLETION_H

#include <stdbool.h>
#include <stddef.h>

#define TUI_MAX_COMPLETIONS_SHOWN 32

typedef char **(*tui_completion_provider_t)(const char *line, const char *word);

bool tui_register_completion_provider(tui_completion_provider_t fn);
bool tui_unregister_completion_provider(tui_completion_provider_t fn);

char **completion_collect(const char *line, const char *word);
void completion_free(char **matches);
// The application owns optional display metadata; replacement words stay bare.
typedef void (*tui_completion_describer_t)(const char *, const char *, char *, size_t);
void tui_set_completion_describer(tui_completion_describer_t fn);
void completion_describe(const char *line, const char *value, char *out, size_t capacity);

#endif
