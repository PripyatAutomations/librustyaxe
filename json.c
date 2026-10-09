//
// librustyaxe/json.c: My ugly json handling mess.
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

#define JSON_MAX_DEPTH 64

static const char *json_parse_value(const char *s, const char *path, dict *d, unsigned depth);

// helper: append to path dynamically
static char *path_append(const char *base, const char *suffix) {
   if (!base || !suffix) {
      return NULL;
   }

   size_t len = strlen(base) + strlen(suffix) + 2;  // +1 for dot or brackets,
                                                    // +1
                                                    // for \0
   char *newpath = malloc(len);

   if (!newpath) {
      return NULL;
   }

   if (suffix[0] == '[') {
      // array index
      snprintf(newpath, len, "%s%s", base, suffix);
   } else if (base[0] != '\0') {
      // normal object key
      snprintf(newpath, len, "%s.%s", base, suffix);
   } else {
      // root key
      snprintf(newpath, len, "%s", suffix);
   }

   return newpath;
}

/////////////////////////////////
// helper: skip whitespace
static const char *skip_ws(const char *s) {
   while (*s && strchr(" \t\r\n", *s)) {
      s++;
   }
   return s;
}


// Validate the complete quoted token before decoding it. Never skip past NUL.
static const char *json_parse_str(const char *s, char **out) {
   if (*s != '"') {
      return NULL;
   }
   const char *start = s++;
   while (*s && *s != '"') {
      if ((unsigned char)*s < 0x20) {
         return NULL;
      }
      if (*s == '\\' && !*++s) {
         return NULL;
      }
      s++;
   }
   if (*s != '"') {
      return NULL;
   }
   char *token = strndup(start, (size_t)(s + 1 - start));
   if (!token) {
      return NULL;
   }
   *out = json_unescape(token);
   free(token);
   return *out ? s + 1 : NULL;
}

static const char *json_parse_primitive(const char *s, char **out) {
   const char *start = s;
   while (*s && !strchr(",]} \t\r\n", *s)) {
      s++;
   }
   if (s == start) {
      return NULL;
   }
   *out = strndup(start, (size_t)(s - start));
   return *out ? s : NULL;
}

static const char *json_parse_obj(const char *s, const char *path, dict *d, unsigned depth) {
   s = skip_ws(s + 1);
   if (*s == '}') {
      return s + 1;
   }

   dict *keys = dict_new();
   if (!keys) {
      return NULL;
   }
   
   while (*s) {
      char *key = NULL;

      s = json_parse_str(s, &key);
      if (!s) {
         goto fail;
      }

      if (dict_get_type(keys, key) != VAL_END || dict_add_bool(keys, key, true)) {
         free(key); goto fail;
      }

      s = skip_ws(s);
      if (*s != ':') {
         free(key);
         goto fail;
      }

      char *newpath = path_append(path, key);
      free(key);

      if (!newpath) {
         goto fail;
      }

      s = json_parse_value(s + 1, newpath, d, depth + 1);
      free(newpath);
      if (!s) {
         goto fail;
      }

      s = skip_ws(s);
      if (*s == '}') {
         dict_free(keys);
         return s + 1;
      }

      if (*s != ',') {
         goto fail;
      }

      s = skip_ws(s + 1);
      if (*s == '}') {
         goto fail;
      }
   }
fail:
   dict_free(keys);
   return NULL;
}

static const char *json_parse_array(const char *s, const char *path, dict *d, unsigned depth) {
   s = skip_ws(s + 1);
   if (*s == ']') {
      return s + 1;
   }
   unsigned idx = 0;
   while (*s) {
      char idxbuf[32];
      snprintf(idxbuf, sizeof(idxbuf), "[%u]", idx++);
      char *newpath = path_append(path, idxbuf);
      if (!newpath) {
         return NULL;
      }
      s = json_parse_value(s, newpath, d, depth + 1);
      free(newpath);
      if (!s) {
         return NULL;
      }
      s = skip_ws(s);
      if (*s == ']') {
         return s + 1;
      }
      if (*s != ',') {
         return NULL;
      }
      s = skip_ws(s + 1);
      if (*s == ']') {
         return NULL;
      }
   }
   return NULL;
}

