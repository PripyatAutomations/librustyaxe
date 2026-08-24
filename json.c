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

static const char *json_parse_value(const char *s, const char *path, dict *d);

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
   while ( *s && isspace( (unsigned char)*s ) ) {
      s++;
   }
   return s;
}


// parse JSON string into a C string
static const char *json_parse_str(const char *s, char **out) {
   if (*s != '"') {
      return NULL;
   }
   s++;
   const char *start = s;
   size_t len = 0;

   while (*s && *s != '"') {
      if (*s == '\\') {
         s++;   // skip escaped char
      }
      s++; len++;
   }

   if (*s != '"') {
      return NULL;
   }
   *out = malloc(len + 1);

   if (!*out) {
      return NULL;
   }
   size_t j = 0;

   for (const char *p = start ; p < s ; p++, j++) {
      if (*p == '\\') {
         p++;
      }
      (*out)[j] = *p;
   }

   (*out)[j] = '\0';

   return s + 1;
}

// parse primitive (number, true, false, null)
static const char *json_parse_primitive(const char *s, char **out) {
   const char *start = s;
   while ( *s && !strchr(",]} \t\r\n", *s) ) {
      s++;
   }
   size_t len = s - start;
   *out = malloc(len + 1);

   if (!*out) {
      return NULL;
   }
   memcpy(*out, start, len);
   (*out)[len] = '\0';

   return s;
}

// parse JSON object
static const char *json_parse_obj(const char *s, const char *path, dict *d) {
   if (*s != '{') {
      return NULL;
   }
   s = skip_ws(s + 1);

   while (*s && *s != '}') {
      s = skip_ws(s);
      char *key = NULL;
      s = json_parse_str(s, &key);

      if (!s) {
         return NULL;
      }
      s = skip_ws(s);

      if (*s++ != ':') {
         free(key);

         return NULL;
      }
      char *newpath = path_append(path, key);
      free(key);

      if (!newpath) {
         return NULL;
      }
      s = skip_ws(s);
      s = json_parse_value(s, newpath, d);
      free(newpath);

      if (!s) {
         return NULL;
      }
      s = skip_ws(s);

      if (*s == ',') {
         s++;
      }
   }
   return (*s == '}') ? s + 1 : NULL;
}

// parse JSON array
static const char *json_parse_array(const char *s, const char *path, dict *d) {
   if (*s != '[') {
      return NULL;
   }
   s = skip_ws(s + 1);
   int idx = 0;

   while (*s && *s != ']') {
      char idxbuf[32];
      snprintf(idxbuf, sizeof(idxbuf), "[%d]", idx++);

      char *newpath = path_append(path, idxbuf);

      if (!newpath) {
         return NULL;
      }
      s = skip_ws(s);
      s = json_parse_value(s, newpath, d);
      free(newpath);

      if (!s) {
         return NULL;
      }
      s = skip_ws(s);

      if (*s == ',') {
         s++;
      }
   }
   return (*s == ']') ? s + 1 : NULL;
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

// unescape JSON string (expects surrounding quotes, returns malloc'd buffer)
char *json_unescape(const char *s) {
   if (!s) {
      return NULL;
   }
   size_t len = strlen(s);

   if (len < 2 || s[0] != '"' || s[len - 1] != '"') {
      Log(LOG_WARN, "librustyaxe", "Invalid JSON string: %s", s);

      return NULL;
   }
   // worst case: input shrinks, so allocate len+1
   char *out = malloc(len);

   if (!out) {
      Log(LOG_DEBUG, "librustyaxe", "OOM in json_unescape");

      return NULL;
   }
   const char *p = s + 1;         // skip opening quote
   const char *end = s + len - 1;  // before closing quote
   char *q = out;

   while (p < end) {
      if (*p == '\\') {
         p++;

         if (p >= end) {
            break;
         }

         switch (*p) {
            case '\"': {
               *q++ = '\"'; break;
            }
            case '\\': {
               *q++ = '\\'; break;
            }
            case '/': {
               *q++ = '/';  break;
            }
            case 'b': {
               *q++ = '\b'; break;
            }
            case 'f': {
               *q++ = '\f'; break;
            }
            case 'n': {
               *q++ = '\n'; break;
            }
            case 'r': {
               *q++ = '\r'; break;
            }
            case 't': {
               *q++ = '\t'; break;
            }
            case 'u': {
               if (end - p < 4) {
                  // not enough chars
                  Log(LOG_WARN, "librustyaxe", "Invalid \\u escape");
                  free(out);

                  return NULL;
               }
               unsigned code = 0;

               for (int i = 0 ; i < 4 ; i++) {
                  p++;

                  if (p >= end) {
                     free(out);

                     return NULL;
                  }
                  char c = *p;
                  code <<= 4;

                  if (c >= '0' && c <= '9') {
                     code |= c - '0';
                  } else if (c >= 'a' && c <= 'f') {
                     code |= c - 'a' + 10;
                  } else if (c >= 'A' && c <= 'F') {
                     code |= c - 'A' + 10;
                  } else {
                     free(out);

                     return NULL;
                  }
               }

               if (code < 0x80) {
                  *q++ = code;
               } else if (code < 0x800) {
                  *q++ = 0xC0 | (code >> 6);
                  *q++ = 0x80 | (code & 0x3F);
               } else {
                  *q++ = 0xE0 | (code >> 12);
                  *q++ = 0x80 | ( (code >> 6) & 0x3F );
                  *q++ = 0x80 | (code & 0x3F);
               }
               break;
            }
            default: {
               *q++ = *p;  // unknown escape, just copy
               break;
            }
         }
      } else {
         *q++ = *p;
      }
      p++;
   }
   *q = '\0';

   return out;
}

static json_node *json_make_node(const char *key) {
   json_node *n = calloc(1, sizeof(*n));

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
      if (!strcmp(c->key, key)) return c;
   }

   json_node *n = json_make_node(key);
   if (!n) return NULL;

   n->next = parent->child;
   parent->child = n;
   return n;
}

