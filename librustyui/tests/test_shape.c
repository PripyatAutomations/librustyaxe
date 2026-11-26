/* SPDX-License-Identifier: MIT */
#include <librustyui/shape.h>
#include <assert.h>
#include <math.h>
int main(void) {
   rrui_pin_t a={"in",1,RRUI_PIN_INPUT,RRUI_PIN_LEFT,.25};
   rrui_pin_t b={"out",1,RRUI_PIN_OUTPUT,RRUI_PIN_RIGHT,.5};
   rrui_shape_bounds_t bounds={10,20,40,80};
   double x,y;
   assert(rrui_shape_pins_compatible(&a,&b));
   assert(!rrui_shape_pins_compatible(&a,&a));
   b.type=2;
   assert(!rrui_shape_pins_compatible(&a,&b));
   b.type=0;
   assert(!rrui_shape_pins_compatible(&a,&b));
   for (unsigned i=0;i<=RRUI_SHAPE_NOT;i++) {
      assert(rrui_shape_pin_position((rrui_shape_t)i,bounds,&a,&x,&y));
      assert(x==10 && y==40);
   }
   a.side=RRUI_PIN_TOP;
   assert(rrui_shape_pin_position(RRUI_SHAPE_RECT,bounds,&a,&x,&y));
   assert(x==20 && y==20);
   a.offset=NAN;
   assert(!rrui_shape_pin_position(RRUI_SHAPE_RECT,bounds,&a,&x,&y));
   return 0;
}
