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
 *
 * Player / camera / input globals (from the binary):
 *   [0x4611] player_x   [0x4613] player_y
 *   [0x512E] cam_x      [0x5130] cam_y
 *   [0x338E] left  [0x338F] right  [0x338D] up  [0x338C] down
 *   [0x4629] move_dir  (-1 left / 0 none / 1 right)
 *   [0x2BA]  player speed
 *
 * Player start comes from g_level_start (the Y-token cell at load time),
 * NOT from kind-1 objects: kind 1 / slot 1 is the player (handler 0x1fb9,
 * a noop retf 6).  Score / collectibles / lives are zero-stubs until the
 * real cell-based rules are derived (no kind-1 collectible loop remains).
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
#define SCREEN_W 320
#define SCREEN_H 200

/* ---------------------------------------------------------------- state
 *
 * Single game-state mirror of the original globals.  Field comments give the
 * original memory location each mirrors, so the port stays traceable to
 * CC1.EXE.  All gameplay functions below read/write through g_state.
 */
typedef struct {
    /* Object image (60 slots, stride 0x2D). */
    ObjectEntry obj[OBJ_MAX];
    uint16_t obj_cursor;       /* [0x5120] next slot to allocate */

    /* [0xa0f8]: pause/fade flag - nonzero skips the visible-object handler path. */
    uint8_t  a0f8;

    /* Player ([0x4611]/[0x4613]).  Initialised from g_level_start (Y-token
     * cell at load time), never hardcoded to (0,0). */
    int32_t player_x;
    int32_t player_y;

    /* Camera ([0x512E]/[0x5130]) and scroll deltas ([0x5118]/[0x5128]). */
    int32_t cam_x;
    int32_t cam_y;
    int16_t cam_dx;
    int16_t cam_dy;

    /* Input flags ([0x338E]/[0x338F]/[0x338D]/[0x338C]/[0x338A]). */
    uint16_t input_left;
    uint16_t input_right;
    uint16_t input_up;
    uint16_t input_down;
    uint16_t input_fire;

    /* Movement direction ([0x4629]: -1 left / 0 none / 1 right). */
    int16_t move_dir;

    /* Player speed ([0x2BA]). */
    uint16_t player_speed;

    /* Handler cross-reference ([0x512A]/[0x513A]) used by kind-33 objects. */
    uint16_t g_512a;
    uint16_t g_513a;

    /* Sound callback (host-set).  Forwarded to by 0x1F9A:0x21F. */
    void (*sound_callback)(uint16_t id);

    /* C4/C5: score / collectibles / lives are explicit zero-stubs until the
     * real cell-based rules are derived.  Kind 1 is the player, not a
     * collectible. */
    uint32_t score;
    uint16_t collectibles_total;
    uint16_t collectibles_left;
    uint16_t lives;
} cc_state_t;

static cc_state_t g_state = { 0 };

#define S   (&g_state)

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
    int32_t left   = S->cam_x - index;
    int32_t right  = S->cam_x + 0x140 + index;
    int32_t top    = S->cam_y - index;
    int32_t bottom = S->cam_y + 0x00C8 + index;
    return (x >= left && x <= right && y >= top && y <= bottom) ? 1 : 0;
}

/* 0x1F9A:0x177B proximity helper.
 * mode=1: same x as player, |y - player_y| < radius -> returns direction
 * mode=2: |x - player_x| < 8 and |y - player_y| < radius -> returns direction
 * Returns: -1 (above/left), 0 (none), 1 (below/right). */
static int16_t proximity_test(int mode, int16_t radius,
                              int16_t obj_x, int16_t obj_y) {
    int16_t dx, dy, adx, ady;

    if (mode == 1) {
        dx = obj_x - S->player_x;
        adx = dx < 0 ? -dx : dx;
        if (adx != 0) return 0;
        dy = obj_y - S->player_y;
        ady = dy < 0 ? -dy : dy;
        if (ady >= radius) return 0;
        return (dy > 0) ? 1 : (dy < 0 ? -1 : 0);
    } else if (mode == 2) {
        dx = obj_x - S->player_x;
        adx = dx < 0 ? -dx : dx;
        if (adx >= 8) return 0;
        dy = obj_y - S->player_y;
        ady = dy < 0 ? -dy : dy;
        if (ady >= radius) return 0;
        return (dy > 0) ? 1 : (dy < 0 ? -1 : 0);
    }
    return 0;
}

