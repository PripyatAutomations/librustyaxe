#include <assert.h>
#include <stdbool.h>
#include <time.h>

bool dying = true;
bool restarting = true;
time_t now = 123;

int main(void)
{
   assert(dying);
   assert(restarting);
   assert(now == 123);

   return 0;
}
