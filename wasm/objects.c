/* Crystal Caves Episode 1 - objects / gameplay (clean-room original).
 *
 * Object model and update-loop semantics derived solely from CC1.EXE
 * (function 0xD72A and the per-object handlers at segment 0x1F9A).
 *
 * Object record layout (offsets are byte offsets from the object base,
 * stride 0x2D, 60 slots):
 *
 *   0x00 state      0x02 p02      0x04 p04      0x06 x
 *   0x08 y          0x0a x_prev   0x0c y_prev   0x0e tile
 *   0x10 p10        0x12 p12      0x14 p14      0x16 p16
 *   0x18 p18        0x1a p1a      0x1c shape    0x1e vx   (field p1e)
 *   0x20 vy         0x22 speed    0x24 p24      0x26 kind
 *   0x28 ready      0x29 handler  0x2b handler_seg  (always 0x1F9A)
 *
 * state: 2 = inactive slot; 0/1/0xffff = active (sub-modes depend on kind).
 * ready: 0 = not yet initialised by the update loop; 1 = initialised.
 *
 * Per level, the loader leaves a 60-slot image in memory; this module loads
 * that image (from objects_data.h, produced by the Unicorn golden oracle)
 * and runs the original update loop: one call to cc_objects_step() advances
 * every slot once (slots 2..59, then 0, then 1).
 */

#include "objects_data.h"

/* freestanding: no libc */
static void *memcpy_(void *d, const void *s, unsigned n) {
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}

#define OBJ_MAX 60

/* ---------------------------------------------------------------- state */
static ObjectEntry g_obj[OBJ_MAX];
static uint16_t g_obj_cursor = 4;   /* [0x5120]: next object slot to allocate */

/* [0xa0f8]: pause/fade flag - nonzero skips the visible-object handler path. */
static uint8_t g_a0f8 = 0;

/* Camera (stubbed for C3; level-0 active kinds bypass cull anyway). */
static int32_t g_cam_x = 0;
static int32_t g_cam_y = 0;

/* [0x512a]/[0x513a]: handler cross-reference used by the kind-33 object. */
static uint16_t g_512a = 0;
static uint16_t g_513a = 0;

/* ---------------------------------------------------------------- helpers */

/* 0x1FCB0 anim_next(index): p10++; if (p10 > p14) p10 = 1; */
static void anim_next(ObjectEntry *o) {
    o->p10 = (uint16_t)(o->p10 + 1);
    if (o->p10 > o->p14) o->p10 = 1;
}

/* 0x0F38 cull(32, index, x, y): visible iff object pixel (x,y) lies inside
 * [cam_x - index, cam_x + 0x140 + index] x [cam_y - index, cam_y + 0xC8 + index].
 * Returns 1 visible / 0 not. */
static int cull32(int index, int32_t x, int32_t y) {
    int32_t left   = g_cam_x - index;
    int32_t right  = g_cam_x + 0x140 + index;
    int32_t top    = g_cam_y - index;
    int32_t bottom = g_cam_y + 0x00C8 + index;
    return (x >= left && x <= right && y >= top && y <= bottom) ? 1 : 0;
}

/* ---------------------------------------------------------------- handlers
 * Each handler is called with the object pointer; the original passes
 * (index, x_new, y_new) on the stack, but every level-0 handler only uses
 * the object record itself.  Behaviour ID = handler offset within segment
 * 0x1F9A (all object handlers live there). */

/* 0x1F9A:0x1FB9 (kind 1): pure noop (retf 6).  Reachable from the update
 * loop when the object is visible and ready==0; does nothing.  Its other
 * logic (sound/collect) is reached from collision code (C4). */
static void h_1fb9(ObjectEntry *o) {
    (void)o;
}

/* 0x1F9A:0x2850 (kind 2): prologue object, permanently ready==1, so the
 * update loop never calls it.  Inert. */
static void h_2850(ObjectEntry *o) {
    (void)o;
}

/* 0x1F9A:0x2877 (kind 3): ready==1 at load, update loop only resets
 * ready/p18 here.  Inert. */
static void h_2877(ObjectEntry *o) {
    (void)o;
}

/* 0x1F9A:0x6FF6 (kind 32): horizontal ping-pong between pixel x=16 and
 * x=624.  Velocity vx flips sign at either bound (x moves 2 px on the flip
 * frame); on even p18 frames x advances by vx*speed. */
static void h_6ff6(ObjectEntry *o) {
    g_512a = (uint16_t)(o - g_obj);

    if (o->x > 16 && o->x < 0x0270) {
        o->vx = (int16_t)(-o->vx);
        o->x = (int16_t)(o->x + o->vx * 2);
    }

    if (o->p18 >= 0x7d00u) {
        o->p18 = 0;
    } else {
        o->p18 = (uint16_t)(o->p18 + 1);
    }

    if ((o->p18 & 1u) == 0u) {
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    }
}

/* 0x1F9A:0x70E4 (kind 33): player-style mover.
 *   anim_next(); if x<=16 or x>=624: p18=1;
 *   if p18==0: x += vx*speed; else { p18++; if (p18>5) p18=0; x += vx*speed; }
 *   if p18==3: swap g_512a/g_513a; p10 of self +/-1 by old g_513a vs index.
 */
static void h_70e4(ObjectEntry *o) {
    int s = (int)(o - g_obj);
    int16_t old_513a;

    g_512a = (uint16_t)s;
    anim_next(o);

    if (!(o->x > 16 && o->x < 0x0270)) {
        o->p18 = 1;
    }

    if (o->p18 == 0) {
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    } else {
        o->p18 = (uint16_t)(o->p18 + 1);
        if (o->p18 > 5) o->p18 = 0;
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    }

    if (o->p18 == 3) {
        old_513a = g_513a;
        g_513a = g_512a;   /* == s */
        g_512a = old_513a;
        if ((int16_t)old_513a <= (int16_t)s) {
            o->p10 = (uint16_t)(o->p10 - 1);
        } else {
            o->p10 = (uint16_t)(o->p10 + 1);
        }
    }
}

