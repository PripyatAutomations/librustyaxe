//
// dict.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
//   @file    dict.c
//   @author  N. Devillard
//   @date    Apr 2011
//   @brief   Dictionary object
//   @note    Heavily modified by rustyaxe; added support for multiple types,
// conversions, etc
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/core.h>

/** Minimum dictionary size to start with */
#define	DICT_MIN_SZ 8

/* Dummy pointer to reference deleted keys */
#define	DUMMY_PTR ( (void*)-1 )

/* Used to hash further when handling collisions */
#define	PERTURB_SHIFT 5

/* Beyond this size, a dictionary will not be grown by the same factor */
#define	DICT_BIGSZ 64000

/* Define this to:  0 for no debug, 1 for moderate debugg, 2 for heavy debug */
#define	DICT_DEBUG 0

/*
 * Specify which hash function to use MurmurHash is fast but may not work on all
 * architectures Dobbs is a tad bit slower but not by much and works everywhere
 */
#define	dict_hash dict_hash_murmur
/* #define dict_hash   dict_hash_dobbs */

/* Forward definitions */
static int dict_resize(dict *d);

/**
 *  This hash function has been taken from an Article in Dr Dobbs Journal. There are
 * probably better ones out there but this one does the job.
 */
static unsigned dict_hash_dobbs(const char *key) {
   int len;
   unsigned hash;
   int i;

   len = strlen(key);

   for (hash = 0, i = 0 ; i < len ; i++) {
      hash += (unsigned)key[i];
      hash += (hash << 10);
      hash ^= (hash >> 6);
   }

   hash += (hash << 3);
   hash ^= (hash >> 11);
   hash += (hash << 15);

   return hash;
}

/* Murmurhash */
static unsigned dict_hash_murmur(const char *key) {
   int len;
   unsigned h, k, seed;
   unsigned m = 0x5bd1e995;
   int r = 24;
   unsigned char *data;

   seed = 0x0badcafe;
   len = (int)strlen(key);

   h = seed ^ len;
   data = (unsigned char *)key;
   while (len >= 4) {
      k = *(unsigned int *)data;

      k *= m;
      k ^= k >> r;
      k *= m;

      h *= m;
      h ^= k;

      data += 4;
      len -= 4;
   }

   switch (len) {
      case 3: {
         h ^= data[2] << 16;
      }
      case 2: {
         h ^= data[1] << 8;
      }
      case 1: {
         h ^= data[0];
         h *= m;
      }
   }

   h ^= h >> 13;
   h *= m;
   h ^= h >> 15;

   return h;
}

/** Lookup an element in a dict This implementation copied almost verbatim from the Python
 * dictionary object, without the Pythonisms.
 */
static keypair *dict_lookup(dict *d, const char *key, unsigned hash) {
   keypair *freeslot;
   keypair *ep;
   unsigned i;
   unsigned perturb;

   if (!d || !key) {
      return NULL;
   }
   i = hash & (d->size - 1);
   /* Look for empty slot */
   ep = d->table + i;

   if (ep->key == NULL || ep->key == key) {
      return ep;
   }

   if (ep->key == DUMMY_PTR) {
      freeslot = ep;
   } else {
      if ( ep->hash == hash && !strcmp(key, ep->key) ) {
         return ep;
      }
      freeslot = NULL;
   }

   for (perturb = hash ; ; perturb >>= PERTURB_SHIFT) {
      i = (i << 2) + i + perturb + 1;
      i &= (d->size - 1);
      ep = d->table + i;

      if (ep->key == NULL) {
         return freeslot == NULL ? ep : freeslot;
      }

      if ( (ep->key == key) || ( ep->hash == hash && ep->key != DUMMY_PTR && !strcmp(ep->key, key) ) ) {
         return ep;
      }

      if (ep->key == DUMMY_PTR && freeslot == NULL) {
         freeslot = ep;
      }
   }

   return NULL;
}

/* Free any dynamically allocated value held by a keypair. */
static void dict_free_value(keypair *kp) {
   if (!kp) {
      return;
   }

   if (kp->val_type == VAL_STR && kp->val.s) {
      free((char *)kp->val.s);
   }

   kp->val.p = NULL;
   kp->val_type = VAL_NULL;
}

