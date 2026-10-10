/* Manual benchmark, not a timing assertion. Run from the repository root:
 * cc -O2 -I. -Ibuild/radio librustyaxe/tests/bench_dict.c -L. \
 *    -Wl,-rpath,"$PWD" -lrustyaxe -o /tmp/bench-dict
 * /tmp/bench-dict
 * Uses the project's monotonic clock; no hardware or hash is selected here. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>

static const char *keys[] = {
   "msg.type", "property.cmd", "property.name", "property.type",
   "property.value", "property.observed", "property.known", "property.available",
   "property.version", "target", "request.id", "stream.epoch", "stream.seq",
   "object.uuid", "object.owner", "object.type", "object.alias", "object.name",
   "object.lifecycle", "object.room"
};
#define KEY_COUNT (sizeof(keys) / sizeof(keys[0]))

static dict *sample(void) {
   dict *d = dict_new();
   assert(d);
   for (unsigned i = 0 ; i < KEY_COUNT ; i++) {
      assert(!dict_add(d, keys[i], "representative-value"));
   }

   return d;
}

static void report(const char *name, long long start, unsigned iterations) {
   long long elapsed = mono_us() - start;
   printf("%-24s %lld ns/operation (%u iterations)\n", name, elapsed * 1000 / iterations, iterations);
}

int main(void) {
   dict *d = sample();
   unsigned checksum = 0;
   long long start = mono_us();
   for (unsigned i = 0 ; i < 1000000 ; i++) {
      const char *value = dict_get(d, keys[i % KEY_COUNT], NULL);
      assert(value);
      checksum += (unsigned char)*value;
   }
   report("lookup, existing key", start, 1000000);
   start = mono_us();
   for (unsigned i = 0 ; i < 1000000 ; i++) {
      assert(!dict_get(d, "property.missing", NULL));
   }
   report("lookup, absent key", start, 1000000);
   start = mono_us();
   for (unsigned i = 0 ; i < 10000 ; i++) {
      dict *copy = sample();
      dict_free(copy);
   }
   report("build/free 20 fields", start, 10000);
   start = mono_us();
   for (unsigned i = 0 ; i < 10000 ; i++) {
      char *json = dict2json(d);
      assert(json);
      checksum += (unsigned char)*json;
      free(json);
   }
   report("serialize 20 fields", start, 10000);
   char *json = dict2json(d);
   assert(json);
   start = mono_us();
   for (unsigned i = 0 ; i < 10000 ; i++) {
      dict *parsed = json2dict(json);
      assert(parsed && dict_get(parsed, "property.name", NULL));
      dict_free(parsed);
   }
   report("parse/free 20 fields", start, 10000);
   printf("Dictionary: %zu fields, %zu JSON bytes; checksum=%u\n", KEY_COUNT, strlen(json), checksum);
   free(json);
   dict_free(d);

   return 0;
}