// escape JSON string (returns malloc'd buffer with quotes included)
char *json_escape(const char *s) {
   if (!s) {
      return strdup("\"\"");
   }
   size_t len = strlen(s);
   // worst case every char becomes \uXXXX (6 bytes) + quotes
   char *out = malloc(len * 6 + 3);

   if (!out) {
      Log(LOG_CRIT, "librustyaxe", "OOM in json_escape");

      return NULL;
   }
   char *p = out;
   *p++ = '"';

   for (size_t i = 0 ; i < len ; i++) {
      unsigned char c = (unsigned char)s[i];

      switch (c) {
         case '\"': {
            *p++ = '\\';
            *p++ = '\"';
            break;
         }
         case '\\': {
            *p++ = '\\';
            *p++ = '\\';
            break;
         }
         case '\b': {
            *p++ = '\\';
            *p++ = 'b';
            break;
         }
         case '\f': {
            *p++ = '\\';
            *p++ = 'f';
            break;
         }
         case '\n': {
            *p++ = '\\';
            *p++ = 'n';
            break;
         }
         case '\r': {
            *p++ = '\\';
            *p++ = 'r';
            break;
         }
         case '\t': {
            *p++ = '\\';
            *p++ = 't';
            break;
         }
         default: {
            if (c < 0x20) {
               p += sprintf(p, "\\u%04x", c);
            } else {
               *p++ = c;
            }
         }
      }
   }

   *p++ = '"';
   *p = '\0';

   return out;
}

// Decode a JSON string into the library's NUL-terminated UTF-8 representation.
// Embedded NUL and lone UTF-16 surrogates cannot be represented safely.
static bool json_hex4(const char *p, const char *end, unsigned *code) {
   if (end - p < 4) {
      return false;
   }
   *code = 0;
   for (unsigned i = 0; i < 4; i++) {
      unsigned char c = p[i];
      unsigned digit;
      if (c >= '0' && c <= '9') {
         digit = c - '0';
      } else if (c >= 'a' && c <= 'f') {
         digit = c - 'a' + 10;
      } else if (c >= 'A' && c <= 'F') {
         digit = c - 'A' + 10; 
      } else {
         return false;
      }
      *code = (*code << 4) | digit;
   }
   return true;
}

char *json_unescape(const char *s) {
   if (!s) {
      return NULL;
   }

   size_t len = strlen(s);
   if (len < 2 || s[0] != '"' || s[len - 1] != '"') {
      return NULL;
   }

   char *out = malloc(len);
   if (!out) {
      return NULL;
   }
   const char *p = s + 1, *end = s + len - 1;
   char *q = out;
   while (p < end) {
      unsigned char c = *p++;

      if (c < 0x20 || c == '"') {
         goto fail;
      }

      if (c != '\\') {
         *q++ = c; continue;
      }

      if (p == end) {
         goto fail;
      }

      c = *p++;
      switch (c) {
         case '"': case '\\': case '/': *q++ = c; break;
         case 'b': *q++ = '\b'; break;
         case 'f': *q++ = '\f'; break;
         case 'n': *q++ = '\n'; break;
         case 'r': *q++ = '\r'; break;
         case 't': *q++ = '\t'; break;
         case 'u': {
            unsigned code;
            if (!json_hex4(p, end, &code) || !code) {
               goto fail;
            }
            p += 4;
            if (code >= 0xD800 && code <= 0xDBFF) {
               unsigned low;
               if (end - p < 6 || p[0] != '\\' || p[1] != 'u' || !json_hex4(p + 2, end, &low) || low < 0xDC00 || low > 0xDFFF) {
                  goto fail;
               }
               p += 6;
               code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00;
            } else if (code >= 0xDC00 && code <= 0xDFFF) {
               goto fail;
            }

            if (code < 0x80) {
               *q++ = code;
            } else if (code < 0x800) {
               *q++ = 0xC0 | (code >> 6); *q++ = 0x80 | (code & 0x3F);
            } else if (code < 0x10000) {
               *q++ = 0xE0 | (code >> 12); *q++ = 0x80 | ((code >> 6) & 0x3F); *q++ = 0x80 | (code & 0x3F);
            } else {
               *q++ = 0xF0 | (code >> 18); *q++ = 0x80 | ((code >> 12) & 0x3F);
               *q++ = 0x80 | ((code >> 6) & 0x3F); *q++ = 0x80 | (code & 0x3F);
            }
            break;
         }
         default: goto fail;
      }
   }
   *q = '\0';
   return out;
fail:
   free(out);
   return NULL;
}

