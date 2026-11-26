/* SPDX-License-Identifier: MIT */
#include <librustyui/ui.h>
#include <runtime.h>
#include <glib.h>
#include <string.h>
#include <stdio.h>

#define RRUI_MAX_WIDGETS 1024
#define RRUI_MAX_TEXT 16384

struct rrui_layout {
   rrui_backend_t backend;
   GPtrArray *widgets;
   char error[256];
};

static void widget_free(gpointer data) {
   rrui_widget_t *w = data;
   g_free((void *)w->id);
   g_free((void *)w->parent);
   g_free((void *)w->label);
   g_free((void *)w->action);
   g_free((void *)w->value);
   g_free(w);
}

rrui_layout_t *rrui_layout_new(rrui_backend_t backend) {
   if (backend != RRUI_TUI && backend != RRUI_GTK3)
      return NULL;
   rrui_layout_t *l = g_new0(rrui_layout_t, 1);
   l->backend = backend;
   l->widgets = g_ptr_array_new_with_free_func(widget_free);
   return l;
}
void rrui_layout_free(rrui_layout_t *l) {
   if (!l) return;
   g_ptr_array_unref(l->widgets);
   g_free(l);
}
size_t rrui_layout_count(const rrui_layout_t *l) {
   return l ? l->widgets->len : 0;
}
const rrui_widget_t *rrui_layout_at(const rrui_layout_t *l, size_t i) {
   return l && i < l->widgets->len ? g_ptr_array_index(l->widgets, i) : NULL;
}
const rrui_widget_t *rrui_layout_find(const rrui_layout_t *l, const char *id) {
   if (!id) return NULL;
   for (size_t i = 0; i < rrui_layout_count(l); i++) {
      const rrui_widget_t *w = rrui_layout_at(l, i);
      if (!strcmp(w->id, id)) return w;
   }
   return NULL;
}
const char *rrui_layout_error(const rrui_layout_t *l) {
   return l ? l->error : "No layout";
}
bool rrui_capability(rrui_backend_t backend, const char *name) {
   if (!name || (backend != RRUI_TUI && backend != RRUI_GTK3)) return false;
   if (!strcmp(name, "tui")) return backend == RRUI_TUI;
   if (!strcmp(name, "gtk3") || !strcmp(name, "graphical"))
      return backend == RRUI_GTK3;
   if ((!strcmp(name, "stepper") || !strcmp(name, "slider"))) return backend == RRUI_TUI;
   /* Specialized widgets are not advertised until actually implemented. */
   return false;
}

