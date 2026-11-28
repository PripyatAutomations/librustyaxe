/* SPDX-License-Identifier: MIT */
#include "patchbay.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
struct rrs_patch_graph {
   size_t nodes,links;
   uint64_t revision;
   rrs_patch_node_t node[RRS_PATCH_MAX_NODES];
   rrs_patch_link_t link[RRS_PATCH_MAX_LINKS];
};
static bool name_ok(const char *s) {
   if (!s || !*s) { return false; }
   size_t i=0;
   while (s[i] && i<RRS_PATCH_NAME_MAX) {
      unsigned char ch=(unsigned char)s[i++];
      if (ch<33 || ch==127) { return false; }
   }
   return i<RRS_PATCH_NAME_MAX;
}
static size_t node_index(const rrs_patch_graph_t *g,const char *id) {
   if (!g || !id) { return SIZE_MAX; }
   for (size_t i=0;i<g->nodes;i++) {
      if (!strcmp(g->node[i].id,id)) { return i; }
   }
   return SIZE_MAX;
}
static size_t port_index(const rrs_patch_node_t *n,const char *id) {
   if (!n || !id) { return SIZE_MAX; }
   for (size_t i=0;i<n->port_count;i++) {
      if (!strcmp(n->ports[i].id,id)) { return i; }
   }
   return SIZE_MAX;
}
rrs_patch_graph_t *rrs_patch_graph_new(void) { return calloc(1,sizeof(rrs_patch_graph_t)); }
void rrs_patch_graph_free(rrs_patch_graph_t *g) { free(g); }
uint64_t rrs_patch_revision(const rrs_patch_graph_t *g) { return g?g->revision:0; }
size_t rrs_patch_node_count(const rrs_patch_graph_t *g) { return g?g->nodes:0; }
size_t rrs_patch_link_count(const rrs_patch_graph_t *g) { return g?g->links:0; }
const rrs_patch_node_t *rrs_patch_node_at(const rrs_patch_graph_t *g,size_t i) {
   return g && i<g->nodes ? &g->node[i] : NULL;
}
const rrs_patch_link_t *rrs_patch_link_at(const rrs_patch_graph_t *g,size_t i) {
   return g && i<g->links ? &g->link[i] : NULL;
}
const rrs_patch_node_t *rrs_patch_node_find(const rrs_patch_graph_t *g,const char *id) {
   size_t i=node_index(g,id);
   return i!=SIZE_MAX ? &g->node[i] : NULL;
}
bool rrs_patch_node_add(rrs_patch_graph_t *g,const char *id,double x,double y) {
   if (!g || !name_ok(id) || !isfinite(x) || !isfinite(y) ||
       g->nodes==RRS_PATCH_MAX_NODES || node_index(g,id)!=SIZE_MAX) { return false; }
   rrs_patch_node_t *n=&g->node[g->nodes++];
   *n=(rrs_patch_node_t){.x=x,.y=y};
   strcpy(n->id,id);
   g->revision++;
   return true;
}
bool rrs_patch_node_move(rrs_patch_graph_t *g,const char *id,double x,double y) {
   size_t i=node_index(g,id);
   if (i==SIZE_MAX || !isfinite(x) || !isfinite(y)) { return false; }
   g->node[i].x=x; g->node[i].y=y;
   g->revision++;
   return true;
}
static bool link_has(const rrs_patch_link_t *w,const char *from,const char *out,
                     const char *to,const char *in) {
   return !strcmp(w->from,from) && !strcmp(w->out,out) &&
          !strcmp(w->to,to) && !strcmp(w->in,in);
}
bool rrs_patch_disconnect(rrs_patch_graph_t *g,const char *from,const char *out,
                          const char *to,const char *in) {
   if (!g || !from || !out || !to || !in) { return false; }
   for (size_t i=0;i<g->links;i++) {
      if (!link_has(&g->link[i],from,out,to,in)) { continue; }
      memmove(&g->link[i],&g->link[i+1],(g->links-i-1)*sizeof(g->link[0]));
      g->links--;
      g->revision++;
      return true;
   }
   return false;
}
bool rrs_patch_node_remove(rrs_patch_graph_t *g,const char *id) {
   size_t i=node_index(g,id);
   if (i==SIZE_MAX) { return false; }
   size_t keep=0;
   for (size_t j=0;j<g->links;j++) {
      if (strcmp(g->link[j].from,id) && strcmp(g->link[j].to,id)) {
         g->link[keep++]=g->link[j];
      }
   }
   g->links=keep;
   memmove(&g->node[i],&g->node[i+1],(g->nodes-i-1)*sizeof(g->node[0]));
   g->nodes--;
   g->revision++;
   return true;
}
bool rrs_patch_port_add(rrs_patch_graph_t *g,const char *id,const char *port,
                        uint32_t type,rrs_patch_direction_t direction) {
   size_t i=node_index(g,id);
   if (i==SIZE_MAX || !name_ok(port) || !type || direction>RRS_PATCH_OUTPUT) { return false; }
   rrs_patch_node_t *n=&g->node[i];
   if (n->port_count==RRS_PATCH_MAX_PORTS || port_index(n,port)!=SIZE_MAX) { return false; }
   rrs_patch_port_t *p=&n->ports[n->port_count++];
   *p=(rrs_patch_port_t){.type=type,.direction=direction};
   strcpy(p->id,port);
   g->revision++;
   return true;
}
bool rrs_patch_port_remove(rrs_patch_graph_t *g,const char *id,const char *port) {
   size_t i=node_index(g,id);
   if (i==SIZE_MAX) { return false; }
   rrs_patch_node_t *n=&g->node[i];
   size_t p=port_index(n,port);
   if (p==SIZE_MAX) { return false; }
   size_t keep=0;
   for (size_t j=0;j<g->links;j++) {
      if ((strcmp(g->link[j].from,id) || strcmp(g->link[j].out,port)) &&
          (strcmp(g->link[j].to,id) || strcmp(g->link[j].in,port))) {
         g->link[keep++]=g->link[j];
      }
   }
   g->links=keep;
   memmove(&n->ports[p],&n->ports[p+1],(n->port_count-p-1)*sizeof(n->ports[0]));
   n->port_count--;
   g->revision++;
   return true;
}
static bool reaches(const rrs_patch_graph_t *g,const char *at,const char *target,
                    bool seen[RRS_PATCH_MAX_NODES]) {
   size_t i=node_index(g,at);
   if (i==SIZE_MAX || seen[i]) { return false; }
   if (!strcmp(at,target)) { return true; }
   seen[i]=true;
   for (size_t j=0;j<g->links;j++) {
      if (!strcmp(g->link[j].from,at) && reaches(g,g->link[j].to,target,seen)) {
         return true;
      }
   }
   return false;
}
bool rrs_patch_connect(rrs_patch_graph_t *g,const char *from,const char *out,
                       const char *to,const char *in) {
   size_t a=node_index(g,from),b=node_index(g,to);
   if (a==SIZE_MAX || b==SIZE_MAX || a==b || !out || !in ||
       g->links==RRS_PATCH_MAX_LINKS) { return false; }
   size_t p=port_index(&g->node[a],out),q=port_index(&g->node[b],in);
   if (p==SIZE_MAX || q==SIZE_MAX) { return false; }
   const rrs_patch_port_t *src=&g->node[a].ports[p],*dst=&g->node[b].ports[q];
   if (src->direction!=RRS_PATCH_OUTPUT || dst->direction!=RRS_PATCH_INPUT ||
       src->type!=dst->type) { return false; }
   for (size_t i=0;i<g->links;i++) {
      if (link_has(&g->link[i],from,out,to,in)) { return false; }
   }
   bool seen[RRS_PATCH_MAX_NODES]={0};
   if (reaches(g,to,from,seen)) { return false; }
   rrs_patch_link_t *w=&g->link[g->links++];
   *w=(rrs_patch_link_t){0};
   strcpy(w->from,from); strcpy(w->out,out);
   strcpy(w->to,to); strcpy(w->in,in);
   g->revision++;
   return true;
}

/* One-hop synchronous fanout; the host owns scheduling, DSP and permission
 * checks. No receiver runs for an invalid port or incompatible payload. */
size_t rrs_patch_emit(const rrs_patch_graph_t *g,const char *from,
                      const char *out,rrs_patch_value_t value,
                      rrs_patch_receive_fn receive,void *user) {
   if (!receive || !g || !out || !from) { return 0; }
   size_t node=node_index(g,from);
   if (node==SIZE_MAX) { return 0; }
   size_t port=port_index(&g->node[node],out);
   if (port==SIZE_MAX ||
       g->node[node].ports[port].direction!=RRS_PATCH_OUTPUT ||
       g->node[node].ports[port].type!=value.type ||
       (value.valid && (!value.data || !value.bytes))) { return 0; }
   size_t delivered=0;
   for (size_t i=0;i<g->links;i++) {
      const rrs_patch_link_t *link=&g->link[i];
      if (strcmp(link->from,from) || strcmp(link->out,out)) { continue; }
      if (!receive(link,value,user)) { break; }
      delivered++;
   }
   return delivered;
}
