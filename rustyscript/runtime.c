/* Small, strict RustyScript interpreter foundation. SPDX-License-Identifier: MIT */
#include "runtime.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct array_item {
   rrs_value_t *value;
   struct array_item *next;
} array_item_t;
typedef struct field {
   char *name;
   rrs_value_t *value;
   struct field *next;
} field_t;
struct rrs_value {
   rrs_type_t type;
   union { int64_t integer; double real; bool boolean; char character;
           char *string; field_t *fields; struct {array_item_t *head,*tail;size_t length;} array; } as;
};
typedef struct native {
   char *name;
   rrs_native_fn call;
   void *user;
   struct native *next;
} native_t;
typedef struct function_def {
   char *name,*body,*signature,*return_type;
   unsigned line;
} function_def_t;
typedef struct binding {
   rrscript_binding_t info;
   char *name;
   char *body;
   unsigned body_line;
} binding_t;
struct rrs_vm {
   native_t *native;
   binding_t *bindings;
   size_t count;
   function_def_t *functions;
   size_t function_count;
   unsigned call_depth;
   size_t live_cells,peak_cells,cell_allocations,cell_releases,reference_bindings,gc_collections;
   size_t peak_bytes;
   struct value_cell *cells;
   char error[512];
   const char *active_body;
   size_t active_offset;
   unsigned active_line;
   const char *loading_source;
   struct variable *console_vars;
};
typedef struct value_cell {
   rrs_value_t *value;
   size_t owners;
   rrs_vm_t *vm;
   struct value_cell *next;
} value_cell_t;
typedef struct variable {
   char *name;
   struct value_cell *cell;
   bool read_only;
   struct variable *next;
} variable_t;
typedef struct {
   const char *text;
   size_t pos;
   unsigned line;
   rrs_vm_t *vm;
   variable_t *vars;
   bool returning;
   bool in_function;
   bool property_separators;
   rrs_value_t *return_value;
   int depth;
} eval_t;

static rrs_value_t *val(rrs_type_t type) {
   rrs_value_t *v=calloc(1,sizeof(*v));
   if(v) v->type=type;
   return v;
}
rrs_value_t *rrs_null(void) { return val(RRS_NULL); }
rrs_value_t *rrs_integer(int64_t n) { rrs_value_t *v=val(RRS_INT); if(v)v->as.integer=n; return v; }
rrs_value_t *rrs_floating(double n) { rrs_value_t *v=val(RRS_FLOAT); if(v)v->as.real=n; return v; }
rrs_value_t *rrs_boolean(bool n) { rrs_value_t *v=val(RRS_BOOL); if(v)v->as.boolean=n; return v; }
rrs_value_t *rrs_string(const char *s) {
   rrs_value_t *v=val(RRS_STRING);
   if(v && !(v->as.string=strdup(s?s:""))) {free(v);return NULL;}
   return v;
}
rrs_value_t *rrs_dictionary(void) {return val(RRS_DICT);}
rrs_value_t *rrs_array(void) {return val(RRS_ARRAY);}
bool rrs_array_append(rrs_value_t *array,const rrs_value_t *value) {
   if(!array||array->type!=RRS_ARRAY)return false;
   array_item_t *item=calloc(1,sizeof(*item));
   if(!item)return false;
   item->value=rrs_value_copy(value);
   if(!item->value){free(item);return false;}
   if(array->as.array.tail)array->as.array.tail->next=item;
   else array->as.array.head=item;
   array->as.array.tail=item;array->as.array.length++;
   return true;
}
size_t rrs_array_length(const rrs_value_t *v) {
   return v&&v->type==RRS_ARRAY?v->as.array.length:0;
}
const rrs_value_t *rrs_array_get(const rrs_value_t *v,size_t index) {
   if(!v||v->type!=RRS_ARRAY||index>=v->as.array.length)return NULL;
   array_item_t *item=v->as.array.head;
   while(index--)item=item->next;
   return item->value;
}
rrs_type_t rrs_value_type(const rrs_value_t *v) {return v?v->type:RRS_NULL;}
int64_t rrs_value_int(const rrs_value_t *v) {return v&&v->type==RRS_INT?v->as.integer:0;}
double rrs_value_float(const rrs_value_t *v) {return v&&v->type==RRS_FLOAT?v->as.real:0;}
bool rrs_value_bool(const rrs_value_t *v) {
   if(!v)return false;
   switch(v->type) {
      case RRS_BOOL:return v->as.boolean;
      case RRS_INT:return v->as.integer!=0;
      case RRS_FLOAT:return v->as.real!=0;
      case RRS_STRING:return *v->as.string!=0;
      case RRS_NULL:return false;
      default:return true;
   }
}
const char *rrs_value_string(const rrs_value_t *v) {
   return v&&v->type==RRS_STRING?v->as.string:NULL;
}
void rrs_value_free(rrs_value_t *v) {
   if(!v)return;
   if(v->type==RRS_STRING)free(v->as.string);
   if(v->type==RRS_ARRAY) {
      array_item_t *item=v->as.array.head;
      while(item) {array_item_t *next=item->next;rrs_value_free(item->value);free(item);item=next;}
   }
   if(v->type==RRS_DICT) {
      field_t *f=v->as.fields;
      while(f) {field_t *next=f->next;free(f->name);rrs_value_free(f->value);free(f);f=next;}
   }
   free(v);
}
const rrs_value_t *rrs_value_field(const rrs_value_t *v,const char *name) {
   if(!v||v->type!=RRS_DICT||!name)return NULL;
   for(field_t *f=v->as.fields;f;f=f->next)if(!strcmp(f->name,name))return f->value;
   return NULL;
}
rrs_value_t *rrs_value_copy(const rrs_value_t *v) {
   if(!v)return rrs_null();
   rrs_value_t *c=val(v->type);
   if(!c)return NULL;
   if(v->type==RRS_STRING) {
      c->as.string=strdup(v->as.string);
      if(!c->as.string){free(c);return NULL;}
   } else if(v->type==RRS_ARRAY) {
      for(array_item_t *item=v->as.array.head;item;item=item->next)
         if(!rrs_array_append(c,item->value)){rrs_value_free(c);return NULL;}
   } else if(v->type==RRS_DICT) {
      for(field_t *f=v->as.fields;f;f=f->next)
         if(!rrs_dict_put(c,f->name,f->value)){rrs_value_free(c);return NULL;}
   } else c->as=v->as;
   return c;
}
bool rrs_dict_put(rrs_value_t *v,const char *key,const rrs_value_t *value) {
   if(!v||v->type!=RRS_DICT||!key)return false;
   rrs_value_t *copy=rrs_value_copy(value);
   if(!copy)return false;
   for(field_t *f=v->as.fields;f;f=f->next) {
      if(!strcmp(f->name,key)){rrs_value_free(f->value);f->value=copy;return true;}
   }
   field_t *f=calloc(1,sizeof(*f));
   if(!f){rrs_value_free(copy);return false;}
   f->name=strdup(key);
   if(!f->name){free(f);rrs_value_free(copy);return false;}
   f->value=copy;f->next=v->as.fields;v->as.fields=f;
   return true;
}
/* Recursive, bounded representation of values for the interactive console. */
typedef struct {char *data;size_t length,capacity;} repr_buf_t;
static bool repr_add(repr_buf_t *b,const char *text,size_t n) {
   if(n>65536||b->length>65536-n)return false;
   size_t needed=b->length+n+1;
   if(needed>b->capacity) {
      size_t capacity=b->capacity?b->capacity:64;
      while(capacity<needed)capacity*=2;
      char *new_data=realloc(b->data,capacity);
      if(!new_data)return false;
      b->data=new_data;b->capacity=capacity;
   }
   memcpy(b->data+b->length,text,n);
   b->length+=n;b->data[b->length]=0;
   return true;
}
static bool repr_str(repr_buf_t *b,const char *s) {
   return repr_add(b,s,strlen(s));
}
static bool repr_quote(repr_buf_t *b,const char *text) {
   if(!repr_str(b,"\""))return false;
   for(const unsigned char *p=(const unsigned char*)(text?text:"");*p;p++) {
      char buf[8];
      if(*p=='"'||*p=='\\') {
         buf[0]='\\';buf[1]=(char)*p;buf[2]=0;
      } else if(*p=='\n')strcpy(buf,"\\n");
      else if(*p=='\r')strcpy(buf,"\\r");
      else if(*p=='\t')strcpy(buf,"\\t");
      else if(*p<32)snprintf(buf,sizeof(buf),"\\u%04x",*p);
      else {buf[0]=(char)*p;buf[1]=0;}
      if(!repr_str(b,buf))return false;
   }
   return repr_str(b,"\"");
}
static bool repr_inner(repr_buf_t *b,const rrs_value_t *v,int depth) {
   if(depth>12)return repr_str(b,"\"<depth limit>\"");
   if(!v||v->type==RRS_NULL)return repr_str(b,"null");
   char number[96];
   switch(v->type) {
      case RRS_BOOL:return repr_str(b,v->as.boolean?"true":"false");
      case RRS_INT:
         snprintf(number,sizeof(number),"%lld",(long long)v->as.integer);
         return repr_str(b,number);
      case RRS_FLOAT:
         snprintf(number,sizeof(number),"%.17g",v->as.real);
         return repr_str(b,number);
      case RRS_CHAR:{
         char chars[2]={v->as.character,0};
         return repr_quote(b,chars);
      }
      case RRS_STRING:return repr_quote(b,v->as.string);
      case RRS_ARRAY:
         if(!repr_str(b,"["))return false;
         for(array_item_t *item=v->as.array.head;item;item=item->next) {
            if(item!=v->as.array.head&&!repr_str(b,", "))return false;
            if(!repr_inner(b,item->value,depth+1))return false;
         }
         return repr_str(b,"]");
      case RRS_DICT:
         if(!repr_str(b,"{"))return false;
         for(field_t *item=v->as.fields;item;item=item->next) {
            if(item!=v->as.fields&&!repr_str(b,", "))return false;
            if(!repr_quote(b,item->name)||!repr_str(b,": ")||
               !repr_inner(b,item->value,depth+1))return false;
         }
         return repr_str(b,"}");
      default:return repr_str(b,"null");
   }
}
char *rrs_value_repr(const rrs_value_t *value) {
   repr_buf_t output={0};
   if(!repr_inner(&output,value,0)) {
      free(output.data);
      return strdup("<value too large>");
   }
   return output.data?output.data:strdup("null");
}

