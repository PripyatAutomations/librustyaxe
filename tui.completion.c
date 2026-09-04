// tui.completion.c
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
#include <sys/ioctl.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <librrprotocol/rrprotocol.h>

// Shared input line state, owned by tui.keys.c
extern char input_buf[TUI_INPUTLEN];
extern int input_len;
extern int cursor_pos;

/*
 *  completion:
 *  - This needs to become aware of the focused window if line starts with / and no space:
 * cli_command_t.cmd if word starts with & or #, check channels joined Else, show users in
 * current channel if a channel window
 */

// ---------------------------------------------------------------
// Tab completion
//
// Programs register completion providers (one or more).  A provider
// is called with the full input line and the word being completed
// (before the cursor).  The provider owns ALL matching logic: it
// looks at the context (the whole line and cursor position) and
// decides on its own whether the word is a match or not, returning
// a NULL-terminated malloc'd array of malloc'd replacement words,
// or NULL if it has nothing to offer.  The library does no matching
// of its own - it merely merges provider results, completes the
// common prefix and lists candidates.  The TUI library owns the
// returned memory. 
// ---------------------------------------------------------------

#define TUI_MAX_COMPLETION_PROVIDERS 8

//typedef char **(*tui_completion_provider_t)(const char *line, const char *word);

static tui_completion_provider_t completion_providers[TUI_MAX_COMPLETION_PROVIDERS];
static int completion_provider_count = 0;

bool tui_register_completion_provider(tui_completion_provider_t fn) {
   if (!fn || completion_provider_count >= TUI_MAX_COMPLETION_PROVIDERS) {
      return false;
   }

   for (int i = 0; i < completion_provider_count; i++) {
      if (completion_providers[i] == fn) {
         return true;
      }
   }

   completion_providers[completion_provider_count++] = fn;
   return true;
}

bool tui_unregister_completion_provider(tui_completion_provider_t fn) {
   if (!fn) {
      return false;
   }

   for (int i = 0; i < completion_provider_count; i++) {
      if (completion_providers[i] == fn) {
         memmove(&completion_providers[i], &completion_providers[i + 1],
                 (completion_provider_count - i - 1) * sizeof(completion_providers[0]));
         completion_provider_count--;
         return true;
      }
   }

   return false;
}

// Collect matches from all registered providers.  Returns a
// NULL-terminated malloc'd array of malloc'd strings.
char **completion_collect(const char *line, const char *word) {
   if (!word || !*word) {
      return NULL;
   }

   char **matches = NULL;
   size_t count = 0;
   size_t len = strlen(word);

   for (int i = 0; i < completion_provider_count; i++) {
      char **sub = completion_providers[i](line, word);

      if (!sub) {
         continue;
      }

      for (int j = 0; sub[j]; j++) {
         char **tmp = realloc(matches, (count + 2) * sizeof(char *));

         if (!tmp) {
            free(sub[j]);
            continue;
         }
         matches = tmp;
         matches[count++] = sub[j];
         matches[count] = NULL;
      }
      free(sub);
   }

   return matches;
}

void completion_free(char **matches) {
   if (!matches) {
      return;
   }

   for (int i = 0; matches[i]; i++) {
      free(matches[i]);
   }
   free(matches);
}

// Called from tui.keys.c on TAB.  Operates on input_buf/cursor_pos/input_len.
// Returns true if the input line changed.
bool tui_do_completion(tui_window_t *win) {
   if (cursor_pos == 0) {
      return false;
   }

   // Find the start of the word before the cursor
   int start = cursor_pos;

   while (start > 0 && input_buf[start - 1] != ' ') {
      start--;
   }

   int word_len = cursor_pos - start;

   if (word_len <= 0) {
      return false;
   }

   char word[TUI_INPUTLEN];

   memcpy(word, &input_buf[start], word_len);
   word[word_len] = '\0';

   char **matches = completion_collect(input_buf, word);

   if (!matches || !matches[0]) {
      completion_free(matches);
      return false;
   }

   // Compute the longest common prefix of all matches
   size_t plen = strlen(matches[0]);
   int nmatch = 0;

   while (matches[nmatch]) {
      nmatch++;
   }

   for (int i = 1; i < nmatch; i++) {
      const char *m = matches[i];
      size_t j = 0;

      while (j < plen && m[j] && matches[0][j] == m[j]) {
         j++;
      }
      plen = j;
   }

   // A single match (or an unambiguous prefix): replace the word in place
   if (nmatch == 1 || plen > (size_t)word_len) {
      size_t pl = plen;

      if (nmatch == 1 && pl < sizeof(word) - 1) {
         word[pl++] = ' ';    // Add a space after a completed word
      }

      if (start + pl < TUI_INPUTLEN) {
         memmove(&input_buf[start + pl], &input_buf[cursor_pos], input_len - cursor_pos + 1);
         memcpy(&input_buf[start], matches[0], nmatch == 1 ? pl - 1 : pl);

         if (nmatch == 1) {
            input_buf[start + pl - 1] = ' ';
         }
         input_len += (int)pl - word_len;
         cursor_pos = start + pl;
         completion_free(matches);
         return true;
      }
      completion_free(matches);
      return false;
   }

   // Ambiguous: complete the common prefix and list the candidates
   for (int i = 0; i < nmatch && i < TUI_MAX_COMPLETIONS_SHOWN; i++) {
      tui_print(win, "  %s", matches[i]);
   }

   if (nmatch > TUI_MAX_COMPLETIONS_SHOWN) {
      tui_print(win, "  ... and %d more", nmatch - TUI_MAX_COMPLETIONS_SHOWN);
   }
   completion_free(matches);
   return false;
}