/* Store a complete key/value pair. String values are duplicated. */
static int dict_store(dict *d, const char *key, val_type_t type,
                      const dict_value_t *val) {
   unsigned hash;
   keypair *slot;
   char *newkey = NULL;
   const char *newstr = NULL;

   if (!d || !key || !val) {
      return -1;
   }

   hash = dict_hash(key);
   slot = dict_lookup(d, key, hash);

   if (!slot) {
      return -1;
   }

   /*
    * Prepare all allocations before modifying the dictionary.
    */
   newkey = strdup(key);
   if (!newkey) {
      return -1;
   }

   if (type == VAL_STR && val->s) {
      newstr = strdup(val->s);
      if (!newstr) {
         free(newkey);
         return -1;
      }
   }

   /*
    * If this is a new entry, make sure there is room before committing it.
    * Replacements don't increase the number of used slots.
    */
   if (!slot->key || slot->key == DUMMY_PTR) {
      if ((3 * (d->fill + 1)) >= (d->size * 2)) {
         if (dict_resize(d) != 0) {
            free( (void *)newstr);
            free( (void *)newkey);
            return -1;
         }

         /*
          * Resize may have moved the slot.
          */
         slot = dict_lookup(d, key, hash);
         if (!slot) {
            free( (void *)newstr);
            free(newkey);
            return -1;
         }
      }
   }

   /*
    * Replace an existing entry.
    */
   if (slot->key && slot->key != DUMMY_PTR) {
      free((char *)slot->key);
      dict_free_value(slot);
      d->used--;
   } else if (slot->key == DUMMY_PTR) {
      d->fill--;
   }

   slot->key = newkey;
   slot->val_type = type;
   slot->hash = hash;

   if (type == VAL_STR) {
      slot->val.s = newstr;
   } else {
      slot->val = *val;
   }

   d->used++;
   d->fill++;

   return 0;
}

/* Add an item to a dictionary without copying key/val. Used by resize only. */
static int dict_add_p(dict *d, const keypair *src) {
   unsigned hash;
   keypair *slot;

   if (!d || !src || !src->key || src->key == DUMMY_PTR) {
      return -1;
   }

   hash = dict_hash(src->key);
   slot = dict_lookup(d, src->key, hash);

   if (!slot) {
      return -1;
   }

   slot->key = src->key;
   slot->val = src->val;
   slot->val_type = src->val_type;
   slot->hash = hash;
   d->used++;
   d->fill++;

   return 0;
}

/** Add an item to a dictionary by copying key/val into the dict. */
int dict_add(dict *d, const char *key, const char *val) {
   dict_value_t v = {
      .s = val
   };
   return dict_store(d, key, VAL_STR, &v);
}

int dict_add_null(dict *d, const char *key) {
   dict_value_t v = { 0 };
   return dict_store(d, key, VAL_NULL, &v);
}

int dict_add_char(dict *d, const char *key, char val) {
   dict_value_t v = { .c = val };
   return dict_store(d, key, VAL_CHAR, &v);
}

int dict_add_bool(dict *d, const char *key, bool val) {
   dict_value_t v = { .i = val ? 1 : 0 };
   return dict_store(d, key, VAL_BOOL, &v);
}

int dict_add_int(dict *d, const char *key, int val) {
   dict_value_t v = { .i = val };
   return dict_store(d, key, VAL_INT, &v);
}

int dict_add_uint(dict *d, const char *key, unsigned int val) {
   dict_value_t v = { .ui = val };
   return dict_store(d, key, VAL_UINT, &v);
}

int dict_add_long(dict *d, const char *key, long val) {
   dict_value_t v = { .l = val };
   return dict_store(d, key, VAL_LONG, &v);
}

int dict_add_ulong(dict *d, const char *key, unsigned long val) {
   dict_value_t v = { .ul = val };
   return dict_store(d, key, VAL_ULONG, &v);
}

int dict_add_llong(dict *d, const char *key, long long val) {
   dict_value_t v = { .ll = val };
   return dict_store(d, key, VAL_LLONG, &v);
}

int dict_add_ullong(dict *d, const char *key, unsigned long long val) {
   dict_value_t v = { .ull = val };
   return dict_store(d, key, VAL_ULLONG, &v);
}

int dict_add_float(dict *d, const char *key, float val) {
   dict_value_t v = { .f = val };
   return dict_store(d, key, VAL_FLOAT, &v);
}

int dict_add_double(dict *d, const char *key, double val) {
   dict_value_t v = { .d = val };
   return dict_store(d, key, VAL_DOUBLE, &v);
}

/** Resize a dictionary */
static int dict_resize(dict *d) {
   unsigned newsize = d->size;
   unsigned factor = (d->size > DICT_BIGSZ) ? 2 : 4;

   while (newsize <= factor * d->used)
      newsize *= 2;

   if (newsize == d->size)
      return 0;

   keypair *oldtable = d->table;
   unsigned oldsize = d->size;

   keypair *newtable = calloc(newsize, sizeof(*newtable));
   if (!newtable)
      return -1;

   d->table = newtable;
   d->size = newsize;
   d->used = 0;
   d->fill = 0;

   for (unsigned i = 0; i < oldsize; i++) {
      if (oldtable[i].key && oldtable[i].key != DUMMY_PTR) {
         if (dict_add_p(d, &oldtable[i]) != 0) {
            /*
             * Restore old table. Nothing has been freed from it;
             * newtable contains only borrowed pointers.
             */
            d->table = oldtable;
            d->size = oldsize;

            /*
             * Recalculate used/fill from the old table.
             */
            d->used = 0;
            d->fill = 0;
            for (unsigned j = 0; j < oldsize; j++) {
               if (oldtable[j].key) {
                  d->fill++;
                  if (oldtable[j].key != DUMMY_PTR)
                     d->used++;
               }
            }

            free(newtable);
            return -1;
         }
      }
   }

   free(oldtable);
   return 0;
}

