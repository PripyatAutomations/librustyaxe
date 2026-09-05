//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Unit tests for librustyaxe/util.time.c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/util.time.h>

// librustyaxe requires the application to provide and periodically refresh
// this global (see logger.c: "you must provide a >= 1hz refresh rate")
time_t now;

static int failures = 0;
#define CHECK(cond) do { \
   if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void test_dhms2time_t(void) {
   CHECK(dhms2time_t(NULL) == 0);
   CHECK(dhms2time_t("") == 0);
   CHECK(dhms2time_t("90s") == 90);
   CHECK(dhms2time_t("1m30s") == 90);
   CHECK(dhms2time_t("2h") == 2 * 3600);
   CHECK(dhms2time_t("1d12h") == 36 * 3600);
   CHECK(dhms2time_t("1w") == 7 * 24 * 3600);
   CHECK(dhms2time_t("1y") == 365 * 24 * 3600);
   CHECK(dhms2time_t("1h30m") == 5400);
   // No unit = ignored by strtol loop, but shouldn't crash or loop forever
   CHECK(dhms2time_t("42") == 0);
   // Garbage tail after a valid prefix
   CHECK(dhms2time_t("5sx") == 5);
}

static void test_time_t2dhms(void) {
   char *s;

   s = time_t2dhms(0);
   CHECK(s != NULL && strcmp(s, "0s") == 0);
   free(s);

   s = time_t2dhms(-5);
   CHECK(s != NULL && strcmp(s, "0s") == 0);
   free(s);

   s = time_t2dhms(90);
   CHECK(s != NULL && strcmp(s, "1m30s") == 0);
   free(s);

   s = time_t2dhms(5400);
   CHECK(s != NULL && strcmp(s, "1h30m") == 0);
   free(s);

   s = time_t2dhms(36 * 3600 + 30);
   CHECK(s != NULL && strcmp(s, "1d12h30s") == 0);
   free(s);

   // Round-trip: format then parse
   time_t orig = 2 * 365 * 24 * 3600 + 3 * 24 * 3600 + 4 * 3600 + 5 * 60 + 6;
   s = time_t2dhms(orig);
   CHECK(s != NULL);
   CHECK(dhms2time_t(s) == orig);
   free(s);
}

static void test_format_timestamp(void) {
   char buf[64];
   time_t t = 875000000;   // 1997-09-24 (UTC); localtime may shift, so just check shape
   format_timestamp(t, buf, sizeof(buf));
   CHECK(strlen(buf) == 20);
   CHECK(buf[0] == '[' && buf[19] == ']');
   CHECK(buf[5] == '/' && buf[8] == '/');
   CHECK(buf[11] == ':' && buf[14] == ':' && buf[17] == ':');
   // Truncation must not overflow: tiny buffer produces empty/short string
   char tiny[4];
   format_timestamp(t, tiny, sizeof(tiny));
   CHECK(strlen(tiny) < 4);
}

static void test_timespec_diff_ms(void) {
   struct timespec a = { .tv_sec = 10, .tv_nsec = 500000000 };
   struct timespec b = { .tv_sec = 10, .tv_nsec = 0 };
   CHECK(timespec_diff_ms(&a, &b) == 500);
   CHECK(timespec_diff_ms(&b, &a) == -500);

   struct timespec c = { .tv_sec = 12, .tv_nsec = 250000000 };
   CHECK(timespec_diff_ms(&c, &a) == 1750);

   // Sub-millisecond truncation
   struct timespec d = { .tv_sec = 0, .tv_nsec = 999999 };
   struct timespec z = { .tv_sec = 0, .tv_nsec = 0 };
   CHECK(timespec_diff_ms(&d, &z) == 0);
}

int main(void) {
   test_dhms2time_t();
   test_time_t2dhms();
   test_format_timestamp();
   test_timespec_diff_ms();

   if (failures > 0) {
      fprintf(stderr, "%s: %d failure(s)\n", __FILE__, failures);
      return 1;
   }
   printf("%s: all tests passed\n", __FILE__);
   return 0;
}
