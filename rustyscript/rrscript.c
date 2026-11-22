/* RustyScript registration parser. SPDX-License-Identifier: MIT */
#include "rrscript.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct {
   const char *src;
   size_t pos;
   unsigned line, column;
   rrscript_error_t *error;
} scanner;

static char peek(scanner *s) { return s->src[s->pos]; }
static char next(scanner *s) {
   char c = peek(s);
   if (c) {
      s->pos++;
      if (c == '\n') { s->line++; s->column = 1; }
      else s->column++;
   }
   return c;
}
static int fail(scanner *s, const char *reason) {
   if (s->error) {
      s->error->line = s->line;
      s->error->column = s->column;
      snprintf(s->error->message, sizeof(s->error->message), "%s", reason);
   }
   return -1;
}
static int skip_space(scanner *s) {
   for (;;) {
      while (isspace((unsigned char)peek(s))) next(s);
      if (peek(s) == '/' && s->src[s->pos+1] == '/') {
         while (peek(s) && peek(s) != '\n') next(s);
      } else if (peek(s) == '/' && s->src[s->pos+1] == '*') {
         next(s); next(s);
         while (peek(s) && !(peek(s) == '*' && s->src[s->pos+1] == '/')) next(s);
         if (!peek(s)) return fail(s, "unterminated comment");
         next(s); next(s);
      } else break;
   }
   return 0;
}
static int word(scanner *s, const char *value) {
   size_t n = strlen(value);
   if (strncmp(s->src+s->pos, value, n)) return 0;
   char after = s->src[s->pos+n];
   if (isalnum((unsigned char)after) || after == '_') return 0;
   for (size_t i=0; i<n; ++i) next(s);
   return 1;
}
static int expect(scanner *s, char c) {
   if (skip_space(s)) return -1;
   if (peek(s) != c) return fail(s, "unexpected token");
   next(s);
   return 0;
}
static int identifier(scanner *s) {
   if (!(isalpha((unsigned char)peek(s)) || peek(s) == '_'))
      return fail(s, "expected identifier");
   do { next(s); } while (isalnum((unsigned char)peek(s)) || peek(s) == '_');
   return 0;
}
static int quoted(scanner *s, const char **start, size_t *len) {
   if (peek(s) != '"') return fail(s, "expected double-quoted command");
   next(s);
   size_t p = s->pos;
   while (peek(s) && peek(s) != '"') {
      if (peek(s) == '\\') {
         next(s);
         if (!peek(s)) return fail(s, "unterminated string escape");
      }
      next(s);
   }
   if (!peek(s)) return fail(s, "unterminated string");
   *start = s->src + p;
   *len = s->pos - p;
   next(s);
   return 0;
}
/* Scan a mandatory block, ignoring braces inside strings and comments.
 * Count nested braces; preserve the original, unmodified source span. */
static int block(scanner *s, const char **start, size_t *len) {
   if (expect(s, '{')) return -1;
   size_t p = s->pos;
   unsigned depth = 1;
   char quoted_char = 0;
   while (peek(s)) {
      char c = next(s);
      if (quoted_char) {
         if (c == '\\' && peek(s)) next(s);
         else if (c == quoted_char) quoted_char = 0;
         continue;
      }
      if (c == '"' || c == '\'') { quoted_char = c; continue; }
      if (c == '/' && peek(s) == '/') {
         while (peek(s) && peek(s) != '\n') next(s);
         continue;
      }
      if (c == '/' && peek(s) == '*') {
         next(s);
         while (peek(s) && !(peek(s) == '*' && s->src[s->pos+1] == '/')) next(s);
         if (!peek(s)) return fail(s, "unterminated block comment");
         next(s); next(s);
         continue;
      }
      if (c == '{') depth++;
      if (c == '}' && --depth == 0) {
         *start = s->src + p;
         *len = (s->pos-1)-p;
         return 0;
      }
   }
   return fail(s, "unterminated block");
}
static int name_hotkey(scanner *s, const char **start, size_t *len) {
   size_t p = s->pos;
   unsigned modifiers = 0;
   while (peek(s) == '_' || peek(s) == '&' || peek(s) == '^') {
      unsigned bit = peek(s) == '_' ? 1U : peek(s) == '&' ? 2U : 4U;
      if (modifiers & bit) return fail(s, "duplicate hotkey modifier");
      modifiers |= bit;
      next(s);
   }
   if (!(isalnum((unsigned char)peek(s)) || peek(s) == '_'))
      return fail(s, "expected hotkey name");
   do { next(s); } while (isalnum((unsigned char)peek(s)) || peek(s) == '_');
   *start = s->src+p;
   *len = s->pos-p;
   return 0;
}
int rrscript_parse_registrations(const char *source, rrscript_register_fn cb,
                                void *user, rrscript_error_t *error) {
   if (error) memset(error, 0, sizeof(*error));
   if (!source) {
      if (error) snprintf(error->message,sizeof(error->message),"null source");
      return -1;
   }
   scanner s = {.src=source, .line=1, .column=1, .error=error};
   while (1) {
      if (skip_space(&s)) return -1;
      if (!peek(&s)) return 0;
      rrscript_binding_t b = {.line=s.line};
      if (word(&s, "script")) {
         b.kind = RRSCRIPT_COMMAND;
         if (expect(&s,'(') || skip_space(&s)) return -1;
         if (quoted(&s,&b.name,&b.name_length)) return -1;
         if (!b.name_length || b.name[0] != '/')
            return fail(&s,"script command must begin with /");
         if (expect(&s,',') || block(&s,&b.body,&b.body_length) ||
             expect(&s,')')) return -1;
      } else if (word(&s, "hotkey")) {
         b.kind = RRSCRIPT_HOTKEY;
         if (skip_space(&s) || name_hotkey(&s,&b.name,&b.name_length) ||
             block(&s,&b.body,&b.body_length)) return -1;
      } else if (word(&s, "event")) {
         b.kind = RRSCRIPT_EVENT;
         if (skip_space(&s)) return -1;
         size_t p = s.pos;
         if (identifier(&s)) return -1;
         while (peek(&s) == '.') {
            next(&s);
            if (identifier(&s)) return -1;
         }
         b.name = s.src+p;
         b.name_length = s.pos-p;
         if (block(&s,&b.body,&b.body_length)) return -1;
      } else return fail(&s,"expected script(...), hotkey, or event registration");
      /* A registration's optional terminator may follow whitespace,
       * including a newline. Never mistake it for a new registration. */
      if (skip_space(&s)) return -1;
      if (peek(&s) == ';') next(&s);
      if (cb && cb(&b,user)) return fail(&s,"registration callback rejected binding");
   }
}
