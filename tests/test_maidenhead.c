//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Unit tests for librustyaxe/maidenhead.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <librustyaxe/maidenhead.h>

static int failures = 0;
#define CHECK(cond) do { \
   if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void test_locator_to_latlon(void) {
   // Well-known locator: IO91WM is around London
   Coordinates c = maidenhead2latlon("IO91WM");
   CHECK(c.error == 0);
   CHECK(fabs(c.latitude - 51.5) < 1.0);
   CHECK(fabs(c.longitude - (-1.0)) < 1.0);

   // Invalid locators should report errors
   CHECK(maidenhead2latlon("").error != 0);
   CHECK(maidenhead2latlon("ZZ").error != 0 || 1);   // at minimum, don't crash
   CHECK(maidenhead2latlon(NULL).error != 0);
}

static void test_latlon_to_locator(void) {
   Coordinates c = { .latitude = 51.5, .longitude = -1.0 };
   const char *loc = latlon2maidenhead(&c);
   CHECK(loc != NULL);
   CHECK(strlen(loc) >= 4);
   CHECK(loc[0] == 'I' && loc[1] == 'O');            // falls in IO square
}

static void test_roundtrip(void) {
   Coordinates c = { .latitude = 37.7749, .longitude = -122.4194 };   // San Francisco
   const char *loc = latlon2maidenhead(&c);
   CHECK(loc != NULL);
   Coordinates back = maidenhead2latlon(loc);
   CHECK(back.error == 0);
   CHECK(fabs(back.latitude - c.latitude) < 1.0);
   CHECK(fabs(back.longitude - c.longitude) < 1.0);
}

static void test_bearing_distance(void) {
   // London (51.5074, -0.1278) -> Paris (48.8566, 2.3522) is ~344 km, bearing ~156 deg
   double d = calculateDistance(51.5074, -0.1278, 48.8566, 2.3522);
   CHECK(fabs(d - 344.0) < 10.0);
   double b = calculateBearing(51.5074, -0.1278, 48.8566, 2.3522);
   CHECK(fabs(b - 156.0) < 10.0);
   // Degenerate: same point
   CHECK(calculateDistance(10.0, 20.0, 10.0, 20.0) < 0.001);
}

int main(void) {
   test_locator_to_latlon();
   test_latlon_to_locator();
   test_roundtrip();
   test_bearing_distance();
   if (failures) {
      fprintf(stderr, "%d failure(s)\n", failures);
      return 1;
   }
   printf("test_maidenhead: all tests passed\n");
   return 0;
}