static size_t managed_value_bytes(const rrs_value_t *v,int depth) {
   if(!v||depth>64)return 0;
   size_t bytes=sizeof(*v);
   if(v->type==RRS_STRING)bytes+=strlen(v->as.string)+1;
   else if(v->type==RRS_ARRAY)
      for(array_item_t *a=v->as.array.head;a;a=a->next)
         bytes+=sizeof(*a)+managed_value_bytes(a->value,depth+1);
   else if(v->type==RRS_DICT)
      for(field_t *f=v->as.fields;f;f=f->next)
         bytes+=sizeof(*f)+strlen(f->name)+1+managed_value_bytes(f->value,depth+1);
   return bytes;
}
static size_t managed_bytes(const rrs_vm_t *vm) {
   if(!vm)return 0;
   size_t bytes=0;
   for(const value_cell_t *c=vm->cells;c;c=c->next)
      bytes+=sizeof(*c)+managed_value_bytes(c->value,0);
   return bytes;
}
static void update_peak_bytes(rrs_vm_t *vm) {
   size_t bytes=managed_bytes(vm);
   if(bytes>vm->peak_bytes)vm->peak_bytes=bytes;
}
rrs_vm_t *rrs_vm_new(void) {return calloc(1,sizeof(rrs_vm_t));}
void rrs_vm_memory_stats(const rrs_vm_t *vm,rrs_memory_stats_t *stats) {
   if(!stats)return;
   memset(stats,0,sizeof(*stats));
   if(!vm)return;
   stats->used_bytes=managed_bytes(vm);
   stats->peak_bytes=vm->peak_bytes;
   stats->live_cells=vm->live_cells;
   stats->peak_cells=vm->peak_cells;
   stats->cell_allocations=vm->cell_allocations;
   stats->cell_releases=vm->cell_releases;
   stats->reference_bindings=vm->reference_bindings;
   stats->collection_requests=vm->gc_collections;
}
size_t rrs_vm_gc_collect(rrs_vm_t *vm) {
   if(!vm)return 0;
   vm->gc_collections++;
   /* Variable cells cannot contain references to other cells yet, so
    * reference counting reclaims every unreachable cell immediately. */
   return 0;
}

