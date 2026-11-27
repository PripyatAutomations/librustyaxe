/* SPDX-License-Identifier: MIT */
#include <librustyui/tree.h>
#include <glib.h>
#include <string.h>
typedef struct node {
   char *id,*parent,*label;
   uint32_t color;
   unsigned depth;
   GPtrArray *children;
} node_t;
struct rrui_tree {
   GHashTable *nodes,*expanded;
   GPtrArray *roots,*visible;
   char *selected;
   bool dirty;
};
static void node_free(gpointer data) {
   node_t *n=data;
   g_free(n->id);g_free(n->parent);g_free(n->label);g_ptr_array_unref(n->children);g_free(n);
}
rrui_tree_t *rrui_tree_new(void) {
   rrui_tree_t *t=g_new0(rrui_tree_t,1);
   t->nodes=g_hash_table_new_full(g_str_hash,g_str_equal,NULL,node_free);
   t->expanded=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
   t->roots=g_ptr_array_new();t->visible=g_ptr_array_new();return t;
}
void rrui_tree_free(rrui_tree_t *t) {
   if (!t) { return; }
   g_hash_table_unref(t->nodes);g_hash_table_unref(t->expanded);
   g_ptr_array_unref(t->roots);g_ptr_array_unref(t->visible);g_free(t->selected);g_free(t);
}
void rrui_tree_clear(rrui_tree_t *t) {
   if (!t) { return; }
   g_ptr_array_set_size(t->roots,0);g_ptr_array_set_size(t->visible,0);
   g_hash_table_remove_all(t->nodes);t->dirty=true;
}
static bool open_node(rrui_tree_t *t,node_t *n) {
   return GPOINTER_TO_INT(g_hash_table_lookup(t->expanded,n->id))==2;
}
bool rrui_tree_add(rrui_tree_t *t,const char *id,const char *parent,const char *label,uint32_t color,bool expanded) {
   if (!t || !id || !*id || !label || g_hash_table_contains(t->nodes,id)) { return false; }
   node_t *p=parent&&*parent?g_hash_table_lookup(t->nodes,parent):NULL;
   if ((parent&&*parent&&!p) || (p&&p->depth>=63)) { return false; }
   node_t *n=g_new0(node_t,1);n->id=g_strdup(id);n->parent=g_strdup(p?p->id:"");
   n->label=g_strdup(label);n->color=color;n->depth=p?p->depth+1:0;n->children=g_ptr_array_new();
   g_hash_table_insert(t->nodes,n->id,n);
   g_ptr_array_add(p?p->children:t->roots,n);
   if (!g_hash_table_contains(t->expanded,id)) {
      g_hash_table_insert(t->expanded,g_strdup(id),GINT_TO_POINTER(expanded?2:1));
   }
   t->dirty=true;return true;
}
static void flatten(rrui_tree_t *t,GPtrArray *nodes) {
   for (unsigned i=0;i<nodes->len;i++) {
      node_t *n=g_ptr_array_index(nodes,i);g_ptr_array_add(t->visible,n);
      if (open_node(t,n)) { flatten(t,n->children); }
   }
}
static void rebuild(rrui_tree_t *t) {
   if (!t || !t->dirty) { return; }
   t->dirty=false;g_ptr_array_set_size(t->visible,0);flatten(t,t->roots);
   if (!t->selected && t->visible->len) { t->selected=g_strdup(((node_t *)g_ptr_array_index(t->visible,0))->id); }
}
size_t rrui_tree_count(rrui_tree_t *t) {
   if (!t) { return 0; }
   rebuild(t);return t->visible->len;
}
bool rrui_tree_row(rrui_tree_t *t,size_t i,rrui_tree_row_t *out) {
   if (!out || i>=rrui_tree_count(t)) { return false; }
   node_t *n=g_ptr_array_index(t->visible,i);
   *out=(rrui_tree_row_t){n->id,n->parent,n->label,n->color,n->depth,n->children->len>0,open_node(t,n)};
   return true;
}
const char *rrui_tree_selected(rrui_tree_t *t) {
   if (!t) { return NULL; }
   rebuild(t);
   if (t->selected && !g_hash_table_contains(t->nodes,t->selected) && t->visible->len) {
      g_free(t->selected);t->selected=g_strdup(((node_t *)g_ptr_array_index(t->visible,0))->id);
   }
   return t->selected;
}
bool rrui_tree_select(rrui_tree_t *t,const char *id) {
   if (!t || !id || !g_hash_table_contains(t->nodes,id)) { return false; }
   char *copy=g_strdup(id);g_free(t->selected);t->selected=copy;return true;
}
bool rrui_tree_toggle(rrui_tree_t *t,const char *id) {
   node_t *n=t&&id?g_hash_table_lookup(t->nodes,id):NULL;
   if (!n || !n->children->len) { return false; }
   bool was=open_node(t,n);
   g_hash_table_replace(t->expanded,g_strdup(id),GINT_TO_POINTER(was?1:2));
   if (was && t->selected) {
      node_t *s=g_hash_table_lookup(t->nodes,t->selected);
      while (s && *s->parent) {
         if (!strcmp(s->parent,id)) { rrui_tree_select(t,id);break; }
         s=g_hash_table_lookup(t->nodes,s->parent);
      }
   }
   t->dirty=true;return true;
}
bool rrui_tree_move(rrui_tree_t *t,int direction) {
   size_t count=rrui_tree_count(t);
   if (!count || !direction) { return false; }
   const char *selected=rrui_tree_selected(t);
   for (size_t i=0;i<count;i++) {
      node_t *n=g_ptr_array_index(t->visible,i);
      if (selected && !strcmp(n->id,selected)) {
         size_t next=direction<0?(i?i-1:0):MIN(i+1,count-1);
         return rrui_tree_select(t,((node_t *)g_ptr_array_index(t->visible,next))->id);
      }
   }
   return rrui_tree_select(t,((node_t *)g_ptr_array_index(t->visible,0))->id);
}
bool rrui_tree_horizontal(rrui_tree_t *t,int direction) {
   const char *id=rrui_tree_selected(t);
   node_t *n=id?g_hash_table_lookup(t->nodes,id):NULL;
   if (!n || !direction) { return false; }
   if (direction<0) {
      if (n->children->len && open_node(t,n)) { return rrui_tree_toggle(t,id); }
      return *n->parent && rrui_tree_select(t,n->parent);
   }
   if (!n->children->len) { return false; }
   if (!open_node(t,n)) { return rrui_tree_toggle(t,id); }
   return rrui_tree_select(t,((node_t *)g_ptr_array_index(n->children,0))->id);
}
