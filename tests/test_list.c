//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Unit tests for librustyaxe/list.c (doubly-linked list helpers)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/list.h>

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

static int list_len(rrlist_t **list)
{
   int n = 0;
   for (rrlist_t *lp = *list; lp; lp = lp->next)
      n++;
   return n;
}

static void test_add_head_tail(void)
{
   rrlist_t *list = NULL;
   static int vals[4];
   CHECK(rrlist_add(&list, &vals[1], LIST_TAIL) != NULL);
   CHECK(rrlist_add(&list, &vals[0], LIST_HEAD) != NULL);
   CHECK(list_len(&list) == 2);
   rrlist_t *lp = rrlist_find_by_ptr(list, &vals[1]);
   CHECK(lp != NULL);
   rrlist_destroy(&list);
   CHECK(list == NULL);
}

static void test_find_missing(void)
{
   rrlist_t *list = NULL;
   static int val = 42;
   CHECK(rrlist_find_by_ptr(list, &val) == NULL); // empty list
   rrlist_add(&list, &val, LIST_TAIL);
   static int other = 7;
   CHECK(rrlist_find_by_ptr(list, &other) == NULL);
   CHECK(rrlist_find_by_ptr(list, &val) != NULL);
   rrlist_destroy(&list);
}

static void test_remove(void)
{
   rrlist_t *list = NULL;
   static int vals[3];
   rrlist_add(&list, &vals[0], LIST_TAIL);
   rrlist_add(&list, &vals[1], LIST_TAIL);
   rrlist_add(&list, &vals[2], LIST_TAIL);
   rrlist_t *mid = rrlist_find_by_ptr(list, &vals[1]);
   CHECK(mid != NULL);
   CHECK(rrlist_remove(&list, mid) != false);
   CHECK(list_len(&list) == 2);
   CHECK(rrlist_find_by_ptr(list, &vals[1]) == NULL);
   // Remove head and tail
   CHECK(rrlist_remove(&list, *list) != false);
   CHECK(list_len(&list) == 1);
   rrlist_destroy(&list);
   CHECK(list == NULL);
}

static void test_order(void)
{
   rrlist_t *list = NULL;
   static int vals[3];
   rrlist_add(&list, &vals[0], LIST_TAIL);
   rrlist_add(&list, &vals[1], LIST_TAIL);
   rrlist_add(&list, &vals[2], LIST_HEAD);
   // vals[2] at head, then vals[0], vals[1]
   CHECK((*list).data == &vals[2]);
   CHECK((*list).next->data == &vals[0]);
   CHECK((*list).next->next->data == &vals[1]);
   rrlist_destroy(&list);
}

int main(void)
{
   test_add_head_tail();
   test_find_missing();
   test_remove();
   test_order();
   if (failures)
   {
      fprintf(stderr, "%d failure(s)\n", failures);
      return 1;
   }
   printf("test_list: all tests passed\n");
   return 0;
}