static void free_functions(function_def_t *f,size_t n) {
   for(size_t i=0;i<n;i++){free(f[i].name);free(f[i].body);free(f[i].signature);free(f[i].return_type);}
   free(f);
}
static void free_bindings(binding_t *b,size_t n) {
   for(size_t i=0;i<n;i++){free(b[i].name);free(b[i].body);}
   free(b);
}
static void free_vars(variable_t *v);
void rrs_vm_free(rrs_vm_t *vm) {
   if(!vm)return;
   free_bindings(vm->bindings,vm->count);
   free_functions(vm->functions,vm->function_count);
   free_vars(vm->console_vars);
   native_t *n=vm->native;
   while(n){native_t *next=n->next;free(n->name);free(n);n=next;}
   free(vm);
}
bool rrs_vm_register_native(rrs_vm_t *vm,const char *name,rrs_native_fn fn,void *user) {
   if(!vm||!name||!*name||!fn)return false;
   for(native_t *n=vm->native;n;n=n->next)
      if(!strcmp(n->name,name)){n->call=fn;n->user=user;return true;}
   native_t *n=calloc(1,sizeof(*n));
   if(!n)return false;
   n->name=strdup(name);
   if(!n->name){free(n);return false;}
   n->call=fn;n->user=user;n->next=vm->native;vm->native=n;
   return true;
}
void rrs_vm_raise(rrs_vm_t *vm,const char *message) {
   /* Preserve the primary failure. Secondary parser unwinding errors such as
    * "expected )" must not conceal the actual missing property or bad native. */
   if(vm && !vm->error[0]) {
      if(vm->active_body) {
         unsigned column=1;
         for(size_t i=vm->active_offset;i>0 && vm->active_body[i-1]!='\n';i--)
            column++;
         snprintf(vm->error,sizeof(vm->error),"line %u:%u: %s",
            vm->active_line,column,message?message:"runtime error");
      } else snprintf(vm->error,sizeof(vm->error),"%s",message?message:"runtime error");
   }
}
const char *rrs_vm_error(const rrs_vm_t *vm) {return vm?vm->error:"invalid VM";}
void rrs_vm_position(const rrs_vm_t *vm,unsigned *line,unsigned *column) {
   if(line)*line=vm&&vm->active_body?vm->active_line:0;
   if(column) {
      unsigned col=1;
      if(vm&&vm->active_body) {
         for(size_t i=vm->active_offset;i>0 && vm->active_body[i-1]!='\n';i--)
            col++;
      } else col=0;
      *column=col;
   }
}