/* 0x1F9A:0x21F play_sound_or_effect(id).
 * Original: manages sound channels [0x50E0]/[0x50F4], sets [0x5138]=1,
 * [0x513F]=1, and advances the sound channel index [0x5136].
 * Here we just forward the sound ID to the host callback. */
static void play_sound(uint16_t id) {
    if (S->sound_callback) S->sound_callback(id);
}

/* ---------------------------------------------------------------- input
 * cc_input_set(flag, pressed) - set a directional input flag.
 * Callers map physical keys to [0x338E]=left, [0x338F]=right,
 * [0x338D]=up, [0x338C]=down, [0x338A]=fire. */
void cc_input_set(uint16_t flag, int pressed) {
    switch (flag) {
        case 0x338E: S->input_left  = (uint16_t)(pressed ? 1 : 0); break;
        case 0x338F: S->input_right = (uint16_t)(pressed ? 1 : 0); break;
        case 0x338D: S->input_up    = (uint16_t)(pressed ? 1 : 0); break;
        case 0x338C: S->input_down  = (uint16_t)(pressed ? 1 : 0); break;
        case 0x338A: S->input_fire  = (uint16_t)(pressed ? 1 : 0); break;
        default: break;
    }
}

/* Process one frame of input: update player position and move direction.
 * Mirrors the binary's input handling at 0xCF6C-0xD13C and 0xE1D3-0xE2FD. */
static void process_input(void) {
    int16_t speed = (int16_t)S->player_speed;

    /* Horizontal movement. */
    if (S->input_left && !S->input_right) {
        S->move_dir = -1;
        S->player_x -= speed;
    } else if (S->input_right && !S->input_left) {
        S->move_dir = 1;
        S->player_x += speed;
    } else {
        S->move_dir = 0;
    }

    /* Vertical movement (up/down). */
    if (S->input_up && !S->input_down) {
        S->player_y -= speed;
    } else if (S->input_down && !S->input_up) {
        S->player_y += speed;
    }
}

/* ---------------------------------------------------------------- camera
 * Camera follow logic (from 0xDCB9-0xE010 and 0xE1D3-0xE2FD).
 * The camera scrolls when the player approaches screen edges, with
 * cell-boundary clamping. */