/** Public: allocate a new dict */
dict *dict_new(void) {
   dict *d = calloc(1, sizeof(*d));

   if (!d)
      return NULL;

   d->size = DICT_MIN_SZ;
   d->table = calloc(DICT_MIN_SZ, sizeof(*d->table));

   if (!d->table) {
      free(d);
      return NULL;
   }

   return d;
}

/** Public: deallocate a dict */
void dict_free(dict *d) {
   unsigned i;

   if (!d) {
      return;
   }

   for (i = 0 ; i < d->size ; i++) {
      if (d->table[i].key && d->table[i].key != DUMMY_PTR) {
         free( (char *)d->table[i].key );

         dict_free_value(&d->table[i]);
      }
   }

   free(d->table);
   free(d);

   return;
}

/** Public: get an item from a dict. Only strings are returned directly. */
const char *dict_get(dict *d, const char *key, const char *def) {
   keypair *kp;
   unsigned hash;

   if (!d || !key) {
      return def;
   }

   hash = dict_hash(key);
   kp = dict_lookup(d, key, hash);

   if (kp && kp->key && kp->key != DUMMY_PTR && kp->val_type == VAL_STR) {
      return (char *)kp->val.s;
   }

   return def;
}

val_type_t dict_get_type(dict *d, const char *key) {
   keypair *kp;
   unsigned hash;

   if (!d || !key) {
      return VAL_END;
   }

   hash = dict_hash(key);
   kp = dict_lookup(d, key, hash);

   if (!kp || !kp->key || kp->key == DUMMY_PTR) {
      return VAL_END;
   }

   return kp->val_type;
}

int dict_enumerate_typed(dict *d, int rank, const char **key,
                         dict_value_t *val, val_type_t *type) {
   if (!d || !key || !val || !type || rank < 0) {
      return -1;
   }

   while (rank < (int)d->size &&
          (d->table[rank].key == NULL || d->table[rank].key == DUMMY_PTR)) {
      rank++;
   }

   if (rank >= (int)d->size) {
      *key = NULL;
      memset(val, 0, sizeof(*val));
      *type = VAL_END;
      return -1;
   }

   *key = d->table[rank].key;
   *val = d->table[rank].val;
   *type = d->table[rank].val_type;

   return rank + 1;
}

/** Public: delete an item in a dict */
int dict_del(dict *d, const char *key) {
   unsigned hash;
   keypair *kp;

   if (!d || !key) {
      return -1;
   }

   hash = dict_hash(key);
   kp = dict_lookup(d, key, hash);

   if (!kp || !kp->key || kp->key == DUMMY_PTR) {
      return -1;
   }

   free((char *)kp->key);
   kp->key = DUMMY_PTR;
   dict_free_value(kp);
   d->used--;

   return 0;
}

/** Public: enumerate a dictionary */
int dict_enumerate(dict *d, int rank, const char **key, char **val) {
   if (!d || !key || !val || rank < 0) {
      return -1;
   }

   while (rank < (int)d->size &&
          (d->table[rank].key == NULL || d->table[rank].key == DUMMY_PTR)) {
      rank++;
   }

   if (rank >= (int)d->size) {
      *key = NULL;
      *val = NULL;
      return -1;
   }

   *key = d->table[rank].key;

   /*
    * This legacy API can only return strings. Typed callers should use
    * dict_enumerate_typed().
    */
   if (d->table[rank].val_type == VAL_STR) {
      *val = (char *)d->table[rank].val.s;
   } else {
      *val = NULL;
   }

   return rank + 1;
}

static const char *dict_type_name(val_type_t type) {
   switch (type) {
      case VAL_NULL:     return "null";
      case VAL_STR:      return "string";
      case VAL_INT:      return "int";
      case VAL_UINT:     return "uint";
      case VAL_LONG:     return "long";
      case VAL_ULONG:    return "ulong";
      case VAL_LLONG:    return "llong";
      case VAL_ULLONG:   return "ullong";
      case VAL_FLOAT:    return "float";
      case VAL_BOOL:     return "bool";
      case VAL_DOUBLE:   return "double";
      case VAL_FLOATP:   return "floatp";
      case VAL_DOUBLEP:  return "doublep";
      case VAL_CHAR:     return "char";
      case VAL_PTR:      return "ptr";
      case VAL_END:      return "end";
      default:           return "unknown";
   }
}