static int collect(const rrscript_binding_t *b,void *arg) {
   rrs_vm_t *v=arg;
   if(b->kind==RRSCRIPT_FUNCTION) {
      for(size_t i=0;i<v->function_count;i++)
         if(strlen(v->functions[i].name)==b->name_length &&
            !strncmp(v->functions[i].name,b->name,b->name_length))return -1;
      function_def_t *grown=realloc(v->functions,(v->function_count+1)*sizeof(*grown));
      if(!grown)return -1;
      v->functions=grown;
      function_def_t *fn=&v->functions[v->function_count];
      memset(fn,0,sizeof(*fn));
      fn->name=strndup(b->name,b->name_length);
      fn->body=strndup(b->body,b->body_length);
      fn->signature=strndup(b->params?b->params:"",b->params_length);
      fn->return_type=strndup(b->return_type,b->return_type_length);
      fn->line=b->line;
      if(!fn->name||!fn->body||!fn->signature||!fn->return_type)return -1;
      v->function_count++;
      return 0;
   }
   binding_t *grown=realloc(v->bindings,(v->count+1)*sizeof(*grown));
   if(!grown)return -1;
   v->bindings=grown;
   binding_t *dst=&v->bindings[v->count];
   memset(dst,0,sizeof(*dst));
   dst->name=strndup(b->name,b->name_length);
   dst->body=strndup(b->body,b->body_length);
   if(!dst->name||!dst->body){free(dst->name);free(dst->body);return -1;}
   dst->body_line=b->line;
   if(v->loading_source && b->body>=v->loading_source) {
      dst->body_line=1;
      for(const char *p=v->loading_source;p<b->body;p++)
         if(*p=='\n')dst->body_line++;
   }
   dst->info=*b;
   dst->info.name=dst->name;dst->info.body=dst->body;
   dst->info.name_length=strlen(dst->name);
   dst->info.body_length=strlen(dst->body);
   v->count++;
   return 0;
}
bool rrs_vm_load(rrs_vm_t *vm,const char *source) {
   if(!vm)return false;
   vm->error[0]=0;
   rrs_vm_t candidate={.loading_source=source};
   rrscript_error_t err;
   if(rrscript_parse_registrations(source,collect,&candidate,&err)) {
      free_bindings(candidate.bindings,candidate.count);
      free_functions(candidate.functions,candidate.function_count);
      snprintf(vm->error,sizeof(vm->error),"line %u:%u: %.150s",err.line,err.column,err.message);
      return false;
   }
   free_bindings(vm->bindings,vm->count);
   free_functions(vm->functions,vm->function_count);
   vm->bindings=candidate.bindings;vm->count=candidate.count;
   vm->functions=candidate.functions;vm->function_count=candidate.function_count;
   return true;
}
size_t rrs_vm_binding_count(const rrs_vm_t *vm){return vm?vm->count:0;}
const rrscript_binding_t *rrs_vm_binding(const rrs_vm_t *vm,size_t i) {
   return vm&&i<vm->count?&vm->bindings[i].info:NULL;
}
static void release_cell(value_cell_t *c) {
   if(!c)return;
   if(c->vm&&c->vm->reference_bindings)c->vm->reference_bindings--;
   if(--c->owners==0){
      if(c->vm&&c->vm->live_cells) {
         value_cell_t **link=&c->vm->cells;
         while(*link&&*link!=c)link=&(*link)->next;
         if(*link)*link=c->next;
         c->vm->live_cells--;c->vm->cell_releases++;
      }
      rrs_value_free(c->value);free(c);
   }
}
static value_cell_t *new_cell(rrs_vm_t *vm,const rrs_value_t *value) {
   value_cell_t *c=calloc(1,sizeof(*c));
   if(!c)return NULL;
   c->value=rrs_value_copy(value);
   if(!c->value){free(c);return NULL;}
   c->owners=1;c->vm=vm;
   c->next=vm->cells;vm->cells=c;
   vm->live_cells++;vm->cell_allocations++;vm->reference_bindings++;
   if(vm->live_cells>vm->peak_cells)vm->peak_cells=vm->live_cells;
   update_peak_bytes(vm);
   return c;
}
static void free_vars(variable_t *v) {
   while(v){variable_t *next=v->next;free(v->name);release_cell(v->cell);free(v);v=next;}
}
static variable_t *find_var(eval_t *e,const char *name) {
   for(variable_t *v=e->vars;v;v=v->next)if(!strcmp(v->name,name))return v;
   return NULL;
}
static bool set_var(eval_t *e,const char *name,const rrs_value_t *value,bool declare) {
   variable_t *v=find_var(e,name);
   if(declare&&v){rrs_vm_raise(e->vm,"duplicate declaration");return false;}
   if(!v&&!declare){rrs_vm_raise(e->vm,"assignment to undeclared variable");return false;}
   if(v&&v->read_only){rrs_vm_raise(e->vm,"assignment to const variable");return false;}
   rrs_value_t *copy=rrs_value_copy(value);
   if(!copy)return false;
   if(!v) {
      v=calloc(1,sizeof(*v));
      if(!v){rrs_value_free(copy);return false;}
      v->name=strdup(name);v->cell=new_cell(e->vm,value);
      if(!v->name||!v->cell){free(v->name);release_cell(v->cell);free(v);rrs_value_free(copy);return false;}
      v->next=e->vars;e->vars=v;
   }
   rrs_value_free(v->cell->value);v->cell->value=copy;
   update_peak_bytes(e->vm);
   return true;
}
static bool bind_var(eval_t *e,const char *name,const char *target,bool declare,bool read_only) {
   variable_t *src=find_var(e,target),*dst=find_var(e,name);
   if(!src){rrs_vm_raise(e->vm,"reference requires an existing variable");return false;}
   if(src->read_only&&!read_only){rrs_vm_raise(e->vm,"cannot bind mutable reference to const");return false;}
   if(declare&&dst){rrs_vm_raise(e->vm,"duplicate declaration");return false;}
   if(!declare&&!dst){rrs_vm_raise(e->vm,"reference assignment to undeclared variable");return false;}
   if(dst&&dst->read_only){rrs_vm_raise(e->vm,"cannot rebind const");return false;}
   if(!dst) {
      dst=calloc(1,sizeof(*dst));
      if(!dst)return false;
      dst->name=strdup(name);
      if(!dst->name){free(dst);return false;}
      dst->next=e->vars;e->vars=dst;
   }
   src->cell->owners++;
   e->vm->reference_bindings++;
   release_cell(dst->cell);
   dst->cell=src->cell;dst->read_only=read_only;
   return true;
}
static char ep(eval_t *e){return e->text[e->pos];}
static char advance(eval_t *e) {
   char c=ep(e);
   if(c){e->pos++;if(c=='\n')e->line++;}
   e->vm->active_offset=e->pos;
   e->vm->active_line=e->line;
   return c;
}
static void ws(eval_t *e) {
   for(;;) {
      while(isspace((unsigned char)ep(e))||(!e->property_separators&&ep(e)==';'))advance(e);
      if(ep(e)=='#'||(ep(e)=='/'&&e->text[e->pos+1]=='/')) {
         while(ep(e)&&ep(e)!='\n')advance(e);
      } else if(ep(e)=='/'&&e->text[e->pos+1]=='*') {
         advance(e);advance(e);
         while(ep(e)&&!(ep(e)=='*'&&e->text[e->pos+1]=='/'))advance(e);
         if(ep(e)){advance(e);advance(e);}
      } else break;
   }
}
static bool match(eval_t *e,char ch){ws(e);if(ep(e)==ch){advance(e);return true;}return false;}
static bool keyword(eval_t *e,const char *name) {
   ws(e);
   size_t n=strlen(name);
   if(strncmp(e->text+e->pos,name,n))return false;
   char c=e->text[e->pos+n];
   if(isalnum((unsigned char)c)||c=='_')return false;
   for(size_t i=0;i<n;i++)advance(e);
   return true;
}
static bool ident(eval_t *e,char *buf,size_t cap) {
   ws(e);
   if(!(isalpha((unsigned char)ep(e))||ep(e)=='_'))return false;
   size_t i=0;
   while(isalnum((unsigned char)ep(e))||ep(e)=='_') {
      if(i+1>=cap){rrs_vm_raise(e->vm,"identifier too long");return false;}
      buf[i++]=advance(e);
   }
   buf[i]=0;return true;
}
static rrs_value_t *expression(eval_t *e);
static bool statements(eval_t *e,bool stop_on_brace);
static rrs_value_t *call_script(eval_t *caller,function_def_t *fn,int argc,
                                 const rrs_value_t *const args[],variable_t *origins[]) {
   if(caller->vm->call_depth>=64){rrs_vm_raise(caller->vm,"maximum function call depth exceeded");return NULL;}
   eval_t signature={.text=fn->signature,.line=fn->line,.vm=caller->vm};
   eval_t execution={.text=fn->body,.line=fn->line,.vm=caller->vm,.in_function=true};
   int index=0;
   ws(&signature);
   while(ep(&signature)) {
      bool read_only=keyword(&signature,"const");
      char type[64],name[192];
      if(!ident(&signature,type,sizeof(type))){rrs_vm_raise(caller->vm,"expected parameter type");break;}
      ws(&signature);
      if(!strcmp(type,"void")&&index==0&&!ep(&signature))break;
      if(strcmp(type,"int")&&strcmp(type,"float")&&strcmp(type,"bool")&&
         strcmp(type,"char")&&strcmp(type,"dict")&&strcmp(type,"auto")) {
         rrs_vm_raise(caller->vm,"unknown parameter type");break;
      }
      for(int dim=0;dim<2&&match(&signature,'[');dim++) {
         if(!match(&signature,']')){rrs_vm_raise(caller->vm,"invalid array parameter");break;}
      }
      if(caller->vm->error[0])break;
      if(!ident(&signature,name,sizeof(name))){rrs_vm_raise(caller->vm,"expected parameter name");break;}
      for(int dim=0;dim<2&&match(&signature,'[');dim++) {
         if(!match(&signature,']')){rrs_vm_raise(caller->vm,"invalid array parameter");break;}
      }
      if(caller->vm->error[0])break;
      if(index>=argc){rrs_vm_raise(caller->vm,"too few function arguments");break;}
      variable_t *arg=calloc(1,sizeof(*arg));
      if(!arg){rrs_vm_raise(caller->vm,"parameter allocation failed");break;}
      arg->name=strdup(name);arg->read_only=read_only;
      if(origins[index]) {
         if(origins[index]->read_only&&!read_only) {
            free(arg->name);free(arg);
            rrs_vm_raise(caller->vm,"cannot pass const variable to mutable parameter");break;
         }
         arg->cell=origins[index]->cell;
         arg->cell->owners++;caller->vm->reference_bindings++;
      } else arg->cell=new_cell(caller->vm,args[index]);
      if(!arg->name||!arg->cell) {
         free(arg->name);release_cell(arg->cell);free(arg);
         rrs_vm_raise(caller->vm,"parameter allocation failed");break;
      }
      for(variable_t *v=execution.vars;v;v=v->next)
         if(!strcmp(v->name,name)){rrs_vm_raise(caller->vm,"duplicate parameter");break;}
      if(caller->vm->error[0]){free(arg->name);release_cell(arg->cell);free(arg);break;}
      arg->next=execution.vars;execution.vars=arg;
      index++;
      if(!match(&signature,',')) {
         ws(&signature);
         if(ep(&signature))rrs_vm_raise(caller->vm,"expected comma in parameters");
         break;
      }
   }
   if(!caller->vm->error[0]&&index!=argc)
      rrs_vm_raise(caller->vm,"wrong function argument count");
   const char *old_body=caller->vm->active_body;
   size_t old_offset=caller->vm->active_offset;
   unsigned old_line=caller->vm->active_line;
   if(!caller->vm->error[0]) {
      caller->vm->call_depth++;
      caller->vm->active_body=execution.text;
      caller->vm->active_offset=0;
      caller->vm->active_line=execution.line;
      statements(&execution,false);
      caller->vm->call_depth--;
      if(!caller->vm->error[0]&&!strcmp(fn->return_type,"void")&&
         execution.return_value&&execution.return_value->type!=RRS_NULL)
         rrs_vm_raise(caller->vm,"void function returned a value");
      if(!caller->vm->error[0]&&strcmp(fn->return_type,"void")&&
         !execution.returning)rrs_vm_raise(caller->vm,"function missing return");
   }
   caller->vm->active_body=old_body;
   caller->vm->active_offset=old_offset;
   caller->vm->active_line=old_line;
   free_vars(execution.vars);
   if(caller->vm->error[0]){rrs_value_free(execution.return_value);return NULL;}
   return execution.return_value?execution.return_value:rrs_null();
}
static bool memory_field(rrs_value_t *d,const char *name,size_t number) {
   rrs_value_t *v=rrs_integer((int64_t)number);
   if(!v)return false;
   bool ok=rrs_dict_put(d,name,v);
   rrs_value_free(v);
   return ok;
}
static rrs_value_t *memory_value(rrs_vm_t *vm) {
   rrs_value_t *d=rrs_dictionary(),*gc=rrs_dictionary();
   if(!d||!gc){rrs_value_free(d);rrs_value_free(gc);return NULL;}
   bool ok=memory_field(d,"used",managed_bytes(vm))&&
      memory_field(d,"peak",vm->peak_bytes)&&
      memory_field(d,"objects",vm->live_cells)&&
      memory_field(d,"live_cells",vm->live_cells)&&
      memory_field(d,"peak_cells",vm->peak_cells)&&
      memory_field(d,"allocations",vm->cell_allocations)&&
      memory_field(d,"releases",vm->cell_releases)&&
      memory_field(d,"references",vm->reference_bindings)&&
      memory_field(gc,"collections",0)&&
      memory_field(gc,"requests",vm->gc_collections)&&
      memory_field(gc,"reclaimed",vm->cell_releases);
   if(ok)ok=rrs_dict_put(d,"gc",gc);
   rrs_value_free(gc);
   if(!ok){rrs_value_free(d);return NULL;}
   return d;
}
static rrs_value_t *primary(eval_t *e) {
   ws(e);
   if(match(e,'(')) {
      rrs_value_t *v=expression(e);
      if(!match(e,')'))rrs_vm_raise(e->vm,"expected )");
      return v;
   }
   if(ep(e)=='"') {
      advance(e);
      size_t capacity=32,n=0;
      char *str=malloc(capacity);
      if(!str)return NULL;
      while(ep(e)&&ep(e)!='"') {
         char c=advance(e);
         if(c=='\\') {
            c=advance(e);
            if(!c){rrs_vm_raise(e->vm,"unterminated escape");break;}
            if(c=='n')c='\n';
            else if(c=='t')c='\t';
         }
         if(n+2>capacity) {
            capacity*=2;
            char *newstr=realloc(str,capacity);
            if(!newstr){free(str);return NULL;}
            str=newstr;
         }
         str[n++]=c;
      }
      if(!match(e,'"'))rrs_vm_raise(e->vm,"unterminated string");
      str[n]=0;
      rrs_value_t *v=rrs_string(str);free(str);return v;
   }
   if(ep(e)=='\'' ) {
      advance(e);char c=advance(e);
      if(!c||!match(e,'\'')){rrs_vm_raise(e->vm,"invalid character");return NULL;}
      char text[2]={c,0};return rrs_string(text);
   }
   if(isdigit((unsigned char)ep(e))) {
      const char *start=e->text+e->pos;
      char *end=NULL;
      errno=0;
      int64_t integer=strtoll(start,&end,0);
      bool floating=false;
      if(*end=='.'||*end=='e'||*end=='E')floating=true;
      if(floating) {
         double d=strtod(start,&end);
         while(e->text+e->pos<end)advance(e);
         return rrs_floating(d);
      }
      while(e->text+e->pos<end)advance(e);
      if(errno==ERANGE)rrs_vm_raise(e->vm,"integer literal out of range");
      return rrs_integer(integer);
   }
   if(match(e,'[')) {
      rrs_value_t *array=rrs_array();
      if(!array)return NULL;
      while(!match(e,']')) {
         if(rrs_array_length(array)>=256) {
            rrs_vm_raise(e->vm,"array has too many entries");break;
         }
         rrs_value_t *item=expression(e);
         if(!item){rrs_vm_raise(e->vm,"invalid array element");break;}
         bool ok=rrs_array_append(array,item);
         rrs_value_free(item);
         if(!ok){rrs_vm_raise(e->vm,"array allocation failed");break;}
         if(match(e,']'))return array;
         if(!match(e,',')){rrs_vm_raise(e->vm,"expected , or ]");break;}
      }
      if(!e->vm->error[0])return array;
      rrs_value_free(array);return NULL;
   }
   if(match(e,'{')) {
      rrs_value_t *d=rrs_dictionary();
      if(!d)return NULL;
      while(!match(e,'}')) {
         char key[128];
         ws(e);
         if(ep(e)=='"') {
            rrs_value_t *k=primary(e);
            const char *p=rrs_value_string(k);
            if(!p||strlen(p)>=sizeof(key)){rrs_value_free(k);rrs_vm_raise(e->vm,"invalid dictionary key");break;}
            strcpy(key,p);rrs_value_free(k);
         } else if(!ident(e,key,sizeof(key))) {rrs_vm_raise(e->vm,"expected dictionary key");break;}
         if(!match(e,':')){rrs_vm_raise(e->vm,"expected :");break;}
         rrs_value_t *v=expression(e);
         if(!v){rrs_vm_raise(e->vm,"invalid dictionary value");break;}
         bool ok=rrs_dict_put(d,key,v);rrs_value_free(v);
         if(!ok){rrs_vm_raise(e->vm,"dictionary insertion failed");break;}
         if(match(e,'}'))return d;
         if(!match(e,',')){rrs_vm_raise(e->vm,"expected , or }");break;}
      }
      if(!e->vm->error[0])return d;
      rrs_value_free(d);return NULL;
   }
   if(keyword(e,"true"))return rrs_boolean(true);
   if(keyword(e,"false"))return rrs_boolean(false);
   if(keyword(e,"null")||keyword(e,"NULL")||keyword(e,"nul")||keyword(e,"NUL"))return rrs_null();
   char name[192];
   if(!ident(e,name,sizeof(name))){rrs_vm_raise(e->vm,"expected expression");return NULL;}
   while(match(e,'.')) {
      size_t len=strlen(name);
      if(len+1>=sizeof(name)){rrs_vm_raise(e->vm,"name too long");return NULL;}
      name[len]='.';name[len+1]=0;
      if(!ident(e,name+len+1,sizeof(name)-len-1)){rrs_vm_raise(e->vm,"expected name after .");return NULL;}
   }
   if(match(e,'(')) {
      const rrs_value_t *args[16]={0};
      variable_t *origins[16]={0};
      int argc=0;
      while(!match(e,')')) {
         if(argc>=16){rrs_vm_raise(e->vm,"too many arguments");break;}
         eval_t probe=*e;
         char source_name[192];
         if(ident(&probe,source_name,sizeof(source_name))) {
            ws(&probe);
            if(ep(&probe)==','||ep(&probe)==')')
               origins[argc]=find_var(e,source_name);
         }
         args[argc]=expression(e);
         if(!args[argc])break;
         argc++;
         if(match(e,')'))break;
         if(!match(e,',')){rrs_vm_raise(e->vm,"expected , or )");break;}
      }
      rrs_value_t *result=NULL;
      if(!e->vm->error[0]) {
         function_def_t *fn=NULL;
         for(size_t i=0;i<e->vm->function_count;i++)
            if(!strcmp(e->vm->functions[i].name,name)){fn=&e->vm->functions[i];break;}
         if(fn)result=call_script(e,fn,argc,args,origins);
         else if(!strcmp(name,"runtime.memory")&&argc==0)
            result=memory_value(e->vm);
         else if(!strcmp(name,"runtime.gc.collect")&&argc==0)
            result=rrs_integer((int64_t)rrs_vm_gc_collect(e->vm));
         else {
            native_t *n=e->vm->native;
            while(n&&strcmp(n->name,name))n=n->next;
            if(!n)rrs_vm_raise(e->vm,"unknown function");
            else result=n->call(e->vm,argc,args,n->user);
         }
      }
      for(int i=0;i<argc;i++)rrs_value_free((rrs_value_t *)args[i]);
      if(e->vm->error[0]){rrs_value_free(result);return NULL;}
      return result?result:rrs_null();
   }
   variable_t *v=find_var(e,name);
   if(v)return rrs_value_copy(v->cell->value);
   /* Dot paths traverse nested objects first, then flatten from longest prefix. */
   for(size_t len=strlen(name);len>0;len--) {
      if(len!=strlen(name)&&name[len]!='.')continue;
      char prefix[192];
      memcpy(prefix,name,len);prefix[len]=0;
      variable_t *root=find_var(e,prefix);
      if(!root)continue;
      const rrs_value_t *value=root->cell->value;
      const char *tail=name+len;
      if(*tail=='.')tail++;
      while(*tail&&value) {
         const rrs_value_t *exact=rrs_value_field(value,tail);
         if(exact){value=exact;break;}
         const char *dot=strchr(tail,'.');
         if(!dot){value=rrs_value_field(value,tail);break;}
         char key[192];size_t n=(size_t)(dot-tail);
         if(n>=sizeof(key)){value=NULL;break;}
         memcpy(key,tail,n);key[n]=0;
         value=rrs_value_field(value,key);
         tail=dot+1;
      }
      if(value)return rrs_value_copy(value);
   }
   rrs_vm_raise(e->vm,"unknown variable");
   return NULL;
}
static bool numeric(const rrs_value_t *v) {
   return v&&(v->type==RRS_INT||v->type==RRS_FLOAT);
}
static double number(const rrs_value_t *v) {
   return v->type==RRS_FLOAT?v->as.real:(double)v->as.integer;
}
static rrs_value_t *binary(eval_t *e,rrs_value_t *lhs,rrs_value_t *rhs,const char *op) {
   if(!lhs||!rhs){rrs_value_free(lhs);rrs_value_free(rhs);return NULL;}
   rrs_value_t *res=NULL;
   if(!strcmp(op,"==")||!strcmp(op,"!=")) {
      bool eq=false;
      if(lhs->type==rhs->type) {
         switch(lhs->type) {
            case RRS_NULL:eq=true;break;
            case RRS_STRING:eq=!strcmp(lhs->as.string,rhs->as.string);break;
            case RRS_BOOL:eq=lhs->as.boolean==rhs->as.boolean;break;
            case RRS_INT:eq=lhs->as.integer==rhs->as.integer;break;
            case RRS_FLOAT:eq=lhs->as.real==rhs->as.real;break;
            case RRS_CHAR:eq=lhs->as.character==rhs->as.character;break;
            default:eq=lhs==rhs;break;
         }
      } else if(numeric(lhs)&&numeric(rhs))eq=number(lhs)==number(rhs);
      res=rrs_boolean(!strcmp(op,"==")?eq:!eq);
   } else if(!strcmp(op,"&&")||!strcmp(op,"||")) {
      res=rrs_boolean(!strcmp(op,"&&")?rrs_value_bool(lhs)&&rrs_value_bool(rhs):
                                       rrs_value_bool(lhs)||rrs_value_bool(rhs));
   } else if(numeric(lhs)&&numeric(rhs)) {
      double a=number(lhs),b=number(rhs);
      if(!strcmp(op,"<"))res=rrs_boolean(a<b);
      else if(!strcmp(op,">"))res=rrs_boolean(a>b);
      else if(!strcmp(op,"<="))res=rrs_boolean(a<=b);
      else if(!strcmp(op,">="))res=rrs_boolean(a>=b);
      else if(!strcmp(op,"/")&&b==0)rrs_vm_raise(e->vm,"division by zero");
      else if(!strcmp(op,"+"))res=lhs->type==RRS_INT&&rhs->type==RRS_INT?
         rrs_integer(lhs->as.integer+rhs->as.integer):rrs_floating(a+b);
      else if(!strcmp(op,"-"))res=lhs->type==RRS_INT&&rhs->type==RRS_INT?
         rrs_integer(lhs->as.integer-rhs->as.integer):rrs_floating(a-b);
      else if(!strcmp(op,"*"))res=lhs->type==RRS_INT&&rhs->type==RRS_INT?
         rrs_integer(lhs->as.integer*rhs->as.integer):rrs_floating(a*b);
      else if(!strcmp(op,"/"))res=rrs_floating(a/b);
   } else if(!strcmp(op,"+")&&lhs->type==RRS_STRING&&rhs->type==RRS_STRING) {
      size_t n=strlen(lhs->as.string),m=strlen(rhs->as.string);
      char *s=malloc(n+m+1);
      if(s){memcpy(s,lhs->as.string,n);memcpy(s+n,rhs->as.string,m+1);res=rrs_string(s);free(s);}
   }
   if(!res&&!e->vm->error[0])rrs_vm_raise(e->vm,"invalid operand types");
   rrs_value_free(lhs);rrs_value_free(rhs);
   return res;
}
static const char *operators[]={"||","&&","==","!=","<=",">=","<",">","+","-","*","/",NULL};
static int precedence(const char *op) {
   if(!strcmp(op,"||"))return 1;
   if(!strcmp(op,"&&"))return 2;
   if(!strcmp(op,"==")||!strcmp(op,"!="))return 3;
   if(!strcmp(op,"<")||!strcmp(op,">")||!strcmp(op,"<=")||!strcmp(op,">="))return 4;
   if(!strcmp(op,"+")||!strcmp(op,"-"))return 5;
   return 6;
}
static rrs_value_t *expr_prec(eval_t *e,int min) {
   ws(e);
   rrs_value_t *lhs=NULL;
   if(ep(e)=='!'&&e->text[e->pos+1]!='=') {
      advance(e);rrs_value_t *v=expr_prec(e,7);
      if(v){lhs=rrs_boolean(!rrs_value_bool(v));rrs_value_free(v);}
   } else if(ep(e)=='-'&&!isdigit((unsigned char)e->text[e->pos+1])) {
      advance(e);rrs_value_t *v=expr_prec(e,7);
      if(v&&numeric(v))lhs=v->type==RRS_INT?rrs_integer(-v->as.integer):rrs_floating(-v->as.real);
      if(v)rrs_value_free(v);
   } else lhs=primary(e);
   while(lhs&&!e->vm->error[0]) {
      ws(e);
      const char *op=NULL;
      for(int i=0;operators[i];i++)if(precedence(operators[i])>=min&&
         !strncmp(e->text+e->pos,operators[i],strlen(operators[i]))) {
         op=operators[i];break;
      }
      if(!op)break;
      for(size_t i=0;i<strlen(op);i++)advance(e);
      rrs_value_t *rhs=expr_prec(e,precedence(op)+1);
      lhs=binary(e,lhs,rhs,op);
   }
   return lhs;
}
static rrs_value_t *expression(eval_t *e){return expr_prec(e,1);}
static bool skip_block(eval_t *e) {
   ws(e);
   if(ep(e)!='{'){rrs_vm_raise(e->vm,"expected mandatory {");return false;}
   int depth=0;
   char quote=0;
   do {
      char c=advance(e);
      if(!c){rrs_vm_raise(e->vm,"unclosed control block");return false;}
      if(quote) {
         if(c=='\\'&&ep(e))advance(e);
         else if(c==quote)quote=0;
      } else if(c=='"'||c=='\'') {
         quote=c;
      } else if(c=='#'||(c=='/'&&ep(e)=='/')) {
         while(ep(e)&&ep(e)!='\n')advance(e);
      } else if(c=='/'&&ep(e)=='*') {
         advance(e);
         while(ep(e)&&!(ep(e)=='*'&&e->text[e->pos+1]=='/'))advance(e);
         if(!ep(e)){rrs_vm_raise(e->vm,"unterminated block comment");return false;}
         advance(e);advance(e);
      } else if(c=='{')depth++;
      else if(c=='}')depth--;
   } while(depth);
   return true;
}
static bool statements(eval_t *e,bool stop_on_brace);
static bool execute_block(eval_t *e,bool run) {
   ws(e);
   if(ep(e)!='{'){rrs_vm_raise(e->vm,"expected mandatory {");return false;}
   if(!run)return skip_block(e);
   advance(e);
   bool result=statements(e,true);
   return result;
}
/* Apply to a private copy, publishing only after the entire block succeeds.
 * Expressions use ordinary lexical scope; nested blocks require dictionaries. */
