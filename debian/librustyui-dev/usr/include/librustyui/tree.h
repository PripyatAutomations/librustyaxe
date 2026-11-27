/* SPDX-License-Identifier: MIT */
#ifndef RUSTYUI_TREE_H
#define RUSTYUI_TREE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct rrui_tree rrui_tree_t;
typedef struct {
   const char *id,*parent,*label;
   uint32_t color;
   unsigned depth;
   bool branch,expanded;
} rrui_tree_row_t;
rrui_tree_t *rrui_tree_new(void);
void rrui_tree_free(rrui_tree_t *);
/* Clear rows but retain expansion and selection for snapshot replacement. */
void rrui_tree_clear(rrui_tree_t *);
bool rrui_tree_add(rrui_tree_t *,const char *id,const char *parent,const char *label,uint32_t color,bool expanded);
size_t rrui_tree_count(rrui_tree_t *);
bool rrui_tree_row(rrui_tree_t *,size_t,rrui_tree_row_t *);
const char *rrui_tree_selected(rrui_tree_t *);
bool rrui_tree_select(rrui_tree_t *,const char *id);
bool rrui_tree_toggle(rrui_tree_t *,const char *id);
/* Vertical movement and conventional left/right collapse/expand/navigation. */
bool rrui_tree_move(rrui_tree_t *,int direction);
bool rrui_tree_horizontal(rrui_tree_t *,int direction);
#endif
