/* SPDX-License-Identifier: MIT */
#include "../runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
   int calls;
   int64_t last;
} fixture_t;
static rrs_value_t *record(rrs_vm_t *vm,int argc,const rrs_value_t *const args[],void *user) {
   fixture_t *f=user;
   if(argc!=1||rrs_value_type(args[0])!=RRS_INT) {
      rrs_vm_raise(vm,"record expects one integer");
      return NULL;
   }
   f->calls++;
   f->last=rrs_value_int(args[0]);
   return rrs_null();
}
static void syntax_and_invocation(void) {
   rrs_vm_t *vm=rrs_vm_new();
   assert(vm);
   fixture_t f={0};
   assert(rrs_vm_register_native(vm,"test.record",record,&f));
   assert(rrs_vm_load(vm,
      "script(\"/test\", { int x = 2; test.record(x + 3) });\n"
      "hotkey _&^X { test.record(7); }\n"
      "event irc.privmsg { if (event.count == 12) { test.record(42); } else { test.record(1); } }\n"));
   assert(rrs_vm_binding_count(vm)==3);
   assert(f.calls==0);
   assert(rrs_vm_invoke(vm,RRSCRIPT_COMMAND,"/test",NULL));
   assert(f.calls==1&&f.last==5);
   assert(rrs_vm_invoke(vm,RRSCRIPT_HOTKEY,"_&^X",NULL));
   assert(f.calls==2&&f.last==7);
   rrs_value_t *ev=rrs_dictionary();
   rrs_value_t *v=rrs_integer(12);
   assert(rrs_dict_put(ev,"count",v));
   rrs_value_free(v);
   assert(rrs_vm_invoke(vm,RRSCRIPT_EVENT,"irc.privmsg",ev));
   assert(f.calls==3&&f.last==42);
   rrs_value_free(ev);
   assert(!rrs_vm_load(vm,"event wrong { print(1)"));
   assert(rrs_vm_binding_count(vm)==3);
   assert(rrs_vm_invoke(vm,RRSCRIPT_COMMAND,"/test",NULL));
   assert(f.calls==4);
   assert(!rrs_vm_invoke(vm,RRSCRIPT_COMMAND,"/missing",NULL));
   rrs_vm_free(vm);
}
static void repeated_events(void) {
   rrs_vm_t *vm=rrs_vm_new();
   fixture_t f={0};
   assert(rrs_vm_register_native(vm,"test.record",record,&f));
   assert(rrs_vm_load(vm,
      "event sample.update { test.record(1) }\n"
      "event sample.update { test.record(2) }\n"));
   assert(rrs_vm_invoke(vm,RRSCRIPT_EVENT,"sample.update",NULL));
   assert(f.calls==2 && f.last==2);
   rrs_vm_free(vm);
}
static rrs_value_t *fake_find(rrs_vm_t *vm,int argc,const rrs_value_t *const args[],void *user) {
   (void)vm;(void)user;
   assert(argc==2);
   assert(!strcmp(rrs_value_string(args[0]),"Spectrum"));
   assert(!strcmp(rrs_value_string(args[1]),"W"));
   rrs_value_t *d=rrs_dictionary();
   rrs_value_t *v=rrs_string("rf-spectrum");
   assert(rrs_dict_put(d,"id",v));
   rrs_value_free(v);
   return d;
}
static rrs_value_t *fake_move(rrs_vm_t *vm,int argc,const rrs_value_t *const args[],void *user) {
   (void)vm;
   fixture_t *state=user;
   assert(argc==2 || argc==4);
   assert(!strcmp(rrs_value_string(args[0]),"rf-spectrum"));
   assert(!strcmp(rrs_value_string(args[1]),"E"));
   if(argc==4) {
      assert(rrs_value_int(args[2])==20);
      assert(rrs_value_int(args[3])==40);
   }
   state->calls++;
   return rrs_boolean(true);
}
static void panel_script(void) {
   rrs_vm_t *vm=rrs_vm_new();
   fixture_t state={0};
   assert(rrs_vm_register_native(vm,"window.find",fake_find,NULL));
   assert(rrs_vm_register_native(vm,"window.move",fake_move,&state));
   assert(rrs_vm_load(vm,
      "script(\"/dock\", {\n"
      "  dict w = window.find(\"Spectrum\", \"W\")\n"
      "  if (w != null) {\n"
      "    window.move(w.id, \"E\", 20, 40)\n"
      "  }\n"
      "});\n"
      "script(\"/dock-restore\", {\n"
      "  dict w = window.find(\"Spectrum\", \"W\")\n"
      "  window.move(w.id, \"E\")\n"
      "});\n"));
   assert(rrs_vm_invoke(vm,RRSCRIPT_COMMAND,"/dock",NULL));
   assert(rrs_vm_invoke(vm,RRSCRIPT_COMMAND,"/dock-restore",NULL));
   assert(state.calls==2);
   rrs_vm_free(vm);
}
static void values(void) {
   rrs_value_t *d=rrs_dictionary();
   rrs_value_t *n=rrs_integer(9);
   assert(rrs_dict_put(d,"a.b",n));
   rrs_value_free(n);
   rrs_value_t *copy=rrs_value_copy(d);
   assert(rrs_value_int(rrs_value_field(copy,"a.b"))==9);
   n=rrs_integer(10);
   assert(rrs_dict_put(copy,"a.b",n));
   assert(rrs_value_int(rrs_value_field(d,"a.b"))==9);
   rrs_value_free(n);rrs_value_free(copy);rrs_value_free(d);
}
int main(void) {
   syntax_and_invocation();
   repeated_events();
   panel_script();
   values();
   puts("RustyScript runtime tests passed");
   return 0;
}
