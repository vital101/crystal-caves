/* Crystal Caves gameplay - ObjectEntry record type (hand-written, Phase 4).
 *
 * The 0x2D-byte object record the original object loop walks (stride 0x2D;
 * slot i lives at DS:0x45D2 + i*0x2D, so field offsets below are the raw DS
 * offsets).  Only the type and its limits live here; the per-level loader
 * images are generated data in wasm/objects_data.h, included by the single
 * owner translation unit game/level.c.
 *
 * Layout is frozen (guardrail): rename fields, never reorder.  The
 * _Static_assert lines in game/state.c pin sizeof and the offsets.
 */
#ifndef GAME_OBJECT_H
#define GAME_OBJECT_H

#include <stdint.h>

/* Object slots and loader levels (mirrors the generated table sizes). */
#define OBJ_MAX 60
#define OBJ_LEVELS 19

typedef struct __attribute__((packed)) {
    uint16_t state, hit_flag, hit_points;     /* 0x00/0x02/0x04 */
    int16_t  x, y, x_prev, y_prev;            /* 0x06/0x08/0x0A/0x0C; signed: positions may be negative */
    uint16_t block;                           /* 0x0E: shape's block index */
    uint16_t anim_frame, anim_shape_a, anim_shape_b;  /* 0x10/0x12/0x14 */
    uint16_t anim_last, phase, subtype, shape;        /* 0x16/0x18/0x1A/0x1C */
    int16_t  vx, vy;                          /* 0x1E/0x20: signed velocity */
    uint16_t speed, unused24, kind;           /* 0x22/0x24/0x26 */
    uint8_t  ready;                           /* 0x28 */
    uint16_t handler, handler_seg;            /* 0x29/0x2B (odd: packed); handler_seg is always 0x1F9A */
} ObjectEntry;

#endif /* GAME_OBJECT_H */