static void dict_dump_value(const keypair *kp, char *buf, size_t len) {
   if (!kp || !buf || !len) {
      return;
   }

   switch (kp->val_type) {
      case VAL_NULL:
         snprintf(buf, len, "null");
         break;

      case VAL_STR:
         snprintf(buf, len, "%s", kp->val.s ? kp->val.s : "NULL");
         break;

      case VAL_INT:
         snprintf(buf, len, "%d", kp->val.i);
         break;

      case VAL_UINT:
         snprintf(buf, len, "%u", kp->val.ui);
         break;

      case VAL_LONG:
         snprintf(buf, len, "%ld", kp->val.l);
         break;

      case VAL_ULONG:
         snprintf(buf, len, "%lu", kp->val.ul);
         break;

      case VAL_LLONG:
         snprintf(buf, len, "%lld", kp->val.ll);
         break;

      case VAL_ULLONG:
         snprintf(buf, len, "%llu", kp->val.ull);
         break;

      case VAL_FLOAT:
      case VAL_FLOATP:
         snprintf(buf, len, "%g", kp->val.f);
         break;

      case VAL_DOUBLE:
      case VAL_DOUBLEP:
         snprintf(buf, len, "%g", kp->val.d);
         break;

      case VAL_BOOL:
         snprintf(buf, len, "%s", kp->val.i ? "true" : "false");
         break;

      case VAL_CHAR:
         if (isprint((unsigned char)kp->val.c)) {
            snprintf(buf, len, "'%c'", kp->val.c);
         } else {
            snprintf(buf, len, "0x%02x",
                     (unsigned char)kp->val.c);
         }
         break;

      case VAL_PTR:
         snprintf(buf, len, "%p", kp->val.p);
         break;

      default:
         snprintf(buf, len, "?");
         break;
   }
}

void dict_dump(dict *d, FILE *out) {
   const char *key;
   dict_value_t val;
   val_type_t type;
   int rank = 0;
   char value[256];

   if (!d) {
      return;
   }

   while ((rank = dict_enumerate_typed(d, rank, &key, &val, &type)) >= 0) {
      keypair kp = {
         .key = key,
         .val = val,
         .val_type = type
      };

      dict_dump_value(&kp, value, sizeof(value));

      if (out) {
         fprintf(out, "%20s=%s (%s)\n",
                 key, value, dict_type_name(type));
      } else {
         Log(LOG_DEBUG, "librustyaxe",
             "%20s=%s (%s)",
             key, value, dict_type_name(type));
      }
   }
}

static bool parse_bool_string(const char *s, bool *out) {
   if (!s || !out) return false;

   if (!strcasecmp(s, "true") || !strcasecmp(s, "yes") ||
       !strcasecmp(s, "on") || !strcmp(s, "1")) {
      *out = true;
      return true;
   }

   if (!strcasecmp(s, "false") || !strcasecmp(s, "no") ||
       !strcasecmp(s, "off") || !strcmp(s, "0")) {
      *out = false;
      return true;
   }

   return false;
}

static bool parse_ll(const char *s, long long *out) {
   char *ep;
   long long v;

   if (!s || !out) return false;

   errno = 0;
   v = strtoll(s, &ep, 10);
   while (*ep && isspace((unsigned char)*ep)) ep++;

   if (errno == ERANGE || ep == s || *ep) return false;

   *out = v;
   return true;
}

static bool parse_ull(const char *s, unsigned long long *out) {
   char *ep;
   unsigned long long v;

   if (!s || !out) return false;

   errno = 0;
   v = strtoull(s, &ep, 10);
   while (*ep && isspace((unsigned char)*ep)) ep++;

   if (errno == ERANGE || ep == s || *ep) return false;

   *out = v;
   return true;
}

static bool parse_double(const char *s, double *out) {
   char *ep;
   double v;

   if (!s || !out) return false;

   errno = 0;
   v = strtod(s, &ep);
   while (*ep && isspace((unsigned char)*ep)) ep++;

   if (errno == ERANGE || ep == s || *ep || !isfinite(v)) return false;

   *out = v;
   return true;
}

static bool dict_get_kp(dict *d, const char *key, keypair **out) {
   unsigned hash;
   keypair *kp;

   if (!d || !key || !out) return false;

   hash = dict_hash(key);
   kp = dict_lookup(d, key, hash);

   if (!kp || !kp->key || kp->key == DUMMY_PTR) return false;

   *out = kp;
   return true;
}