static json_node *json_make_node(const char *key) {
   json_node *n = calloc( 1, sizeof(*n) );

   if (!n) {
      Log(LOG_CRIT, "librustyaxe", "OOM in json_make_node");

      return NULL;
   }

   n->key = strdup(key);

   if (!n->key) {
      free(n);

      return NULL;
   }

   return n;
}

static json_node *find_child(json_node *parent, const char *key) {
   for (json_node *c = parent->child ; c ; c = c->next) {
      if ( !strcmp(c->key, key) ) {
         return c;
      }
   }

   json_node *n = json_make_node(key);

   if (!n) {
      return NULL;
   }

   n->next = parent->child;
   parent->child = n;

   return n;
}

/*
 * Store a value as its final JSON representation. This means the JSON tree itself remains
 * compatible with the existing json_node structure.
 */
static int json_insert(json_node *root, const char *fullkey, const dict_value_t *v, val_type_t type) {
   char *tmp = strdup(fullkey);
   char buf[128];
   char *jsonval = NULL;

   if (!tmp || !v) {
      free(tmp);

      return -1;
   }

   switch (type) {
      case VAL_NULL: {
         jsonval = strdup("null");
         break;
      }

      case VAL_STR: {
         jsonval = json_escape(v->s);
         break;
      }

      case VAL_CHAR: {
         char str[2] = {
            v->c, '\0'
         };
         jsonval = json_escape(str);
         break;
      }

      case VAL_BOOL: {
         jsonval = strdup(v->i ? "true" : "false");
         break;
      }

      case VAL_INT: {
         snprintf(buf, sizeof(buf), "%d", v->i);
         jsonval = strdup(buf);
         break;
      }

      case VAL_UINT: {
         snprintf(buf, sizeof(buf), "%u", v->ui);
         jsonval = strdup(buf);
         break;
      }

      case VAL_LONG: {
         snprintf(buf, sizeof(buf), "%ld", v->l);
         jsonval = strdup(buf);
         break;
      }

      case VAL_ULONG: {
         snprintf(buf, sizeof(buf), "%lu", v->ul);
         jsonval = strdup(buf);
         break;
      }

      case VAL_LLONG: {
         snprintf(buf, sizeof(buf), "%lld", v->ll);
         jsonval = strdup(buf);
         break;
      }

      case VAL_ULLONG: {
         snprintf(buf, sizeof(buf), "%llu", v->ull);
         jsonval = strdup(buf);
         break;
      }

      case VAL_FLOAT:
      case VAL_FLOATP: {
         snprintf(buf, sizeof(buf), "%.9g", (double)v->f);
         jsonval = strdup(buf);
         break;
      }

      case VAL_DOUBLE:
      case VAL_DOUBLEP: {
         snprintf(buf, sizeof(buf), "%.17g", v->d);
         jsonval = strdup(buf);
         break;
      }

      case VAL_PTR: {
         jsonval = strdup("null");
         break;
      }

      default: {
         jsonval = strdup("null");
         break;
      }
   }

   if (!jsonval) {
      free(tmp);

      return -1;
   }

   char *tok = strtok(tmp, ".");
   json_node *cur = root;

   while (tok) {
      cur = find_child(cur, tok);

      if (!cur) {
         free(jsonval);
         free(tmp);

         return -1;
      }
      tok = strtok(NULL, ".");
   }
   free(cur->value);
   cur->value = jsonval;

   free(tmp);

   return 0;
}

// ---- string builder ----
typedef struct {
   char *buf;
   size_t len, cap;
} sbuf;

static void sbuf_init(sbuf *b) {
   b->cap = 256;
   b->len = 0;
   b->buf = malloc(b->cap);

   if (b->buf) {
      b->buf[0] = 0;
   }
}

static bool sbuf_putc(sbuf *b, char c) {
   if (b->len + 2 > b->cap) {
      b->cap *= 2;

      char *tmp = realloc(b->buf, b->cap);

      if (!tmp) {
         return true;
      }
      b->buf = tmp;
   }

   b->buf[b->len++] = c;
   b->buf[b->len] = 0;

   return false;
}

static bool sbuf_puts(sbuf *b, const char *s) {
   size_t slen;

   if (!s) {
      return true;
   }

   slen = strlen(s);

   if (b->len + slen + 1 > b->cap) {
      while (b->len + slen + 1 > b->cap) {
         b->cap *= 2;
      }
      char *tmp = realloc(b->buf, b->cap);

      if (!tmp) {
         return true;
      }
      b->buf = tmp;
   }

   memcpy(b->buf + b->len, s, slen);
   b->len += slen;
   b->buf[b->len] = 0;

   return false;
}