static bool property_block(eval_t *e,rrs_value_t *target,unsigned depth) {
   if(depth>=64 || !match(e,'{')) {
      rrs_vm_raise(e->vm,"expected property block or maximum depth exceeded");
      return false;
   }
   while(!match(e,'}')) {
      char key[192];
      if(!ident(e,key,sizeof(key))) {
         rrs_vm_raise(e->vm,"expected property name");return false;
      }
      ws(e);
      rrs_value_t *value=NULL;
      if(ep(e)=='.'&&e->text[e->pos+1]=='=') {
         advance(e);advance(e);
         const rrs_value_t *old=rrs_value_field(target,key);
         if(!old||old->type!=RRS_DICT) {
            rrs_vm_raise(e->vm,"nested property block requires dictionary");return false;
         }
         value=rrs_value_copy(old);
         if(!value||!property_block(e,value,depth+1)) {
            rrs_value_free(value);return false;
         }
      } else {
         if(ep(e)!='='||e->text[e->pos+1]=='=') {
            rrs_vm_raise(e->vm,"expected property assignment");return false;
         }
         advance(e);
         value=expression(e);
      }
      bool ok=value&&!e->vm->error[0]&&rrs_dict_put(target,key,value);
      rrs_value_free(value);
      if(!ok)return false;
      ws(e);
      if(ep(e)=='}')continue;
      if(ep(e)!=','&&ep(e)!=';') {
         rrs_vm_raise(e->vm,"expected property separator");return false;
      }
      advance(e);
   }
   return true;
}
static bool update_properties(eval_t *e,const char *name) {
   variable_t *v=find_var(e,name);
   if(!v||v->read_only||v->cell->value->type!=RRS_DICT) {
      rrs_vm_raise(e->vm,"property assignment requires mutable dictionary (not const)");
      return false;
   }
   rrs_value_t *copy=rrs_value_copy(v->cell->value);
   if(!copy)return false;
   bool previous=e->property_separators;
   e->property_separators=true;
   bool ok=property_block(e,copy,0);
   e->property_separators=previous;
   if(ok&&!e->vm->error[0])ok=set_var(e,name,copy,false);
   rrs_value_free(copy);
   return ok;
}
static bool statement(eval_t *e) {
   if(keyword(e,"if")) {
      if(!match(e,'(')){rrs_vm_raise(e->vm,"expected (");return false;}
      rrs_value_t *condition=expression(e);
      bool yes=rrs_value_bool(condition);
      rrs_value_free(condition);
      if(!match(e,')')){rrs_vm_raise(e->vm,"expected )");return false;}
      if(!execute_block(e,yes))return false;
      if(keyword(e,"else")) {
         if(!execute_block(e,!yes))return false;
      }
      return true;
   }
   if(keyword(e,"return")) {
      ws(e);
      if(!e->in_function) {
         if(ep(e)&&ep(e)!='}'&&ep(e)!=';') {
            rrs_vm_raise(e->vm,"handler cannot return a value");return false;
         }
         e->returning=true;return true;
      }
      if(ep(e)=='}')e->return_value=rrs_null();
      else e->return_value=expression(e);
      if(!e->return_value)return false;
      e->returning=true;return true;
   }
   bool read_only=keyword(e,"const");
   bool declaration=false;
   if(keyword(e,"int")||keyword(e,"float")||keyword(e,"bool")||
      keyword(e,"char")||keyword(e,"dict")||keyword(e,"auto"))declaration=true;
   if(read_only&&!declaration){rrs_vm_raise(e->vm,"const requires a type");return false;}
   if(declaration) {
      for(int i=0;i<2 && match(e,'[');i++)
         if(!match(e,']')){rrs_vm_raise(e->vm,"expected ]");return false;}
   }
   char name[192];
   if(!ident(e,name,sizeof(name))){rrs_vm_raise(e->vm,"expected statement");return false;}
   if(declaration) {
      for(int i=0;i<2 && match(e,'[');i++)
         if(!match(e,']')){rrs_vm_raise(e->vm,"expected ]");return false;}
   }
   ws(e);
   if(!declaration&&((ep(e)=='+'&&e->text[e->pos+1]=='+')||
                     (ep(e)=='-'&&e->text[e->pos+1]=='-'))) {
      char sign=advance(e);advance(e);
      variable_t *v=find_var(e,name);
      if(!v||!v->cell->value||v->cell->value->type!=RRS_INT) {
         rrs_vm_raise(e->vm,"increment requires integer variable");return false;
      }
      rrs_value_t *next=rrs_integer(v->cell->value->as.integer+(sign=='+'?1:-1));
      if(!next)return false;
      bool ok=set_var(e,name,next,false);rrs_value_free(next);return ok;
   }
   if(!declaration&&ep(e)=='.'&&e->text[e->pos+1]=='=') {
      advance(e);advance(e);
      return update_properties(e,name);
   }
   if(ep(e)==':'&&e->text[e->pos+1]=='=') {
      advance(e);advance(e);
      char target[192];
      if(!ident(e,target,sizeof(target))){rrs_vm_raise(e->vm,"reference requires a variable");return false;}
      return bind_var(e,name,target,declaration,read_only);
   }
   if(ep(e)=='='&&e->text[e->pos+1]!='=') {
      advance(e);
      rrs_value_t *value=expression(e);
      if(!value)return false;
      bool ok=set_var(e,name,value,declaration);
      if(ok&&read_only)find_var(e,name)->read_only=true;
      rrs_value_free(value);return ok;
   }
   if(declaration) {
      if(read_only){rrs_vm_raise(e->vm,"const requires an initializer");return false;}
      rrs_value_t *v=rrs_null();bool ok=set_var(e,name,v,true);rrs_value_free(v);return ok;
   }
   /* Function call: rewind and parse full expression */
   e->pos-=strlen(name);
   rrs_value_t *result=expression(e);
   if(!result)return false;
   rrs_value_free(result);
   return true;
}
static bool statements(eval_t *e,bool stop_on_brace) {
   if(++e->depth>64){rrs_vm_raise(e->vm,"maximum block depth exceeded");return false;}
   while(!e->vm->error[0]&&!e->returning) {
      ws(e);
      if(!ep(e)) {
         e->depth--;
         if(stop_on_brace){rrs_vm_raise(e->vm,"missing }");return false;}
         return true;
      }
      if(ep(e)=='}') {
         if(!stop_on_brace){rrs_vm_raise(e->vm,"unexpected }");e->depth--;return false;}
         advance(e);e->depth--;return true;
      }
      size_t before=e->pos;
      if(!statement(e)){e->depth--;return false;}
      if(e->pos==before){rrs_vm_raise(e->vm,"no parser progress");e->depth--;return false;}
   }
   /* Still consume the remainder of this block after an early return. */
   if(stop_on_brace) {
      while(ep(e)&&ep(e)!='}')advance(e);
      if(ep(e)=='}')advance(e);
   }
   e->depth--;
   return !e->vm->error[0];
}
bool rrs_vm_invoke(rrs_vm_t *vm,rrscript_binding_kind_t kind,
                   const char *name,const rrs_value_t *event) {
   if(!vm||!name)return false;
   vm->error[0]=0;
    vm->active_body=NULL;
    bool found=false;
   for(size_t i=0;i<vm->count;i++) {
      binding_t *b=&vm->bindings[i];
      if(b->info.kind!=kind||strcmp(b->name,name))continue;
      found=true;
      eval_t e={.text=b->body,.line=b->body_line,.vm=vm};
       vm->active_body=e.text;
       vm->active_offset=0;
       vm->active_line=e.line;
      rrs_value_t *input=event?rrs_value_copy(event):rrs_dictionary();
      if(!input||!set_var(&e,"event",input,true)){
         rrs_value_free(input);free_vars(e.vars);
         rrs_vm_raise(vm,"unable to initialize event");
         return false;
      }
      rrs_value_free(input);
      bool ok=statements(&e,false);
      free_vars(e.vars);
       vm->active_body=NULL;
      if(!ok||vm->error[0])return false;
      /* A name can have multiple event handlers. Command/hotkey names
       * are single dispatch points; the host resolves cross-file collisions. */
      if(kind!=RRSCRIPT_EVENT)return true;
   }
   if(!found)rrs_vm_raise(vm,"handler not registered");
   return found;
}