bool dict_get_bool(dict *d, const char *key, bool def) {
   keypair *kp;

   if (!dict_get_kp(d, key, &kp)) return def;

   switch (kp->val_type) {
      case VAL_BOOL: return kp->val.i != 0;
      case VAL_INT: return kp->val.i != 0;
      case VAL_UINT: return kp->val.ui != 0;
      case VAL_LONG: return kp->val.l != 0;
      case VAL_ULONG: return kp->val.ul != 0;
      case VAL_LLONG: return kp->val.ll != 0;
      case VAL_ULLONG: return kp->val.ull != 0;
      case VAL_FLOAT: return kp->val.f != 0.0f;
      case VAL_DOUBLE: return kp->val.d != 0.0;
      case VAL_CHAR: return kp->val.c != 0;
      case VAL_STR:
         if (parse_bool_string(kp->val.s, &def))
            return def;
         break;
      default:
         return def;
   }

   return def;
}

int dict_get_int(dict *d, const char *key, int def) {
   keypair *kp;
   long long v;
   unsigned long long uv;
   double dv;

   if (!dict_get_kp(d, key, &kp)) return def;

   switch (kp->val_type) {
      case VAL_INT: return kp->val.i;
      case VAL_UINT:
         if (kp->val.ui <= INT_MAX) return (int)kp->val.ui;
         break;
      case VAL_LONG:
         if (kp->val.l >= INT_MIN && kp->val.l <= INT_MAX) return (int)kp->val.l;
         break;
      case VAL_ULONG:
         if (kp->val.ul <= INT_MAX) return (int)kp->val.ul;
         break;
      case VAL_LLONG:
         if (kp->val.ll >= INT_MIN && kp->val.ll <= INT_MAX) return (int)kp->val.ll;
         break;
      case VAL_ULLONG:
         if (kp->val.ull <= INT_MAX) return (int)kp->val.ull;
         break;
      case VAL_FLOAT:
         dv = kp->val.f;
         if (isfinite(dv) && dv >= INT_MIN && dv <= INT_MAX) return (int)dv;
         break;
      case VAL_DOUBLE:
         dv = kp->val.d;
         if (isfinite(dv) && dv >= INT_MIN && dv <= INT_MAX) return (int)dv;
         break;
      case VAL_CHAR: return (int)kp->val.c;
      case VAL_BOOL: return kp->val.i != 0;
      case VAL_STR:
         if (parse_ll(kp->val.s, &v) && v >= INT_MIN && v <= INT_MAX)
            return (int)v;
         if (parse_ull(kp->val.s, &uv) && uv <= INT_MAX)
            return (int)uv;
         break;
      default:
         break;
   }

   return def;
}

unsigned int dict_get_uint(dict *d, const char *key, unsigned int def) {
   keypair *kp;
   long long sv;
   unsigned long long uv;
   double dv;

   if (!dict_get_kp(d, key, &kp)) return def;

   switch (kp->val_type) {
      case VAL_UINT: return kp->val.ui;
      case VAL_INT:
         if (kp->val.i >= 0) return (unsigned int)kp->val.i;
         break;
      case VAL_LONG:
         if (kp->val.l >= 0 && (unsigned long long)kp->val.l <= UINT_MAX)
            return (unsigned int)kp->val.l;
         break;
      case VAL_ULONG:
         if (kp->val.ul <= UINT_MAX) return (unsigned int)kp->val.ul;
         break;
      case VAL_LLONG:
         if (kp->val.ll >= 0 && (unsigned long long)kp->val.ll <= UINT_MAX)
            return (unsigned int)kp->val.ll;
         break;
      case VAL_ULLONG:
         if (kp->val.ull <= UINT_MAX) return (unsigned int)kp->val.ull;
         break;
      case VAL_FLOAT:
         dv = kp->val.f;
         if (isfinite(dv) && dv >= 0 && dv <= UINT_MAX) return (unsigned int)dv;
         break;
      case VAL_DOUBLE:
         dv = kp->val.d;
         if (isfinite(dv) && dv >= 0 && dv <= UINT_MAX) return (unsigned int)dv;
         break;
      case VAL_BOOL: return kp->val.i != 0;
      case VAL_CHAR:
         return (unsigned int)(unsigned char)kp->val.c;
      case VAL_STR:
         if (parse_ull(kp->val.s, &uv) && uv <= UINT_MAX)
            return (unsigned int)uv;
         if (parse_ll(kp->val.s, &sv) && sv >= 0 && sv <= UINT_MAX)
            return (unsigned int)sv;
         break;
      default:
         break;
   }

   return def;
}