static void update_camera(void) {
    /* Horizontal: if player moved, scroll camera in same direction. */
    if (S->move_dir != 0) {
        S->cam_x += S->move_dir * 2;   /* scroll speed */
    }

    /* Clamp camera to level bounds (0..1280-320 horizontally, 0..400-200 vertically). */
    if (S->cam_x < 0) S->cam_x = 0;
    if (S->cam_x > 1280 - SCREEN_W) S->cam_x = 1280 - SCREEN_W;
    if (S->cam_y < 0) S->cam_y = 0;
    if (S->cam_y > 400 - SCREEN_H) S->cam_y = 400 - SCREEN_H;
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
    S->g_512a = (uint16_t)(o - S->obj);

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
 *   if p18==3: swap S->g_512a/S->g_513a; p10 of self +/-1 by old S->g_513a vs index.
 */
static void h_70e4(ObjectEntry *o) {
    int s = (int)(o - S->obj);
    int16_t old_513a;

    S->g_512a = (uint16_t)s;
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
        old_513a = S->g_513a;
        S->g_513a = S->g_512a;   /* == s */
        S->g_512a = old_513a;
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
 *
 * C4 additions: input is processed first, then the camera is updated,
 * then every object slot is advanced. */
void cc_objects_step(void) {
    int i;

    /* C4: process input and update camera before objects. */
    process_input();
    update_camera();

    for (i = 0; i < OBJ_MAX; i++) {
        int s = (2 + i) % OBJ_MAX;          /* 2..59, 0, 1 */
        ObjectEntry *o = &S->obj[s];
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
                if (!S->a0f8 && !o->ready) {
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

/* ---------------------------------------------------------------- collision
 * Check if any object is in proximity to the player (using helper 0x1F9A:0x177B).
 * Returns the object index, or -1 if none.  `radius` is the proximity
 * threshold in pixels; `mode` selects the test (1 = same-x, 2 = box). */
int cc_collide_player(int mode, int16_t radius) {
    int i;
    for (i = 0; i < OBJ_MAX; i++) {
        ObjectEntry *o = &S->obj[i];
        if (o->kind == 0) continue;
        int16_t dx = (int16_t)o->x - S->player_x;
        int16_t dy = (int16_t)o->y - S->player_y;
        int16_t adx = dx < 0 ? -dx : dx;
        int16_t ady = dy < 0 ? -dy : dy;
        int close;
        if (mode == 1) {
            close = (adx == 0 && ady < radius);
        } else { /* mode == 2 */
            close = (adx < 8 && ady < radius);
        }
        if (close) return i;
    }
    return -1;
}

/* ---------------------------------------------------------------- exports */
void cc_objects_init(int level) {
    int i;
    if (level < 0 || level >= OBJ_LEVELS) level = 0;
    memcpy_(S->obj, g_obj_levels[level], sizeof(S->obj));
    S->obj_cursor = g_level_cursor[level];
    S->a0f8 = 0;

    /* Initialise player and camera from the level's Y-token start cell
     * ([0x2b72]/[0x2b74] path at 0x6e05), converted cell -> pixels (x*16,
     * y*16).  Previously hardcoded to (0,0) which was wrong for every level
     * except a hypothetical level whose Y token sits at (0,0). */
    S->player_x = (int32_t)g_level_start[level][1] * 16;
    S->player_y = (int32_t)g_level_start[level][0] * 16;
    S->cam_x = 0;
    S->cam_y = 0;
    S->cam_dx = 0;
    S->cam_dy = 0;
    S->move_dir = 0;
    S->player_speed = 7;

    S->input_left = S->input_right = S->input_up = S->input_down = S->input_fire = 0;
    S->g_512a = 0;
    S->g_513a = 0;

    /* score / collectibles / lives remain zero-stubs (see declaration). */
}

void cc_sound_set_callback(void (*cb)(uint16_t id)) {
    S->sound_callback = cb;
}

int cc_obj_count(void) { return OBJ_MAX; }
uint16_t cc_obj_state(int i)  { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].state  : 0; }
int32_t  cc_obj_x(int i)      { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].x      : 0; }
int32_t  cc_obj_y(int i)      { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].y      : 0; }
uint16_t cc_obj_kind(int i)   { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].kind   : 0; }
uint16_t cc_obj_shape(int i)  { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].shape  : 0; }
uint16_t cc_obj_tile(int i)   { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].tile   : 0; }
uint16_t cc_obj_p10(int i)    { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].p10    : 0; }
uint16_t cc_obj_p18(int i)    { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].p18    : 0; }
uint16_t cc_obj_ready(int i)  { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].ready  : 0; }
int32_t  cc_obj_vx(int i)     { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].vx     : 0; }
uint16_t cc_obj_cursor(void)  { return S->obj_cursor; }

/* C4 exports. */
int32_t  cc_player_x(void) { return S->player_x; }
int32_t  cc_player_y(void) { return S->player_y; }
int32_t  cc_cam_x(void)    { return S->cam_x; }
int32_t  cc_cam_y(void)    { return S->cam_y; }
int16_t  cc_move_dir(void) { return S->move_dir; }
int      cc_collide_player(int mode, int16_t radius);

/* C5 exports: score / collectibles / lives / sound. */
uint32_t cc_score(void)       { return S->score; }
uint16_t cc_collectibles_total(void)  { return S->collectibles_total; }
uint16_t cc_collectibles_left(void)   { return S->collectibles_left; }
uint16_t cc_lives(void)             { return S->lives; }
/* level_complete is a zero-stub until the real exit/collectible rules are
 * derived.  Do NOT derive it from collectibles_left (that was the invalid
 * "collect the player at (0,0)" premise). */
int      cc_level_complete(void)    { return 0; }