static void dump_json(json_node *n, sbuf *out) {
   if (!n || !out) {
      return;
   }

   sbuf_putc(out, '{');

   for (json_node *c = n->child ; c ; c = c->next) {
      char *key = json_escape(c->key);

      if (key) {
         sbuf_puts(out, key);
         free(key);
      }

      sbuf_putc(out, ':');

      if (c->value && !c->child) {
         /* json_insert() already produced either a quoted string or primitive. */
         sbuf_puts(out, c->value);
      } else {
         dump_json(c, out);
      }

      if (c->next) {
         sbuf_putc(out, ',');
      }
   }

   sbuf_putc(out, '}');
}

static void free_json(json_node *n) {
   while (n) {
      json_node *next = n->next;

      free_json(n->child);
      free(n->key);
      free(n->value);
      free(n);

      n = next;
   }
}

char *dict2json(dict *d) {
   const char *key;
   dict_value_t val;
   val_type_t type;
   int rank = 0;
   json_node root = {
      0
   };
   sbuf out = {
      0
   };

   if (!d) {
      return NULL;
   }

   while ( ( rank = dict_enumerate_typed(d, rank, &key, &val, &type) ) >= 0 ) {
      if (json_insert(&root, key, &val, type) != 0) {
         free_json(root.child);

         return NULL;
      }
   }
   sbuf_init(&out);

   if (!out.buf) {
      free_json(root.child);

      return NULL;
   }

   dump_json(&root, &out);

   free_json(root.child);

   return out.buf;
}

void dict_import_va(dict *d, int first_type, va_list ap) {
   int type = first_type;

   while (type != VAL_END) {
      const char *key = va_arg(ap, const char *);

      switch (type) {
         case VAL_NULL: {
            dict_add_null(d, key); break;
         }
         case VAL_STR: {
            dict_add( d, key, va_arg(ap, const char *) ); break;
         }
         case VAL_CHAR: {
            dict_add_char( d, key, (char)va_arg(ap, int) ); break;
         }
         case VAL_INT: {
            dict_add_int( d, key, va_arg(ap, int) ); break;
         }
         case VAL_UINT: {
            dict_add_uint( d, key, va_arg(ap, unsigned int) ); break;
         }
         case VAL_LONG: {
            dict_add_long( d, key, va_arg(ap, long) ); break;
         }
         case VAL_ULONG: {
            dict_add_ulong( d, key, va_arg(ap, unsigned long) ); break;
         }
         case VAL_LLONG: {
            dict_add_llong( d, key, va_arg(ap, long long) ); break;
         }
         case VAL_ULLONG: {
            dict_add_ullong( d, key, va_arg(ap, unsigned long long) ); break;
         }
         case VAL_FLOAT: {
            dict_add_float( d, key, (float)va_arg(ap, double) ); break;
         }
         case VAL_DOUBLE: {
            dict_add_double( d, key, va_arg(ap, double) ); break;
         }
         case VAL_BOOL: {
            dict_add_bool(d, key, va_arg(ap, int) != 0); break;
         }
         case VAL_FLOATP: {
            double v = va_arg(ap, double);
            (void)va_arg(ap, int);
            dict_add_float(d, key, (float)v);
            break;
         }
         case VAL_DOUBLEP: {
            double v = va_arg(ap, double);
            (void)va_arg(ap, int);
            dict_add_double(d, key, v);
            break;
         }
         case VAL_PTR: {
            (void)va_arg(ap, void *);
            dict_add_null(d, key);
            break;
         }
         default: {
            (void)va_arg(ap, void *);
            break;
         }
      }

      type = va_arg(ap, int);
   }
}

void dict_import_real(dict *d, int first_type, ...) {
   va_list ap;
   va_start(ap, first_type);
   dict_import_va(d, first_type, ap);
   va_end(ap);
}

// Higher-level: build a dict from varargs, turn it into a json string, free the
// dict and return string
// You *must* free the string when done
const char *dict2json_mkstr_real(int first_type, ...) {
   dict *d = dict_new();

   va_list ap;
   va_start(ap, first_type);
   dict_import_va(d, first_type, ap);
   va_end(ap);

   char *jp = dict2json(d);
   dict_free(d);

   // You must free jp when done with it
   return jp;
}

// parse JSON value (object, array, string, primitive)
static bool json_number_is_integer(const char *s) {
   return !strpbrk(s, ".eE");
}