unsigned long dict_get_ulong(dict *d, const char *key, unsigned long def) {
   keypair *kp;
   long long sv;
   unsigned long long uv;
   double dv;

   if (!dict_get_kp(d, key, &kp)) return def;

   switch (kp->val_type) {
      case VAL_ULONG: return kp->val.ul;
      case VAL_UINT: return kp->val.ui;
      case VAL_INT:
         if (kp->val.i >= 0) return (unsigned long)kp->val.i;
         break;
      case VAL_LONG:
         if (kp->val.l >= 0) return (unsigned long)kp->val.l;
         break;
      case VAL_LLONG:
         if (kp->val.ll >= 0) return (unsigned long)kp->val.ll;
         break;
      case VAL_ULLONG:
         if (kp->val.ull <= ULONG_MAX) return (unsigned long)kp->val.ull;
         break;
      case VAL_FLOAT:
         dv = kp->val.f;
         if (isfinite(dv) && dv >= 0 && dv <= ULONG_MAX) return (unsigned long)dv;
         break;
      case VAL_DOUBLE:
         dv = kp->val.d;
         if (isfinite(dv) && dv >= 0 && dv <= ULONG_MAX) return (unsigned long)dv;
         break;
      case VAL_BOOL: return kp->val.i != 0;
      case VAL_STR:
         if (parse_ull(kp->val.s, &uv) && uv <= ULONG_MAX)
            return (unsigned long)uv;
         if (parse_ll(kp->val.s, &sv) && sv >= 0 && (unsigned long long)sv <= ULONG_MAX)
            return (unsigned long)sv;
         break;
      default:
         break;
   }

   return def;
}

long dict_get_long(dict *d, const char *key, long def) {
   keypair *kp;
   long long v;
   double dv;

   if (!dict_get_kp(d, key, &kp)) return def;

   switch (kp->val_type) {
      case VAL_LONG: return kp->val.l;
      case VAL_INT: return kp->val.i;
      case VAL_UINT:
         return (long)kp->val.ui;
      case VAL_ULONG:
         if (kp->val.ul <= LONG_MAX) return (long)kp->val.ul;
         break;
      case VAL_LLONG:
         if (kp->val.ll >= LONG_MIN && kp->val.ll <= LONG_MAX) return (long)kp->val.ll;
         break;
      case VAL_ULLONG:
         if (kp->val.ull <= LONG_MAX) return (long)kp->val.ull;
         break;
      case VAL_FLOAT:
         dv = kp->val.f;
         if (isfinite(dv) && dv >= LONG_MIN && dv <= LONG_MAX) return (long)dv;
         break;
      case VAL_DOUBLE:
         dv = kp->val.d;
         if (isfinite(dv) && dv >= LONG_MIN && dv <= LONG_MAX) return (long)dv;
         break;
      case VAL_BOOL: return kp->val.i != 0;
      case VAL_STR:
         if (parse_ll(kp->val.s, &v) && v >= LONG_MIN && v <= LONG_MAX)
            return (long)v;
         break;
      default:
         break;
   }

   return def;
}

long long dict_get_llong(dict *d, const char *key, long long def) {
   keypair *kp;
   long long v;
   unsigned long long uv;
   double dv;

   if (!dict_get_kp(d, key, &kp)) return def;

   switch (kp->val_type) {
      case VAL_LLONG: return kp->val.ll;
      case VAL_INT: return kp->val.i;
      case VAL_UINT: return kp->val.ui;
      case VAL_LONG: return kp->val.l;
      case VAL_ULONG:
         if (kp->val.ul <= LLONG_MAX) return (long long)kp->val.ul;
         break;
      case VAL_ULLONG:
         if (kp->val.ull <= LLONG_MAX) return (long long)kp->val.ull;
         break;
      case VAL_FLOAT:
         dv = kp->val.f;
         if (isfinite(dv) && dv >= LLONG_MIN && dv <= LLONG_MAX) return (long long)dv;
         break;
      case VAL_DOUBLE:
         dv = kp->val.d;
         if (isfinite(dv) && dv >= LLONG_MIN && dv <= LLONG_MAX) return (long long)dv;
         break;
      case VAL_BOOL: return kp->val.i != 0;
      case VAL_STR:
         if (parse_ll(kp->val.s, &v)) return v;
         if (parse_ull(kp->val.s, &uv) && uv <= LLONG_MAX) return (long long)uv;
         break;
      default:
         break;
   }

   return def;
}

time_t dict_get_time_t(dict *d, const char *key, time_t def) {
   return (time_t)dict_get_llong(d, key, (long long)def);
}

