/* SPDX-License-Identifier: MIT */
#include <librustyui/shape.h>
#include <math.h>
#include <string.h>

bool rrui_shape_pins_compatible(const rrui_pin_t *a,const rrui_pin_t *b) {
   return a && b && a->type && a->type==b->type &&
      ((a->direction==RRUI_PIN_INPUT && b->direction==RRUI_PIN_OUTPUT) ||
       (b->direction==RRUI_PIN_INPUT && a->direction==RRUI_PIN_OUTPUT));
}

bool rrui_shape_pin_position(rrui_shape_t shape,rrui_shape_bounds_t b,
                            const rrui_pin_t *pin,double *x,double *y) {
   if (!pin || !x || !y || (unsigned)shape>RRUI_SHAPE_UTILITY ||
       (unsigned)pin->side>RRUI_PIN_BOTTOM ||
       !isfinite(b.x) || !isfinite(b.y) || !isfinite(b.w) || !isfinite(b.h) ||
       b.w<=0 || b.h<=0 || !isfinite(pin->offset) ||
       pin->offset<0 || pin->offset>1) {
      return false;
   }
   /* The bounds are the external connection envelope. Shape outlines
    * may be inset by a renderer; pin positions remain backend-neutral. */
   double u=pin->offset,v=pin->offset;
   switch (pin->side) {
      case RRUI_PIN_LEFT: u=0; break;
      case RRUI_PIN_RIGHT: u=1; break;
      case RRUI_PIN_TOP: v=0; break;
      case RRUI_PIN_BOTTOM: v=1; break;
      default: return false;
   }
   double px=b.x+u*b.w,py=b.y+v*b.h;
   if (!isfinite(px) || !isfinite(py)) {
      return false;
   }
   *x=px; *y=py;
   return true;
}

static void command(rrui_path_fn emit,void *user,rrui_path_kind_t kind,
                    double a,double b,double c,double d,double e,double f) {
   rrui_path_command_t p={kind,a,b,c,d,e,f};
   emit(&p,user);
}
bool rrui_shape_path(rrui_shape_t shape,rrui_shape_bounds_t b,
                     bool decorations,rrui_path_fn emit,void *user) {
   if (!emit || (unsigned)shape>RRUI_SHAPE_UTILITY ||
       !isfinite(b.x) || !isfinite(b.y) || !isfinite(b.w) || !isfinite(b.h) ||
       b.w<=0 || b.h<=0 || !isfinite(b.x+b.w) || !isfinite(b.y+b.h)) {
      return false;
   }
   const double pi=3.14159265358979323846;
#define X(u) (b.x+b.w*(u))
#define Y(v) (b.y+b.h*(v))
#define M(u,v) command(emit,user,RRUI_PATH_MOVE,X(u),Y(v),0,0,0,0)
#define L(u,v) command(emit,user,RRUI_PATH_LINE,X(u),Y(v),0,0,0,0)
#define C(u,v,w,z,p,q) command(emit,user,RRUI_PATH_CUBIC,X(u),Y(v),X(w),Y(z),X(p),Y(q))
#define A(u,v,rx,ry,start,end) command(emit,user,RRUI_PATH_ARC,X(u),Y(v),rx,ry,start,end)
#define Z() command(emit,user,RRUI_PATH_CLOSE,0,0,0,0,0,0)
   if (decorations) {
      if (shape==RRUI_SHAPE_XOR || shape==RRUI_SHAPE_XNOR) {
         M(.17,.10); C(.37,.50,.37,.50,.17,.90);
      }
      if (shape==RRUI_SHAPE_NAND || shape==RRUI_SHAPE_NOR ||
          shape==RRUI_SHAPE_XNOR || shape==RRUI_SHAPE_NOT) {
         double u=shape==RRUI_SHAPE_NOT?.84:shape==RRUI_SHAPE_NAND?.78:.90;
         double r=b.h*(5.8/112.);
         command(emit,user,RRUI_PATH_MOVE,X(u)+r,Y(.5),0,0,0,0);
         A(u,.5,r,r,0,2*pi);
      }
   } else {
      switch (shape) {
         case RRUI_SHAPE_AND: case RRUI_SHAPE_NAND:
            M(.22,.10); L(.48,.10); A(.48,.5,b.h*.40,b.h*.40,-pi/2,pi/2);
            L(.22,.90); Z(); break;
         case RRUI_SHAPE_OR: case RRUI_SHAPE_NOR:
         case RRUI_SHAPE_XOR: case RRUI_SHAPE_XNOR:
            M(.23,.10); C(.55,.10,.74,.22,.86,.50);
            C(.74,.78,.55,.90,.23,.90); C(.43,.50,.43,.50,.23,.10); Z(); break;
         case RRUI_SHAPE_NOT:
            M(.24,.10); L(.80,.50); L(.24,.90); Z(); break;
         case RRUI_SHAPE_THRESHOLD:
            M(.23,.10); L(.77,.10); L(.91,.50); L(.77,.90);
            L(.23,.90); L(.09,.50); Z(); break;
         case RRUI_SHAPE_UTILITY:
            M(.20,.10); L(.80,.10); L(.91,.23); L(.91,.77);
            L(.80,.90); L(.20,.90); L(.09,.77); L(.09,.23); Z(); break;
         case RRUI_SHAPE_ELLIPSE:
            M(1,.5); A(.5,.5,b.w*.5,b.h*.5,0,2*pi); Z(); break;
         case RRUI_SHAPE_TRIANGLE:
            M(0,0); L(1,.5); L(0,1); Z(); break;
         case RRUI_SHAPE_RECT:
            M(0,0); L(1,0); L(1,1); L(0,1); Z(); break;
      }
   }
#undef X
#undef Y
#undef M
#undef L
#undef C
#undef A
#undef Z
   return true;
}

const rrui_graph_port_t *rrui_graph_port_find(const rrui_graph_port_t *ports,
                                             size_t count,const char *id) {
   if (!ports || !id || !*id) { return NULL; }
   for (size_t i=0;i<count;i++) {
      if (ports[i].pin.id && !strcmp(ports[i].pin.id,id)) { return &ports[i]; }
   }
   return NULL;
}
size_t rrui_graph_port_hit(const rrui_graph_port_t *ports,size_t count,
                           rrui_shape_t shape,rrui_shape_bounds_t bounds,
                           double x,double y,double radius) {
   if (!ports || !isfinite(x) || !isfinite(y) ||
       !isfinite(radius) || radius<0) { return SIZE_MAX; }
   size_t best=SIZE_MAX;
   for (size_t i=0;i<count;i++) {
      double px,py;
      if (!ports[i].available ||
          !rrui_shape_pin_position(shape,bounds,&ports[i].pin,&px,&py)) { continue; }
      double distance=hypot(px-x,py-y);
      if (distance<=radius && (best==SIZE_MAX || distance<radius)) {
         best=i; radius=distance;
      }
   }
   return best;
}
size_t rrui_graph_port_next(const rrui_graph_port_t *ports,size_t count,
                            size_t current,int direction,const rrui_pin_t *peer) {
   if (!ports || !count || !direction) { return SIZE_MAX; }
   size_t i=current<count?current:(direction>0?count-1:0);
   for (size_t n=0;n<count;n++) {
      i=direction>0?(i==count-1?0:i+1):(i?i-1:count-1);
      if (ports[i].available && ports[i].pin.type &&
          (!peer || rrui_shape_pins_compatible(peer,&ports[i].pin))) { return i; }
   }
   return SIZE_MAX;
}