/* Persistent console scope: statements and expressions share variables, but
 * never inherit a loaded script's handler locals. Each console VM is separate. */
bool rrs_vm_eval(rrs_vm_t *vm,const char *source,rrs_value_t **result) {
   if(result)*result=NULL;
   if(!vm||!source)return false;
   vm->error[0]=0;
   eval_t e={.text=source,.line=1,.vm=vm,.vars=vm->console_vars};
   vm->active_body=source;
   vm->active_offset=0;
   vm->active_line=1;
   ws(&e);
   bool declaration=strchr(source,';')!=NULL || strchr(source,'\n')!=NULL;
   const char *start=e.text+e.pos;
   if(!strncmp(start,"if",2) &&
      (isspace((unsigned char)start[2])||start[2]=='('))declaration=true;
   const char *types[]={"const","int","float","bool","char","dict","auto",NULL};
   for(int i=0;types[i];i++) {
      size_t n=strlen(types[i]);
      if(!strncmp(start,types[i],n) &&
         (isspace((unsigned char)start[n])||start[n]=='[')) {
         declaration=true;break;
      }
   }
   /* Assignment is a statement; everything else evaluates to a value. */
   if(!declaration) {
      eval_t probe=e;
      char name[192];
      if(ident(&probe,name,sizeof(name))) {
         ws(&probe);
         if((ep(&probe)=='='&&probe.text[probe.pos+1]!='=') ||
            ((ep(&probe)==':'||ep(&probe)=='.')&&probe.text[probe.pos+1]=='='))
            declaration=true;
      }
      e.pos=0;e.line=1;
   }
   rrs_value_t *value=NULL;
   bool ok=true;
   if(declaration) {
      ok=statements(&e,false);
      value=rrs_null();
   } else {
      value=expression(&e);
      ws(&e);
      if(value&&ep(&e))rrs_vm_raise(vm,"unexpected trailing input");
      ok=value&&!vm->error[0];
   }
   vm->console_vars=e.vars;
   vm->active_body=NULL;
   if(!ok||vm->error[0]) {
      rrs_value_free(value);
      return false;
   }
   if(result)*result=value;
   else rrs_value_free(value);
   return true;
}