unsigned long long dict_get_ullong(dict *d, const char *key,
                                      unsigned long long def) {
   keypair *kp;
   unsigned long long uv;
   double dv;

   if (!dict_get_kp(d, key, &kp)) return def;

   switch (kp->val_type) {
      case VAL_ULLONG: return kp->val.ull;
      case VAL_LLONG:
         if (kp->val.ll >= 0) return (unsigned long long)kp->val.ll;
         break;
      case VAL_INT:
         if (kp->val.i >= 0) return (unsigned long long)kp->val.i;
         break;
      case VAL_UINT: return kp->val.ui;
      case VAL_LONG:
         if (kp->val.l >= 0) return (unsigned long long)kp->val.l;
         break;
      case VAL_ULONG: return kp->val.ul;
      case VAL_FLOAT:
         dv = kp->val.f;
         if (isfinite(dv) && dv >= 0 && dv <= ULLONG_MAX) return (unsigned long long)dv;
         break;
      case VAL_DOUBLE:
         dv = kp->val.d;
         if (isfinite(dv) && dv >= 0 && dv <= ULLONG_MAX) return (unsigned long long)dv;
         break;
      case VAL_BOOL: return kp->val.i != 0;
      case VAL_CHAR: return (unsigned long long)(unsigned char)kp->val.c;
      case VAL_STR:
         if (parse_ull(kp->val.s, &uv))
            return uv;
         break;
      default:
         break;
   }

   return def;
}

char dict_get_char(dict *d, const char *key, char def) {
   keypair *kp;
   long long v;

   if (!dict_get_kp(d, key, &kp)) return def;

   switch (kp->val_type) {
      case VAL_CHAR: return kp->val.c;
      case VAL_BOOL: return kp->val.i ? 1 : 0;
      case VAL_INT: return (char)kp->val.i;
      case VAL_UINT: return (char)kp->val.ui;
      case VAL_LONG: return (char)kp->val.l;
      case VAL_ULONG: return (char)kp->val.ul;
      case VAL_LLONG: return (char)kp->val.ll;
      case VAL_ULLONG: return (char)kp->val.ull;
      case VAL_FLOAT: return (char)kp->val.f;
      case VAL_DOUBLE: return (char)kp->val.d;
      case VAL_STR:
         if (parse_ll(kp->val.s, &v)) return (char)v;
         if (kp->val.s && kp->val.s[0] && !kp->val.s[1]) return kp->val.s[0];
         break;
      default:
         break;
   }

   return def;
}

double dict_get_double(dict *d, const char *key, double def) {
   keypair *kp;
   double v;

   if (!dict_get_kp(d, key, &kp)) return def;

   switch (kp->val_type) {
      case VAL_DOUBLE: return kp->val.d;
      case VAL_FLOAT: return kp->val.f;
      case VAL_INT: return kp->val.i;
      case VAL_UINT: return kp->val.ui;
      case VAL_LONG: return kp->val.l;
      case VAL_ULONG: return kp->val.ul;
      case VAL_LLONG: return (double)kp->val.ll;
      case VAL_ULLONG: return (double)kp->val.ull;
      case VAL_BOOL: return kp->val.i != 0;
      case VAL_CHAR: return kp->val.c;
      case VAL_STR:
         if (parse_double(kp->val.s, &v)) return v;
         break;
      default:
         break;
   }

   return def;
}

float dict_get_float(dict *d, const char *key, float def) {
   double v = dict_get_double(d, key, (double)def);

   if (!isfinite(v) || v > FLT_MAX || v < -FLT_MAX)
      return def;

   return (float)v;
}

// You *MUST* free the return value
const char *dict_get_exp(dict *d, const char *key) {
   if (!d) {
      return NULL;
   }

   if (!key) {
      Log(LOG_WARN, "config", "dict_get_exp: NULL key!");

      return NULL;
   }
   const char *p = dict_get(d, key, NULL);

   if (!p) {
      return NULL;
   }
   char *buf = malloc(MAX_CFG_EXP_STRLEN);

   if (!buf) {
      Log(LOG_DEBUG, "librustyaxe", "OOM in dict_get_exp!");

      return NULL;
   }
   strlcpy(buf, p, MAX_CFG_EXP_STRLEN);
   buf[MAX_CFG_EXP_STRLEN - 1] = '\0';

   for (int depth = 0 ; depth < MAX_CFG_EXP_RECURSION ; depth++) {
      char tmp[MAX_CFG_EXP_STRLEN];
      char *dst = tmp;
      const char *src = buf;
      int changed = 0;

      while (*src && (dst - tmp) < MAX_CFG_EXP_STRLEN - 1) {
         if (src[0] == '$' && src[1] == '{') {
            const char *end = strchr(src + 2, '}');

            if (end) {
               size_t klen = end - (src + 2);
               char keybuf[256];

               if ( klen >= sizeof(keybuf) ) {
                  klen = sizeof(keybuf) - 1;
               }
               memcpy(keybuf, src + 2, klen);
               keybuf[klen] = '\0';

               const char *val = cfg_get(keybuf);

               if (val) {
                  size_t vlen = strlen(val);

                  if ( (dst - tmp) + vlen >= MAX_CFG_EXP_STRLEN - 1 ) {
                     vlen = MAX_CFG_EXP_STRLEN - 1 - (dst - tmp);
                  }
                  memcpy(dst, val, vlen);
                  dst += vlen;
                  changed = 1;
               }
               src = end + 1;
               continue;
            }
         }
         *dst++ = *src++;
      }
      *dst = '\0';

      if (!changed) {
         break;  // No more expansions
      }
      strlcpy(buf, tmp, MAX_CFG_EXP_STRLEN);
      buf[MAX_CFG_EXP_STRLEN - 1] = '\0';
   }

   // Shrink the allocation down to it's actual size
   size_t final_len = strlen(buf) + 1;
   char *shrunk = realloc(buf, final_len);

   if (shrunk) {
      buf = shrunk;
   }

//   Log(LOG_DEBUG, "librustyaxe", "dict_get_exp: returning %lu bytes for key %s => %s",
// (unsigned long)final_len, key, buf);
   return buf;
}

