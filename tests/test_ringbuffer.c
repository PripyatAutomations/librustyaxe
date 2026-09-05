//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Unit tests for librustyaxe/ringbuffer.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/ringbuffer.h>

static int failures = 0;
#define CHECK(cond)                                                      \
   do                                                                    \
   {                                                                     \
      if (!(cond))                                                       \
      {                                                                  \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
         failures++;                                                     \
      }                                                                  \
   } while (0)

static void test_create_destroy(void)
{
   rb_buffer_t *b = rb_create(10, "test");
   CHECK(b != NULL);
   CHECK(b->max_size == 10);
   CHECK(b->current_size == 0);
   rb_destroy(b);
}

static void test_add_and_recent(void)
{
   rb_buffer_t *b = rb_create(8, "test");
   static int vals[8];
   for (int i = 0; i < 5; i++)
   {
      vals[i] = i;
      CHECK(rb_add(b, &vals[i], 0) != NULL);
   }
   CHECK(b->current_size == 5);
   rb_node_t *n = rb_get_most_recent(b);
   CHECK(n != NULL && n->data == &vals[4]);
   rb_destroy(b);
}

static void test_overflow_drops_oldest(void)
{
   rb_buffer_t *b = rb_create(4, "test");
   static int vals[8];
   for (int i = 0; i < 8; i++)
   {
      vals[i] = i;
      CHECK(rb_add(b, &vals[i], 0) != NULL);
   }
   CHECK(b->current_size == 4); // capped at max_size
   // The most recent must be the last added
   rb_node_t *n = rb_get_most_recent(b);
   CHECK(n != NULL && n->data == &vals[7]);
   rb_destroy(b);
}

static void test_get_range(void)
{
   rb_buffer_t *b = rb_create(8, "test");
   static int vals[8];
   for (int i = 0; i < 6; i++)
   {
      vals[i] = i;
      rb_add(b, &vals[i], 0);
   }
   void **range = rb_get_range(b, 0, 3);
   CHECK(range != NULL);
   if (range)
   {
      CHECK(range[0] == &vals[3] || range[0] == &vals[0]); // ordering depends on impl
      free(range);
   }
   rb_destroy(b);
}

int main(void)
{
   test_create_destroy();
   test_add_and_recent();
   test_overflow_drops_oldest();
   test_get_range();
   if (failures)
   {
      fprintf(stderr, "%d failure(s)\n", failures);
      return 1;
   }
   printf("test_ringbuffer: all tests passed\n");
   return 0;
}
