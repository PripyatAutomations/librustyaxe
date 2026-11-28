/* SPDX-License-Identifier: MIT */
#include "../patchbay.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
   rrs_patch_graph_t *g=rrs_patch_graph_new();
   assert(g);
   assert(rrs_patch_node_add(g,"source",10,20));
   assert(rrs_patch_node_add(g,"gate",30,40));
   assert(rrs_patch_node_add(g,"sink",50,60));
   assert(!rrs_patch_node_add(g,"source",1,2));
   assert(!rrs_patch_node_move(g,"gate",NAN,4));
   assert(rrs_patch_port_add(g,"source","out",1,RRS_PATCH_OUTPUT));
   assert(rrs_patch_port_add(g,"gate","in",1,RRS_PATCH_INPUT));
   assert(rrs_patch_port_add(g,"gate","out",1,RRS_PATCH_OUTPUT));
   assert(rrs_patch_port_add(g,"sink","in",1,RRS_PATCH_INPUT));
   assert(rrs_patch_port_add(g,"sink","other",2,RRS_PATCH_INPUT));
   assert(!rrs_patch_connect(g,"source","out","sink","other"));
   assert(!rrs_patch_connect(g,"source","out","gate","out"));
   assert(rrs_patch_connect(g,"source","out","gate","in"));
   assert(rrs_patch_connect(g,"gate","out","sink","in"));
   assert(!rrs_patch_connect(g,"gate","out","sink","in"));
   assert(!rrs_patch_connect(g,"sink","in","gate","in"));
   assert(!rrs_patch_connect(g,"gate","out","gate","in"));
   assert(rrs_patch_node_move(g,"gate",333,444));
   assert(rrs_patch_node_find(g,"gate")->x==333);
   assert(rrs_patch_link_count(g)==2);
   assert(!strcmp(rrs_patch_link_at(g,0)->from,"source"));
   assert(rrs_patch_port_remove(g,"gate","in"));
   assert(rrs_patch_link_count(g)==1);
   assert(rrs_patch_node_remove(g,"gate"));
   assert(rrs_patch_link_count(g)==0);
   assert(rrs_patch_node_count(g)==2);
   assert(!rrs_patch_node_find(g,"gate"));
   rrs_patch_graph_free(g);
   puts("PASS: reusable C patchbay topology");
   return 0;
}
