/* SPDX-License-Identifier: MIT */
#include <librustyui/shape.h>
#include <math.h>

bool rrui_shape_pins_compatible(const rrui_pin_t *a,const rrui_pin_t *b) {
   return a && b && a->type && a->type==b->type &&
      ((a->direction==RRUI_PIN_INPUT && b->direction==RRUI_PIN_OUTPUT) ||
       (b->direction==RRUI_PIN_INPUT && a->direction==RRUI_PIN_OUTPUT));
}

bool rrui_shape_pin_position(rrui_shape_t shape,rrui_shape_bounds_t b,
                            const rrui_pin_t *pin,double *x,double *y) {
   if (!pin || !x || !y || (unsigned)shape>RRUI_SHAPE_NOT ||
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