static rrs_value_t *fail(rrs_vm_t *vm, const char *message) {
   rrs_vm_raise(vm, message);
   return NULL;
}
static bool read_string(const rrs_value_t *d, const char *key, const char **out) {
   const rrs_value_t *v = rrs_value_field(d, key);
   if (!v) { *out = ""; return true; }
   if (rrs_value_type(v) != RRS_STRING) return false;
   *out = rrs_value_string(v);
   return *out && strlen(*out) <= RRUI_MAX_TEXT && g_utf8_validate(*out, -1, NULL);
}
static bool read_uint(const rrs_value_t *d, const char *key, unsigned *out,
                      unsigned fallback, unsigned max) {
   const rrs_value_t *v = rrs_value_field(d, key);
   if (!v) { *out = fallback; return true; }
   if (rrs_value_type(v) != RRS_INT) return false;
   int64_t n = rrs_value_int(v);
   if (n < 0 || n > max) return false;
   *out = (unsigned)n;
   return true;
}
static bool read_int(const rrs_value_t *d,const char *key,int *out,int fallback) {
   const rrs_value_t *v=rrs_value_field(d,key);
   if(!v){*out=fallback;return true;}
   if(rrs_value_type(v)!=RRS_INT)return false;
   int64_t n=rrs_value_int(v);
   if(n<INT32_MIN||n>INT32_MAX)return false;
   *out=(int)n;return true;
}
static bool read_bool(const rrs_value_t *d, const char *key, bool *out, bool fallback) {
   const rrs_value_t *v = rrs_value_field(d, key);
   if (!v) { *out = fallback; return true; }
   if (rrs_value_type(v) != RRS_BOOL) return false;
   *out = rrs_value_bool(v);
   return true;
}
static bool valid_id(const char *id) {
   if (!id || !*id || strlen(id) > 63) return false;
   for (; *id; id++)
      if (!g_ascii_isalnum(*id) && *id != '_' && *id != '-' && *id != '.') return false;
   return true;
}
static bool container(rrui_kind_t kind) {
   return kind == RRUI_DIALOG || kind == RRUI_PANEL ||
          kind == RRUI_ROW || kind == RRUI_COLUMN;
}
static rrs_value_t *native_add(rrs_vm_t *vm, int argc,
                               const rrs_value_t *const argv[], void *user) {
   rrui_layout_t *l = user;
   static const char *const kinds[] = {
      "dialog", "panel", "row", "column", "label", "entry", "checkbox", "button", "list", "stepper", "slider"
   };
   if (argc != 1 || rrs_value_type(argv[0]) != RRS_DICT)
      return fail(vm, "ui.add requires a widget dictionary");
   if (l->widgets->len >= RRUI_MAX_WIDGETS) return fail(vm, "Too many widgets");
   const rrs_value_t *d = argv[0];
   const char *kind, *id, *parent, *label, *action, *value;
   if (!read_string(d,"kind",&kind) || !read_string(d,"id",&id) ||
       !read_string(d,"parent",&parent) || !read_string(d,"label",&label) ||
       !read_string(d,"action",&action) || !read_string(d,"value",&value))
      return fail(vm, "Invalid widget string property");
   if (!valid_id(id) || rrui_layout_find(l,id))
      return fail(vm, "Invalid or duplicate widget ID");
   size_t k = 0;
   while (k < G_N_ELEMENTS(kinds) && strcmp(kinds[k],kind)) k++;
   if (k == G_N_ELEMENTS(kinds)) return fail(vm, "Unknown widget kind");
   if (*parent) {
      const rrui_widget_t *p = rrui_layout_find(l,parent);
      if (!p || !container(p->kind) || k == RRUI_DIALOG)
         return fail(vm, "Invalid widget parent");
   } else if (k != RRUI_DIALOG && k != RRUI_PANEL)
      return fail(vm, "A root must be a dialog or panel");
   rrui_widget_t w = { .kind = (rrui_kind_t)k };
   if (!read_uint(d,"width_chars",&w.width_chars,0,4096) ||
       !read_uint(d,"height_rows",&w.height_rows,0,4096) ||
       !read_uint(d,"max_length",&w.max_length,180,RRUI_MAX_TEXT) ||
       !read_uint(d,"spacing",&w.spacing,1,64) ||
       !read_bool(d,"visible",&w.visible,true) ||
       !read_bool(d,"enabled",&w.enabled,true) ||
       !read_bool(d,"checked",&w.checked,false) ||
       !read_bool(d,"expand_x",&w.expand_x,false) ||
       !read_bool(d,"expand_y",&w.expand_y,false) ||
       !read_bool(d,"masked",&w.masked,false))
      return fail(vm, "Invalid widget property type or range");
   if (k == RRUI_ENTRY && g_utf8_strlen(value,-1) > (glong)w.max_length)
      return fail(vm, "Entry value exceeds max_length");
   if (k == RRUI_STEPPER || k == RRUI_SLIDER) {
      if (!read_int(d,"min",&w.minimum,0)||!read_int(d,"max",&w.maximum,100)||
          !read_int(d,"step",&w.step,1)||w.minimum>w.maximum||w.step<=0)
         return fail(vm,"Invalid stepper range");
      const char *orientation;
      if (!read_string(d,"orientation",&orientation)||(*orientation&&strcmp(orientation,"horizontal")&&strcmp(orientation,"vertical")))
         return fail(vm,"Invalid slider orientation");
      w.vertical=!strcmp(orientation,"vertical");
      if (*value) {
         char *end=NULL;gint64 n=g_ascii_strtoll(value,&end,10);
         if (!end||*end||n<w.minimum||n>w.maximum)
            return fail(vm,"Invalid stepper value");
      }
   }
   w.id = g_strdup(id); w.parent = g_strdup(parent); w.label = g_strdup(label);
   w.action = g_strdup(action); w.value = g_strdup(value);
   rrui_widget_t *owned = g_new(rrui_widget_t,1);
   *owned = w;
   g_ptr_array_add(l->widgets,owned);
   return rrs_string(id);
}
static rrs_value_t *native_capability(rrs_vm_t *vm, int argc,
                                      const rrs_value_t *const argv[], void *user) {
   rrui_layout_t *l = user;
   if (argc != 1 || rrs_value_type(argv[0]) != RRS_STRING)
      return fail(vm, "ui.capability requires a string");
   return rrs_boolean(rrui_capability(l->backend,rrs_value_string(argv[0])));
}
bool rrui_layout_load(rrui_layout_t *l, const char *source) {
   if (!l || !source) return false;
   rrui_layout_t *stage = rrui_layout_new(l->backend);
   rrs_vm_t *vm = rrs_vm_new();
   rrs_value_t *result = NULL;
   bool ok = vm && rrs_vm_register_native(vm,"ui.add",native_add,stage) &&
      rrs_vm_register_native(vm,"ui.capability",native_capability,stage) &&
      rrs_vm_eval(vm,source,&result);
   if (ok && !stage->widgets->len) {
      ok = false;
      g_strlcpy(l->error,"Layout has no widgets",sizeof(l->error));
   } else if (!ok) {
      unsigned line = 0, col = 0;
      if (vm) rrs_vm_position(vm,&line,&col);
      snprintf(l->error,sizeof(l->error),"%u:%u: %s",line,col,
               vm && rrs_vm_error(vm) ? rrs_vm_error(vm) : "Layout VM failed");
   } else {
      GPtrArray *old = l->widgets;
      l->widgets = stage->widgets;
      stage->widgets = old;
      l->error[0] = 0;
   }
   rrs_value_free(result);
   if (vm) rrs_vm_free(vm);
   rrui_layout_free(stage);
   return ok;
}
