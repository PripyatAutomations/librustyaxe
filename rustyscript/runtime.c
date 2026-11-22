/* Small, strict RustyScript interpreter foundation. SPDX-License-Identifier: MIT */
#include "runtime.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct field {
   char *name;
   rrs_value_t *value;
   struct field *next;
} field_t;
struct rrs_value {
   rrs_type_t type;
   union { int64_t integer; double real; bool boolean; char character;
           char *string; field_t *fields; } as;
};
typedef struct native {
   char *name;
   rrs_native_fn call;
   void *user;
   struct native *next;
} native_t;
typedef struct binding {
   rrscript_binding_t info;
   char *name;
   char *body;
} binding_t;
struct rrs_vm {
   native_t *native;
   binding_t *bindings;
   size_t count;
   char error[256];
};
typedef struct variable {
   char *name;
   rrs_value_t *value;
   struct variable *next;
} variable_t;
typedef struct {
   const char *text;
   size_t pos;
   unsigned line;
   rrs_vm_t *vm;
   variable_t *vars;
   bool returning;
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
rrs_vm_t *rrs_vm_new(void) {return calloc(1,sizeof(rrs_vm_t));}
static void free_bindings(binding_t *b,size_t n) {
   for(size_t i=0;i<n;i++){free(b[i].name);free(b[i].body);}
   free(b);
}
void rrs_vm_free(rrs_vm_t *vm) {
   if(!vm)return;
   free_bindings(vm->bindings,vm->count);
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
   if(vm)snprintf(vm->error,sizeof(vm->error),"%s",message?message:"runtime error");
}
const char *rrs_vm_error(const rrs_vm_t *vm) {return vm?vm->error:"invalid VM";}
static int collect(const rrscript_binding_t *b,void *arg) {
   rrs_vm_t *v=arg;
   binding_t *grown=realloc(v->bindings,(v->count+1)*sizeof(*grown));
   if(!grown)return -1;
   v->bindings=grown;
   binding_t *dst=&v->bindings[v->count];
   memset(dst,0,sizeof(*dst));
   dst->name=strndup(b->name,b->name_length);
   dst->body=strndup(b->body,b->body_length);
   if(!dst->name||!dst->body){free(dst->name);free(dst->body);return -1;}
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
   rrs_vm_t candidate={0};
   rrscript_error_t err;
   if(rrscript_parse_registrations(source,collect,&candidate,&err)) {
      free_bindings(candidate.bindings,candidate.count);
      snprintf(vm->error,sizeof(vm->error),"line %u:%u: %.150s",err.line,err.column,err.message);
      return false;
   }
   free_bindings(vm->bindings,vm->count);
   vm->bindings=candidate.bindings;vm->count=candidate.count;
   return true;
}
size_t rrs_vm_binding_count(const rrs_vm_t *vm){return vm?vm->count:0;}
const rrscript_binding_t *rrs_vm_binding(const rrs_vm_t *vm,size_t i) {
   return vm&&i<vm->count?&vm->bindings[i].info:NULL;
}
static void free_vars(variable_t *v) {
   while(v){variable_t *next=v->next;free(v->name);rrs_value_free(v->value);free(v);v=next;}
}
static variable_t *find_var(eval_t *e,const char *name) {
   for(variable_t *v=e->vars;v;v=v->next)if(!strcmp(v->name,name))return v;
   return NULL;
}
static bool set_var(eval_t *e,const char *name,const rrs_value_t *value,bool declare) {
   variable_t *v=find_var(e,name);
   if(declare && v){rrs_vm_raise(e->vm,"duplicate declaration");return false;}
   if(!v&&!declare){rrs_vm_raise(e->vm,"assignment to undeclared variable");return false;}
   if(!v) {
      v=calloc(1,sizeof(*v));
      if(!v)return false;
      v->name=strdup(name);
      if(!v->name){free(v);return false;}
      v->next=e->vars;e->vars=v;
   }
   rrs_value_t *c=rrs_value_copy(value);
   if(!c)return false;
   rrs_value_free(v->value);v->value=c;
   return true;
}
static char ep(eval_t *e){return e->text[e->pos];}
static char advance(eval_t *e) {char c=ep(e);if(c){e->pos++;if(c=='\n')e->line++;}return c;}
static void ws(eval_t *e) {
   for(;;) {
      while(isspace((unsigned char)ep(e))||ep(e)==';')advance(e);
      if(ep(e)=='/'&&e->text[e->pos+1]=='/') {
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
      rrs_value_t *v=val(RRS_CHAR);if(v)v->as.character=c;return v;
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
      int argc=0;
      while(!match(e,')')) {
         if(argc>=16){rrs_vm_raise(e->vm,"too many arguments");break;}
         args[argc]=expression(e);
         if(!args[argc])break;
         argc++;
         if(match(e,')'))break;
         if(!match(e,',')){rrs_vm_raise(e->vm,"expected , or )");break;}
      }
      rrs_value_t *result=NULL;
      if(!e->vm->error[0]) {
         native_t *n=e->vm->native;
         while(n&&strcmp(n->name,name))n=n->next;
         if(!n)rrs_vm_raise(e->vm,"unknown function");
         else result=n->call(e->vm,argc,args,n->user);
      }
      for(int i=0;i<argc;i++)rrs_value_free((rrs_value_t *)args[i]);
      return result?result:rrs_null();
   }
   variable_t *v=find_var(e,name);
   if(v)return rrs_value_copy(v->value);
   /* Dot paths traverse nested objects first, then flatten from longest prefix. */
   for(size_t len=strlen(name);len>0;len--) {
      if(len!=strlen(name)&&name[len]!='.')continue;
      char prefix[192];
      memcpy(prefix,name,len);prefix[len]=0;
      variable_t *root=find_var(e,prefix);
      if(!root)continue;
      const rrs_value_t *value=root->value;
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
   int depth=0;char quote=0;
   do {
      char c=advance(e);
      if(!c){rrs_vm_raise(e->vm,"unclosed control block");return false;}
      if(quote) {
         if(c=='\\'&&ep(e))advance(e);
         else if(c==quote)quote=0;
      } else if(c=='"'||c=='\'')quote=c;
      else if(c=='{')depth++;
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
      /* The current initial VM supports only handler blocks, which have null
       * return type. Reject returning values until typed functions arrive. */
      ws(e);
      if(ep(e)&&ep(e)!='}'&&ep(e)!=';') {
         rrs_vm_raise(e->vm,"handler cannot return a value");
         return false;
      }
      e->returning=true;
      return true;
   }
   bool declaration=false;
   if(keyword(e,"int")||keyword(e,"float")||keyword(e,"bool")||
      keyword(e,"char")||keyword(e,"dict"))declaration=true;
   if(declaration) {
      /* char[] is our string declaration, char alone is a character. */
      if(match(e,'[')&&!match(e,']')){rrs_vm_raise(e->vm,"expected ]");return false;}
   }
   char name[192];
   if(!ident(e,name,sizeof(name))){rrs_vm_raise(e->vm,"expected statement");return false;}
   ws(e);
   if(ep(e)=='='&&e->text[e->pos+1]!='=') {
      advance(e);
      rrs_value_t *value=expression(e);
      if(!value)return false;
      bool ok=set_var(e,name,value,declaration);
      rrs_value_free(value);
      return ok;
   }
   if(declaration){rrs_value_t *v=rrs_null();bool ok=set_var(e,name,v,true);rrs_value_free(v);return ok;}
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
   for(size_t i=0;i<vm->count;i++) {
      binding_t *b=&vm->bindings[i];
      if(b->info.kind!=kind||strcmp(b->name,name))continue;
      eval_t e={.text=b->body,.line=b->info.line,.vm=vm};
      rrs_value_t *input=event?rrs_value_copy(event):rrs_dictionary();
      if(!input||!set_var(&e,"event",input,true)){
         rrs_value_free(input);free_vars(e.vars);rrs_vm_raise(vm,"unable to initialize event");
         return false;
      }
      rrs_value_free(input);
      bool ok=statements(&e,false);
      free_vars(e.vars);
      return ok&&!vm->error[0];
   }
   rrs_vm_raise(vm,"handler not registered");
   return false;
}
