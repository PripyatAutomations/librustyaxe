/* SPDX-License-Identifier: MIT */
#ifndef RRS_PATCHBAY_H
#define RRS_PATCHBAY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* C owns topology. UI clients and scripts operate on the same graph handle.
 * Signal identifiers are application-defined and must be nonzero. */
#define RRS_PATCH_NAME_MAX 64
#define RRS_PATCH_MAX_NODES 128
#define RRS_PATCH_MAX_PORTS 32
#define RRS_PATCH_MAX_LINKS 512
typedef enum { RRS_PATCH_INPUT, RRS_PATCH_OUTPUT } rrs_patch_direction_t;
typedef struct {
   char id[RRS_PATCH_NAME_MAX];
   uint32_t type;
   rrs_patch_direction_t direction;
} rrs_patch_port_t;
typedef struct {
   char id[RRS_PATCH_NAME_MAX];
   double x,y;
   size_t port_count;
   rrs_patch_port_t ports[RRS_PATCH_MAX_PORTS];
} rrs_patch_node_t;
typedef struct {
   char from[RRS_PATCH_NAME_MAX],out[RRS_PATCH_NAME_MAX];
   char to[RRS_PATCH_NAME_MAX],in[RRS_PATCH_NAME_MAX];
} rrs_patch_link_t;
typedef struct rrs_patch_graph rrs_patch_graph_t;
rrs_patch_graph_t *rrs_patch_graph_new(void);
void rrs_patch_graph_free(rrs_patch_graph_t *graph);
bool rrs_patch_node_add(rrs_patch_graph_t *graph,const char *id,double x,double y);
bool rrs_patch_node_move(rrs_patch_graph_t *graph,const char *id,double x,double y);
bool rrs_patch_node_remove(rrs_patch_graph_t *graph,const char *id);
bool rrs_patch_port_add(rrs_patch_graph_t *graph,const char *node,
                        const char *port,uint32_t type,rrs_patch_direction_t direction);
bool rrs_patch_port_remove(rrs_patch_graph_t *graph,const char *node,const char *port);
bool rrs_patch_connect(rrs_patch_graph_t *graph,const char *from,const char *out,
                       const char *to,const char *in);
bool rrs_patch_disconnect(rrs_patch_graph_t *graph,const char *from,const char *out,
                          const char *to,const char *in);
size_t rrs_patch_node_count(const rrs_patch_graph_t *graph);
size_t rrs_patch_link_count(const rrs_patch_graph_t *graph);
const rrs_patch_node_t *rrs_patch_node_at(const rrs_patch_graph_t *graph,size_t index);
const rrs_patch_node_t *rrs_patch_node_find(const rrs_patch_graph_t *graph,const char *id);
const rrs_patch_link_t *rrs_patch_link_at(const rrs_patch_graph_t *graph,size_t index);
#endif