////////////
static int dict_copy_entry(dict *dst, const keypair *src) {
   if (!dst || !src || !src->key || src->key == DUMMY_PTR) return -1;
   return dict_store(dst, src->key, src->val_type, &src->val);
}

static bool dict_values_equal(const keypair *a, const keypair *b) {
   if (!a || !b || a->val_type != b->val_type) return false;

   switch (a->val_type) {
      case VAL_NULL: return true;
      case VAL_STR:
         return a->val.s == b->val.s ||
                (a->val.s && b->val.s && !strcmp(a->val.s, b->val.s));
      case VAL_INT: return a->val.i == b->val.i;
      case VAL_UINT: return a->val.ui == b->val.ui;
      case VAL_LONG: return a->val.l == b->val.l;
      case VAL_ULONG: return a->val.ul == b->val.ul;
      case VAL_LLONG: return a->val.ll == b->val.ll;
      case VAL_ULLONG: return a->val.ull == b->val.ull;
      case VAL_FLOAT: return a->val.f == b->val.f;
      case VAL_DOUBLE: return a->val.d == b->val.d;
      case VAL_BOOL: return (a->val.i != 0) == (b->val.i != 0);
      case VAL_CHAR: return a->val.c == b->val.c;
      case VAL_PTR: return a->val.p == b->val.p;
      default: return false;
   }
}

static keypair *dict_find_entry(dict *d, const char *key) {
   keypair *kp;
   return dict_get_kp(d, key, &kp) ? kp : NULL;
}

int dict_merge(dict *dst, dict *src) {
   const char *key;
   dict_value_t val;
   val_type_t type;
   int rank = 0;

   if (!dst || !src) return -1;

   while ((rank = dict_enumerate_typed(src, rank, &key, &val, &type)) >= 0) {
      keypair tmp = {
         .key = key,
         .val_type = type,
         .val = val
      };

      if (dict_copy_entry(dst, &tmp) != 0)
         return -1;
   }

   return 0;
}

dict *dict_merge_new(dict *a, dict *b) {
   dict *merged;

   if (!a || !b) {
      Log(LOG_WARN, "dict", "dict_merge_new called with NULL a <%p> or NULL b <%p>", a, b);
      return NULL;
   }

   merged = dict_new();
   if (!merged) return NULL;

   if (dict_merge(merged, a) != 0 || dict_merge(merged, b) != 0) {
      dict_free(merged);
      return NULL;
   }

   return merged;
}

/*
 * Return a new dictionary containing entries from B whose type or value
 * differs from A. Values in the result are copied with their native types.
 *
 * Keys which existed in A but not B are represented as VAL_NULL, which
 * serves as the deletion marker in the diff.
 */
dict *dict_diff(dict *a, dict *b) {
   dict *diff;
   const char *key;
   dict_value_t val;
   val_type_t type;
   int rank = 0;

   if (!a || !b) return NULL;

   diff = dict_new();
   if (!diff) return NULL;

   while ((rank = dict_enumerate_typed(b, rank, &key, &val, &type)) >= 0) {
      keypair *old = dict_find_entry(a, key);
      keypair cur = {
         .key = key,
         .val_type = type,
         .val = val
      };

      if (!old || !dict_values_equal(old, &cur)) {
         if (dict_copy_entry(diff, &cur) != 0) {
            dict_free(diff);
            return NULL;
         }
      }
   }

   /*
    * Keys removed from B are represented by JSON/dict null. This gives the
    * diff a usable deletion marker without adding another public value type.
    */
   rank = 0;
   while ((rank = dict_enumerate_typed(a, rank, &key, &val, &type)) >= 0) {
      if (!dict_find_entry(b, key)) {
         if (dict_add_null(diff, key) != 0) {
            dict_free(diff);
            return NULL;
         }
      }
   }

   return diff;
}