/* kind -> handler behaviour ID lookup (handler offset in segment 0x1F9A) */
typedef void (*handler_fn)(ObjectEntry *);

static handler_fn handler_for(uint16_t off) {
    switch (off) {
        case 0x1fb9: return h_1fb9;
        case 0x2850: return h_2850;
        case 0x2877: return h_2877;
        case 0x6ff6: return h_6ff6;
        case 0x70e4: return h_70e4;
        default:     return h_1fb9;   /* inert fallback: noop */
    }
}

/* ---------------------------------------------------------------- update loop
 * Function 0xD72A.  One call advances every slot once:
 * slots 2..59, then 0, then 1 (the [0x2c4] counter is reset to 2 on entry).
 */
void cc_objects_step(void) {
    int i;
    for (i = 0; i < OBJ_MAX; i++) {
        int s = (2 + i) % OBJ_MAX;          /* 2..59, 0, 1 */
        ObjectEntry *o = &g_obj[s];
        int32_t x_new = o->x + (int32_t)o->vx * o->speed;
        int32_t y_new = o->y + (int32_t)o->vy * o->speed;

        if (o->kind == 0) continue;         /* empty slot */

        if (!o->ready) {                    /* first-touch state init */
            if (o->state == 0) o->state = 1;
            else if (o->state == 1) o->state = 2;
        }

        /* Kinds whose handler runs every frame (no cull). */
        switch (o->kind) {
            case 0x20: case 0x2b: case 0x21: case 0x14: case 0x13:
            case 0x33: case 0x2d: case 0x27: case 0x3b: case 0x26: case 0x2a:
                if (!o->ready) handler_for(o->handler)(o);
                continue;
        }

        /* Viewport-cullled kinds. */
        if (cull32(s, x_new, y_new)) {
            if (o->kind == 2 || o->kind == 0x2b) {
                if (!g_a0f8 && !o->ready) {
                    /* 0x1F9A:0x251D: increments p18 of the object. */
                    o->p18 = (uint16_t)(o->p18 + 1);
                }
            }
        } else if (o->kind == 0x0d) {
            if (!o->ready && o->state == 3) o->ready = 1;
            continue;
        } else if (o->kind == 3 || o->kind == 0x1d || o->kind == 0x32 ||
                   o->kind == 0x37 || o->kind == 0x31 || o->kind == 4) {
            if (!o->ready) {
                o->ready = 1;
                o->p18 = 0;
            }
            continue;
        } else if (o->kind == 7) {
            /* kind 7 initialisation (state 0 -> 3); sprite draw stubbed. */
            if (o->state == 0) {
                int32_t ax;
                o->p18 = 0;
                o->state = 3;
                o->x = (int16_t)(o->x - 4);
                o->y = (int16_t)(o->y - 4);
                ax = o->vx;  if (ax < 0) ax = -ax;  o->x_prev = (int16_t)(o->x_prev + ax * 15);
                ax = o->vy;  if (ax < 0) ax = -ax;  o->y_prev = (int16_t)(o->y_prev + ax * 15);
                /* 0x26C5:0x0000 utility stubbed (draw-related). */
            }
            continue;
        } else {
            /* kind 1 and others: nothing further in the update loop. */
            continue;
        }

        /* kind 2 / 0x2b post-call ready logic (0xD91D). */
        if (o->kind == 2 || o->kind == 0x2b) {
            if (!o->ready && cull32(s, x_new, y_new)) o->ready = 1;
        }
    }
}

/* ---------------------------------------------------------------- exports */
void cc_objects_init(int level) {
    if (level < 0 || level >= OBJ_LEVELS) level = 0;
    memcpy_(g_obj, g_obj_levels[level], sizeof(g_obj));
    g_obj_cursor = g_level_cursor[level];
    g_a0f8 = 0;
    g_cam_x = 0;
    g_cam_y = 0;
    g_512a = 0;
    g_513a = 0;
}

int cc_obj_count(void) { return OBJ_MAX; }
uint16_t cc_obj_state(int i)  { return (i >= 0 && i < OBJ_MAX) ? g_obj[i].state  : 0; }
int32_t  cc_obj_x(int i)      { return (i >= 0 && i < OBJ_MAX) ? g_obj[i].x      : 0; }
int32_t  cc_obj_y(int i)      { return (i >= 0 && i < OBJ_MAX) ? g_obj[i].y      : 0; }
uint16_t cc_obj_kind(int i)   { return (i >= 0 && i < OBJ_MAX) ? g_obj[i].kind   : 0; }
uint16_t cc_obj_shape(int i)  { return (i >= 0 && i < OBJ_MAX) ? g_obj[i].shape  : 0; }
uint16_t cc_obj_tile(int i)   { return (i >= 0 && i < OBJ_MAX) ? g_obj[i].tile   : 0; }
uint16_t cc_obj_p10(int i)    { return (i >= 0 && i < OBJ_MAX) ? g_obj[i].p10    : 0; }
uint16_t cc_obj_p18(int i)    { return (i >= 0 && i < OBJ_MAX) ? g_obj[i].p18    : 0; }
uint16_t cc_obj_ready(int i)  { return (i >= 0 && i < OBJ_MAX) ? g_obj[i].ready  : 0; }
int32_t  cc_obj_vx(int i)     { return (i >= 0 && i < OBJ_MAX) ? g_obj[i].vx     : 0; }
uint16_t cc_obj_cursor(void)  { return g_obj_cursor; }
