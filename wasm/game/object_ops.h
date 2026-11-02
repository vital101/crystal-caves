#ifndef GAME_OBJECT_OPS_H
#define GAME_OBJECT_OPS_H

#include <stdint.h>

#include "object.h"

typedef struct {
    uint16_t handler, handler_seg;
    uint16_t phase, speed, anim_last, anim_shape_b, anim_shape_a, vy, vx;
    uint16_t ycell, xcell, block, kind;
    int slot;
} SpawnArgs;

void anim_next(ObjectEntry *o);
void anim_select_shape(ObjectEntry *o);
void mark_redraw(int16_t x, int16_t y);
int visible_obj(int margin, int32_t x, int32_t y);
int16_t proximity_test(int mode, int16_t radius, int16_t obj_x, int16_t obj_y);
int16_t proximity_mode2(int16_t x, int16_t y, int16_t radius);
int16_t proximity_obj_dir(int mode, int16_t r1, int16_t r2, int slot);
ObjectEntry *obj_safe(int i);
void base_cell_stamp(int xcell, int ycell);
void reuse_free_slot(void);
void spawn_template_2002d(int mode);
void spawn_object_47d(const SpawnArgs *a);
void spawn_effect(int16_t shape, int16_t xcell, int16_t ycell);

#endif /* GAME_OBJECT_OPS_H */