/*
 * Store a value as its final JSON representation. This means the JSON tree
 * itself remains compatible with the existing json_node structure.
 */
static int json_insert(json_node *root, const char *fullkey,
                       const dict_value_t *v, val_type_t type) {
   char *tmp = strdup(fullkey);
   char buf[128];
   char *jsonval = NULL;

   if (!tmp || !v) {
      free(tmp);
      return -1;
   }

   switch (type) {
      case VAL_NULL:
         jsonval = strdup("null");
         break;

      case VAL_STR:
         jsonval = json_escape(v->s);
         break;

      case VAL_CHAR: {
         char str[2] = { v->c, '\0' };
         jsonval = json_escape(str);
         break;
      }

      case VAL_BOOL:
         jsonval = strdup(v->i ? "true" : "false");
         break;

      case VAL_INT:
         snprintf(buf, sizeof(buf), "%d", v->i);
         jsonval = strdup(buf);
         break;

      case VAL_UINT:
         snprintf(buf, sizeof(buf), "%u", v->ui);
         jsonval = strdup(buf);
         break;

      case VAL_LONG:
         snprintf(buf, sizeof(buf), "%ld", v->l);
         jsonval = strdup(buf);
         break;

      case VAL_ULONG:
         snprintf(buf, sizeof(buf), "%lu", v->ul);
         jsonval = strdup(buf);
         break;

      case VAL_LLONG:
         snprintf(buf, sizeof(buf), "%lld", v->ll);
         jsonval = strdup(buf);
         break;

      case VAL_ULLONG:
         snprintf(buf, sizeof(buf), "%llu", v->ull);
         jsonval = strdup(buf);
         break;

      case VAL_FLOAT:
      case VAL_FLOATP:
         snprintf(buf, sizeof(buf), "%.9g", (double)v->f);
         jsonval = strdup(buf);
         break;

      case VAL_DOUBLE:
      case VAL_DOUBLEP:
         snprintf(buf, sizeof(buf), "%.17g", v->d);
         jsonval = strdup(buf);
         break;

      case VAL_PTR:
         jsonval = strdup("null");
         break;

      default:
         jsonval = strdup("null");
         break;
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

   if (b->buf)
      b->buf[0] = 0;
}

static void sbuf_putc(sbuf *b, char c) {
   if (b->len + 2 > b->cap) {
      b->cap *= 2;
      b->buf = realloc(b->buf, b->cap);
   }

   b->buf[b->len++] = c;
   b->buf[b->len] = 0;
}

static void sbuf_puts(sbuf *b, const char *s) {
   size_t slen;

   if (!s) return;

   slen = strlen(s);

   if (b->len + slen + 1 > b->cap) {
      while (b->len + slen + 1 > b->cap)
         b->cap *= 2;

      b->buf = realloc(b->buf, b->cap);
   }

   memcpy(b->buf + b->len, s, slen);
   b->len += slen;
   b->buf[b->len] = 0;
}

static void dump_json(json_node *n, sbuf *out) {
   if (!n || !out) return;

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

      if (c->next)
         sbuf_putc(out, ',');
   }

   sbuf_putc(out, '}');
}