static const char *json_parse_value(const char *s, const char *path, dict *d, unsigned depth) {
   s = skip_ws(s);

   if (!*s || depth > JSON_MAX_DEPTH) {
      return NULL;
   }

   if (dict_get_type(d, path) != VAL_END) {
      return NULL;
   }

   if (*s == '"') {
      char *val = NULL;
      s = json_parse_str(s, &val);

      if (!s) {
         return NULL;
      }

      if (dict_add(d, path, val) != 0) {
         free(val);

         return NULL;
      }
      free(val);

      return s;
   }

   if (*s == '{') {
      return json_parse_obj(s, path, d, depth);
   }

   if (*s == '[') {
      return json_parse_array(s, path, d, depth);
   }

   char *val = NULL;
   s = json_parse_primitive(s, &val);

   if (!s) {
      return NULL;
   }

   if ( !strcmp(val, "null") ) {
      if (dict_add_null(d, path) != 0) {
         goto fail;
      }
   } else if ( !strcmp(val, "true") ) {
      if (dict_add_bool(d, path, true) != 0) {
         goto fail;
      }
   } else if ( !strcmp(val, "false") ) {
      if (dict_add_bool(d, path, false) != 0) {
         goto fail;
      }
   } else {
      const char *number = val;
      if (*number == '-') {
         number++;
      }
      if (*number == '0') {
         number++;
      }
      else if (*number >= '1' && *number <= '9') {
         while (isdigit((unsigned char)*number)) {
            number++;
         }
      } else {
         goto fail;
      }

      if (*number == '.') {
         number++;
         if (!isdigit((unsigned char)*number)) {
            goto fail;
         }

         while (isdigit((unsigned char)*number)) {
            number++;
         }
      }

      if (*number == 'e' || *number == 'E') {
         number++;
         if (*number == '+' || *number == '-') {
            number++;
         }

         if (!isdigit((unsigned char)*number)) {
            goto fail;
         }

         while (isdigit((unsigned char)*number)) {
            number++;
         }
      }

      if (*number) {
         goto fail;
      }

      if (json_number_is_integer(val)) {
         char *ep = NULL;
         errno = 0;
         long long ll = strtoll(val, &ep, 10);

         if (errno == 0 && ep != val && *ep == '\0') {
            if (ll >= INT_MIN && ll <= INT_MAX) {
               if (dict_add_int(d, path, (int)ll)) { goto fail; }
            } else if (ll >= LONG_MIN && ll <= LONG_MAX) {
               if (dict_add_long(d, path, (long)ll)) { goto fail; }
            } else {
               if (dict_add_llong(d, path, ll)) { goto fail; }
            }
         } else if (val[0] != '-') {
            errno = 0;
            unsigned long long ull = strtoull(val, &ep, 10);

            if (errno != 0 || ep == val || *ep != '\0') {
               goto fail;
            }

            if (ull <= UINT_MAX) {
               if (dict_add_uint(d, path, (unsigned int)ull)) {
                  goto fail;
               }
            } else if (ull <= ULONG_MAX) {
               if (dict_add_ulong(d, path, (unsigned long)ull)) {
                  goto fail;
               }
            } else {
               if (dict_add_ullong(d, path, ull)) {
                  goto fail;
               }
            }
         } else {
            goto fail;
         }
      } else {
         char *ep = NULL;
         errno = 0;
         double v = strtod(val, &ep);

         if ( errno == ERANGE || ep == val || *ep != '\0' || !isfinite(v) ) {
            goto fail;
         }

         if (dict_add_double(d, path, v)) {
            goto fail;
         }
      }
   }
   free(val);

   return s;

fail:
   free(val);

   return NULL;
}

dict *json2dict(const char *json) {
   if (!json || *json == '\0') {
      return NULL;
   }

   dict *d = dict_new();

   if (!d) {
      return NULL;
   }

   const char *res = json_parse_value(json, "", d, 0);

   /* A websocket message must contain exactly one JSON value.  Previously trailing bytes
    * were silently ignored, which made truncated or concatenated frames look like valid
    * dictionaries and sent the failure much later through the event bus. */
   if (!res || *skip_ws(res) != '\0') {
      dict_free(d);
      return NULL;
   }

   return d;
}

void json_parse_and_flatten(const char *json, dict *dptr) {
   if (!json || !dptr) {
      return;
   }
   json_parse_value(json, "", dptr, 0);
}
