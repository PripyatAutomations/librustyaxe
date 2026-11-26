/* SPDX-License-Identifier: MIT */
#ifndef RRUI_SHAPE_H
#define RRUI_SHAPE_H
#include <stdbool.h>
#include <stddef.h>
typedef enum {
   RRUI_SHAPE_RECT, RRUI_SHAPE_ELLIPSE, RRUI_SHAPE_TRIANGLE,
   RRUI_SHAPE_AND, RRUI_SHAPE_OR, RRUI_SHAPE_XOR,
   RRUI_SHAPE_NAND, RRUI_SHAPE_NOR, RRUI_SHAPE_XNOR, RRUI_SHAPE_NOT
} rrui_shape_t;
typedef enum {
   RRUI_PIN_LEFT, RRUI_PIN_RIGHT, RRUI_PIN_TOP, RRUI_PIN_BOTTOM
} rrui_pin_side_t;
typedef enum { RRUI_PIN_INPUT, RRUI_PIN_OUTPUT } rrui_pin_direction_t;
/* Type identifiers belong to the application, not the renderer. Zero is invalid. */
typedef struct {
   const char *id;
   unsigned type;
   rrui_pin_direction_t direction;
   rrui_pin_side_t side;
   double offset; /* Normalized distance along the side, 0..1. */
} rrui_pin_t;
typedef struct { double x,y,w,h; } rrui_shape_bounds_t;
bool rrui_shape_pin_position(rrui_shape_t shape, rrui_shape_bounds_t bounds,
                            const rrui_pin_t *pin, double *x, double *y);
bool rrui_shape_pins_compatible(const rrui_pin_t *a,const rrui_pin_t *b);
#endif