static void free_json(json_node *n) {
   if (!n) return;

   for (json_node *c = n->child ; c ; ) {
      json_node *next = c->next;
      free_json(c);
      c = next;
   }

   free(n->key);
   free(n->value);
   free(n);
}

char *dict2json(dict *d) {
   const char *key;
   dict_value_t val;
   val_type_t type;
   int rank = 0;
   json_node root = { 0 };

   if (!d) return NULL;

   while ((rank = dict_enumerate_typed(d, rank, &key, &val, &type)) >= 0) {
      if (json_insert(&root, key, &val, type) != 0) {
         free_json(root.child);
         return NULL;
      }
   }

   sbuf out;
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
         case VAL_NULL: dict_add_null(d, key); break;
         case VAL_STR: dict_add(d, key, va_arg(ap, const char *)); break;
         case VAL_CHAR: dict_add_char(d, key, (char)va_arg(ap, int)); break;
         case VAL_INT: dict_add_int(d, key, va_arg(ap, int)); break;
         case VAL_UINT: dict_add_uint(d, key, va_arg(ap, unsigned int)); break;
         case VAL_LONG: dict_add_long(d, key, va_arg(ap, long)); break;
         case VAL_ULONG: dict_add_ulong(d, key, va_arg(ap, unsigned long)); break;
         case VAL_LLONG: dict_add_llong(d, key, va_arg(ap, long long)); break;
         case VAL_ULLONG: dict_add_ullong(d, key, va_arg(ap, unsigned long long)); break;
         case VAL_FLOAT: dict_add_float(d, key, (float)va_arg(ap, double)); break;
         case VAL_DOUBLE: dict_add_double(d, key, va_arg(ap, double)); break;
         case VAL_BOOL: dict_add_bool(d, key, va_arg(ap, int) != 0); break;
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
         case VAL_PTR:
            (void)va_arg(ap, void *);
            dict_add_null(d, key);
            break;
         default:
            (void)va_arg(ap, void *);
            break;
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

static const char *json_parse_value(const char *s, const char *path, dict *d) {
   s = skip_ws(s);
   if (!*s) return NULL;

   if (*s == '"') {
      char *val = NULL;
      s = json_parse_str(s, &val);
      if (!s) return NULL;
      if (dict_add(d, path, val) != 0) {
         free(val);
         return NULL;
      }
      free(val);
      return s;
   }

   if (*s == '{') return json_parse_obj(s, path, d);
   if (*s == '[') return json_parse_array(s, path, d);

   char *val = NULL;
   s = json_parse_primitive(s, &val);
   if (!s) return NULL;

   if (!strcmp(val, "null")) {
      if (dict_add_null(d, path) != 0) goto fail;
   } else if (!strcmp(val, "true")) {
      if (dict_add_bool(d, path, true) != 0) goto fail;
   } else if (!strcmp(val, "false")) {
      if (dict_add_bool(d, path, false) != 0) goto fail;
   } else if (json_number_is_integer(val)) {
      char *ep = NULL;
      errno = 0;
      long long ll = strtoll(val, &ep, 10);

      if (errno == 0 && ep != val && *ep == '\0') {
         if (ll >= INT_MIN && ll <= INT_MAX)
            dict_add_int(d, path, (int)ll);
         else if (ll >= LONG_MIN && ll <= LONG_MAX)
            dict_add_long(d, path, (long)ll);
         else
            dict_add_llong(d, path, ll);
      } else if (val[0] != '-') {
         errno = 0;
         unsigned long long ull = strtoull(val, &ep, 10);

         if (errno != 0 || ep == val || *ep != '\0') goto fail;

         if (ull <= UINT_MAX)
            dict_add_uint(d, path, (unsigned int)ull);
         else if (ull <= ULONG_MAX)
            dict_add_ulong(d, path, (unsigned long)ull);
         else
            dict_add_ullong(d, path, ull);
      }
   } else {
      char *ep = NULL;
      errno = 0;
      double v = strtod(val, &ep);

      if (errno == ERANGE || ep == val || *ep != '\0' || !isfinite(v))
         goto fail;

      dict_add_double(d, path, v);
   }

   free(val);
   return s;

fail:
   free(val);
   return NULL;
}

dict *json2dict(const char *json) {
   if (!json || *json == '\0') return NULL;

   dict *d = dict_new();
   if (!d) return NULL;

   const char *res = json_parse_value(json, "", d);
   if (!res) {
      dict_free(d);
      return NULL;
   }

   return d;
}

void json_parse_and_flatten(const char *json, dict *dptr) {
   if (!json || !dptr) return;
   json_parse_value(json, "", dptr);
}
