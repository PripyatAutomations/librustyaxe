#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/tui.completion.h>
static void describe(const char *line, const char *word, char *out, size_t capacity) {
   assert(!strcmp(line,"/media subscribe "));
   snprintf(out,capacity,"%s — Main receiver [RX opus]",word);
}
int main(void) {
   char word[]="rig0.vfo_a.rx", label[128];
   completion_describe("/media subscribe ",word,label,sizeof(label));
   assert(!strcmp(word,label));
   tui_set_completion_describer(describe);
   completion_describe("/media subscribe ",word,label,sizeof(label));
   assert(strstr(label,"Main receiver") && !strcmp(word,"rig0.vfo_a.rx"));
   char small[8];completion_describe("/media subscribe ",word,small,sizeof(small));
   assert(small[sizeof(small)-1]=='\0');
   tui_set_completion_describer(NULL);
   completion_describe("/media subscribe ",word,label,sizeof(label));
   assert(!strcmp(word,label));
   puts("PASS: completion labels keep insertion words intact, truncate safely and reset");
}
