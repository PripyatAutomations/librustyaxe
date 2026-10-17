#ifndef RUSTYAXE_CONFIG_COMMENTS_H
#define RUSTYAXE_CONFIG_COMMENTS_H

/* Strip C-style block comments without interpreting delimiters inside quotes.
 * Keep text on both sides of an inline comment; a separating space prevents
 * adjacent tokens from silently turning into one token.
 * The state is carried between physical lines, including continuations. */
static void cfg_strip_block_comments(char *line, bool *in_comment) {
   char *readp = line, *writep = line;
   char quote = '\0';
   bool escaped = false;

   while (*readp) {
      if (*in_comment) {
         if (readp[0] == '*' && readp[1] == '/') {
            *in_comment = false;
            readp += 2;
         } else {
            readp++;
         }
         continue;
      }
      if (quote) {
         char c = *readp++;
         *writep++ = c;
         if (escaped) escaped = false;
         else if (c == '\\') escaped = true;
         else if (c == quote) quote = '\0';
         continue;
      }
      if (*readp == '"' || *readp == '\'') {
         quote = *readp;
         *writep++ = *readp++;
         continue;
      }
      if (readp[0] == '/' && readp[1] == '*') {
         *in_comment = true;
         readp += 2;
         if (writep > line && writep[-1] != ' ' && writep[-1] != '\t')
            *writep++ = ' ';
         continue;
      }
      *writep++ = *readp++;
   }
   *writep = '\0';
}


#endif
