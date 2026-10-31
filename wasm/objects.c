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
 *   [0x338E] left  [0x338F] right  [0x338B] jump  [0x3390] fire
 *   [0x4629] facing (slot-1 p1e: 1 right / 0xFFFF left)
 *   [0x2BA]  walk speed (2/4), [0x2CA] impulse, [0x5134] jump phase
 *
 * Player start comes from g_level_start (the Y-token cell at load time),
 * NOT from kind-1 objects: kind 1 / slot 1 is the player (handler 0x1fb9,
 * a noop retf 6).  Score/crystals/ammo/health/lives are driven by the cell
 * event handler (0x0FC5:0xBB26) and combat paths; see notes/gameplay.md.
 */

#include "objects.h"
#include "objects_data.h"
#include "cells_data.h"
#include "startup_data.h"
#include "shell.h"
#include "sound.h"

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

    /* Mutable 20-byte cell records for the current level (w0..w9). */
    unsigned char cells[CELL_ROWS][CELL_COLS][20];

    /* [0xa0f8]: pause/fade flag - nonzero skips the visible-object handler path. */
    uint8_t  a0f8;

    /* Player ([0x4611]/[0x4613]).  Initialised from the loader's spawn state. */
    int16_t player_x;
    int16_t player_y;

    /* Camera ([0x512E]/[0x5130]) and carry/scroll accumulators
     * ([0x5118]/[0x5126]/[0x5128]). */
    int16_t cam_x;
    int16_t cam_y;
    uint16_t g_5118;             /* [0x5118] horizontal carry delta */
    uint16_t g_5128;             /* [0x5128] vertical carry delta */
    uint16_t g_5126;             /* [0x5126] horizontal scroll accumulator */

    /* Input flags ([0x338E]/[0x338F]/[0x338B]/[0x3390]); 0x338A is joystick. */
    uint8_t input_left;
    uint8_t input_right;
    uint8_t input_jump;
    uint8_t input_down;
    uint8_t input_fire;
    uint8_t input_joystick_fire;

    /* Player movement state. */
    uint16_t move_dir;           /* [0x4629]: 1 right / 0xFFFF left */
    uint16_t walk_speed;         /* [0x2BA] 0/2/4 from 0x6BE */
    uint16_t walk_phase;         /* [0xA0B4] walk acceleration counter */
    uint16_t impulse;            /* [0x2CA] decaying knockback/carry speed */
    uint16_t jump_phase;         /* [0x5134] 0..0x13 vertical table index */
    uint16_t c8;                 /* [0x2C8] airborne frame counter */
    uint16_t c6;                 /* [0x2C6] jump sound latch */
    uint8_t  jump_flag;          /* [0x306] ascending */
    uint8_t  airborne;           /* [0xA0FD] not on floor */
    uint8_t  anim_suppress;      /* [0x30A] suppress walk animation */
    uint8_t  status_30b;         /* [0x30B] */
    uint8_t  paused_30c;         /* [0x30C] */
    int16_t  gravity_scale;      /* [0x5132] 1 normal / -1 reversed */
    uint16_t shape_player;       /* [0x4627] */
    uint16_t shape_a;            /* [0x461F] right/idle shape */
    uint16_t shape_b;            /* [0x461D] left/idle shape */
    uint16_t player_block;       /* [0x4619] 6 normal / 8 reversed */
    uint16_t anim_timer;         /* [0x461B] */
    uint16_t g_a0fc;             /* [0xA0FC] jump/head-bump latch */
    uint16_t g_a0b8;             /* [0xA0B8] jump sound latch */
    uint8_t  blocked_l;          /* [0xA0F1] */
    uint8_t  blocked_r;          /* [0xA0F2] */

    /* Current level (for cell collision lookups). */
    int16_t cur_level;

    /* Exit / level-transition state ([0xA0E0]/[0xA0E2] exit cell,
     * [0xA0E6] exit step counter, [0x2B72]/[0x2B74] spawn override). */
    int16_t exit_col;            /* [0xA0E0] */
    int16_t exit_row;            /* [0xA0E2] */
    uint16_t exit_step;          /* [0xA0E6] */
    uint16_t spawn_col_override; /* [0x2B72]: 0xffff = none, use Y-token start */
    uint16_t spawn_row_override; /* [0x2B74] */
    int16_t next_level;          /* [0xA0BE] current/pending loader argument */

    /* Phase 3: cells, crystals, exits and hub doors. */
    uint16_t g_a0cc;             /* [0xA0CC] crystals remaining in this cave */
    uint16_t g_50a2;             /* [0x50A2] crystals collected */
    uint8_t  g_a0f5;             /* [0xA0F5] exit unlocked (all crystals) */
    uint8_t  g_a0f7;             /* [0xA0F7] hub-door entry sequence */
    uint8_t  g_a0f9;             /* [0xA0F9] switch/door latch */
    uint8_t  g_a0f3;             /* [0xA0F3] lever/door state */
    uint8_t  g_a0fb;             /* [0xA0FB] all caves complete/finale */
    uint8_t  g_a126;             /* [0xA126] story/menu transition */
    uint8_t  g_a10b;             /* [0xA10B] collectible spawn active */
    uint8_t  g_a10a;             /* [0xA10A] spawn base-cell suppress */
    uint16_t g_a0e8;             /* [0xA0E8] switch repeat counter */
    uint16_t g_a0d0;             /* [0xA0D0] spawned-cell px */
    uint16_t g_a0d2;             /* [0xA0D2] spawned-cell py */
    uint16_t g_a0d4;             /* [0xA0D4] switch A cell (raw col+1) */
    uint16_t g_a0d6;             /* [0xA0D6] switch A cell (raw row+1) */
    uint16_t g_a0d8;             /* [0xA0D8] switch B cell (raw col+1) */
    uint16_t g_a0da;             /* [0xA0DA] switch B cell (raw row+1) */
    uint16_t g_a0dc;             /* [0xA0DC] switch C cell (raw col+1) */
    uint16_t g_a0de;             /* [0xA0DE] switch C cell (raw row+1) */
    uint16_t g_a0ce;             /* [0xA0CE] collectible spawn timer */
    uint16_t g_a0c2;             /* [0xA0C2] power-shot timer preset */
    uint16_t g_a0c8;             /* [0xA0C8] power-shot timer preset */
    uint16_t g_a0ca;             /* [0xA0CA] power-shot timer preset */
    uint16_t g_a0e4;             /* [0xA0E4] power-shot timer preset */
    uint16_t g_a0c6;             /* [0xA0C6] power-shot sub-timer */
    uint8_t  g_a100;             /* [0xA100] timer event latch */
    uint8_t  g_a101;             /* [0xA101] switch animation latch */
    uint8_t  g_a102;             /* [0xA102] switch animation latch */
    uint8_t  g_a104;             /* [0xA104] power-up animation latch */
    uint8_t  g_a105;             /* [0xA105] switch animation latch */
    uint8_t  g_a106;             /* [0xA106] exit-open latch */
    uint8_t  g_a107;             /* [0xA107] exit-open latch */
    uint8_t  g_a108;             /* [0xA108] lever animation latch */
    uint8_t  g_a109;             /* [0xA109] switch animation latch */
    uint8_t  g_a10d;             /* [0xA10D] gravity switch latch */
    uint8_t  g_a0f4;             /* [0xA0F4] joystick fire latch */
    uint8_t  g_3392;             /* [0x3392] joystick latch */
    uint8_t  g_3395;             /* [0x3395] story/exit key latch */
    uint8_t  g_3396;             /* [0x3396] story/exit key latch */
    uint8_t  cave_flags[17];     /* [0x50A8 + arg], 1 = available/uncompleted */
    uint8_t  level_block[0x21];  /* [0x5098] checkpoint/level state block */
    uint8_t  checkpoint_block[0x21]; /* [0x50BA] saved copy */

    /* Handler cross-reference ([0x512A]/[0x513A]) used by kind-33 objects. */
    uint16_t g_512a;
    uint16_t g_513a;

    /* [0x511a]/[0x511c] platform attachment slots. */
    uint16_t g_511a;
    uint16_t g_511c;
    uint8_t  g_513c;             /* [0x513c] interaction enabled */
    uint8_t  g_5140;             /* [0x5140] interaction suppression */
    uint8_t  g_513d;             /* [0x513d] riding a horizontal platform */
    uint8_t  g_5142;             /* [0x5142] riding a vertical platform */
    uint16_t g_5124;             /* [0x5124] interaction/death state */
    uint16_t g_a0ea;             /* [0xa0ea] hit/invulnerability state */
    uint8_t  g_a0f6;             /* [0xa0f6] interaction lock */
    uint16_t g_509e;             /* [0x509e] lives/crystal counter in border cell */
    uint16_t g_509c;             /* [0x509c] HUD counter in border cell */

    /* Phase 2 exact combat/fire state. */
    uint16_t score_lo;           /* [0x50a5] */
    uint16_t score_hi;           /* [0x50a7] */
    uint16_t rng_lo;             /* [0x02a6] */
    uint16_t rng_hi;             /* [0x02a8] */
    uint16_t cooldown;           /* [0x02e0] frames since fire */
    uint16_t g_260;              /* [0x0260] 4FD4 retarget timer */
    uint16_t g_50dc;             /* [0x50dc] 403A proximity radius A */
    uint16_t g_50de;             /* [0x50de] 403A proximity radius B */
    uint16_t g_50e0;             /* [0x50e0] 403A proximity radius C */
    uint8_t  g_a0f0;             /* [0xa0f0] 5C8F companion flag A */
    uint8_t  g_a0fa;             /* [0xa0fa] 5C8F companion flag B */
    uint16_t g_2cc, g_2ce, g_2d4, g_2d6, g_2d8; /* loader map-command state */
    uint16_t g_2fa, g_2fc, g_2fe;
    uint16_t fire_impulse;       /* [0x02da] per-level firing impulse */
    uint16_t g_2f2;              /* [0x02f2] status object index */
    uint16_t g_2b8;              /* [0x02b8] base cell block */
    uint16_t g_511e;             /* [0x511e] level tile family */
    uint16_t g_5122;             /* [0x5122] dominant cell shape */
    uint8_t  g_30d;              /* [0x030d] disable cell cache stamp */
    uint16_t g_2f6;              /* [0x02f6] last cell cache stamp */
    uint16_t g_512c;             /* [0x512c] slash cell counter */
    uint16_t g_2b5e;             /* [0x2b5e] restart timer */
    int16_t  g_2b5c;             /* [0x2b5c] text-window vertical scroll */
    uint8_t  g_3394;             /* [0x3394] restart input latch */
    uint8_t  g_a110;             /* [0xa110] restart latch */
    uint8_t  power_shot;         /* [0xa0ff] power-shot active */
    uint16_t power_timer;        /* [0xa0c4] power-shot expiry */
    uint16_t checkpoint_50a0;    /* [0x50a0] restart checkpoint */
    uint16_t checkpoint_50be;    /* [0x50be] */
    uint16_t checkpoint_50c4;    /* [0x50c4] */
    uint16_t checkpoint_50c7;    /* [0x50c7] */
    uint16_t checkpoint_50c9;    /* [0x50c9] */
} cc_state_t;

/* The save payload is a raw struct image.  Any field change must bump the
 * save format version and add a migration (see cc_state_save below). */
_Static_assert(sizeof(cc_state_t) == 23010,
               "cc_state_t changed; bump the save format version");
_Static_assert(sizeof(cc_state_t) <= 0xFFFF, "save payload too large");

static cc_state_t g_state = { 0 };
static const unsigned char *g_cells_asset = 0;

/* Host-only sound attempt log/callback.  Deliberately outside cc_state_t so
 * the deterministic save payload never contains pointers or host state. */
static void (*g_sound_callback)(uint16_t id);
static uint16_t g_sound_log[256];
static uint16_t g_sound_log_count;
static uint32_t g_sound_log_total;

#define S   (&g_state)

/* 16-bit word arithmetic helpers: the 8086 wraps on every operation. */
static int16_t wadd(int16_t a, int16_t b) { return (int16_t)((uint16_t)a + (uint16_t)b); }
static int16_t wsub(int16_t a, int16_t b) { return (int16_t)((uint16_t)a - (uint16_t)b); }
static int16_t wmul(int16_t a, int16_t b) { return (int16_t)((uint16_t)a * (uint16_t)b); }
static int16_t wneg(int16_t a) { return (int16_t)(0u - (uint16_t)a); }

/* [0x50a5]/[0x50a7] add/adc pair.  The two words are not contiguous in
 * memory (0x50a6 is unused), so carry is applied explicitly. */
static void score_add(uint16_t lo_add, uint16_t hi_add) {
    uint32_t lo = (uint32_t)S->score_lo + lo_add;
    uint32_t hi = (uint32_t)S->score_hi + hi_add + (lo >> 16);
    S->score_lo = (uint16_t)lo;
    S->score_hi = (uint16_t)hi;
}

void cc_set_cells(const unsigned char *cells) { g_cells_asset = cells; }

/* Mutable cell record access.  Logical (col,row) maps to the original
 * 0x508C + (col+1)*500 + (row+1)*20 index. */
static unsigned char *cell_rec(int col, int row) {
    if (col < 0 || col >= CELL_COLS || row < 0 || row >= CELL_ROWS) return 0;
    return S->cells[row][col];
}
static int cell_byte_cur(int col, int row, int off) {
    unsigned char *c = cell_rec(col, row);
    return c ? c[off] : 0;
}
static void cell_put(int col, int row, int off, int value) {
    unsigned char *c = cell_rec(col, row);
    if (c) c[off] = (unsigned char)value;
}
static int cell_word_cur(int col, int row, int off) {
    return cell_byte_cur(col, row, off) |
           (cell_byte_cur(col, row, off + 1) << 8);
}
static void cell_word_put(int col, int row, int off, int value) {
    cell_put(col, row, off, value & 0xFF);
    cell_put(col, row, off + 1, (value >> 8) & 0xFF);
}
/* Raw original cell indices already include the +1 border offset. */
static int cell_word_raw(int col1, int row1, int off) {
    return cell_word_cur(col1 - 1, row1 - 1, off);
}
static void cell_word_raw_put(int col1, int row1, int off, int value) {
    cell_word_put(col1 - 1, row1 - 1, off, value);
}
static int cave_flag_get(int idx) {
    if (idx < 0 || idx > 16) return 0;
    return S->cave_flags[idx];
}
static void cave_flag_set(int idx, int value) {
    if (idx >= 0 && idx <= 16) {
        S->cave_flags[idx] = (uint8_t)value;
        S->level_block[0x10 + idx] = (uint8_t)value;
    }
}

/* Current-level solid flag: mutable oracle cells when available, otherwise
 * the baked solid bitmap from cells_data.h. */
static int cell_solid_cur(int col, int row) {
    if (col < 0 || col >= CELL_COLS || row < 0 || row >= CELL_ROWS) return 1;
    if (g_cells_asset) return S->cells[row][col][17] != 0;
    return cc_cell_solid(S->cur_level, col, row);
}

/* ---------------------------------------------------------------- helpers */

/* 0x1FCB0 anim_next(index): p10++; if (p10 > p16) p10 = 1; */
static void anim_next(ObjectEntry *o) {
    o->p10 = (uint16_t)(o->p10 + 1);
    if (o->p10 > o->p16) o->p10 = 1;
}

/* 0x1FCF7 anim_select_shape(index): p1e == 1 ? p14 : p12 -> shape. */
static void anim_select_shape(ObjectEntry *o) {
    o->shape = (o->vx == 1) ? o->p14 : o->p12;
}

/* 0x1FC73 mark_redraw(x, y): the original queues a cell redraw and calls the
 * video blitters.  Runtime rendering only needs the cell coordinates; keep
 * the call traceable and let the renderer redraw the affected cell. */
static void mark_redraw(int16_t x, int16_t y) {
    (void)x;
    (void)y;
}

/* 0x0F38 cull(margin, index, x, y): visible iff object pixel (x,y) lies inside
 * [cam_x - margin, cam_x + 0x140 + margin] x [cam_y - margin, cam_y + 0xC8 + margin].
 * The update loop always passes margin 0x20. */
static int visible_obj(int margin, int32_t x, int32_t y) {
    int32_t left   = S->cam_x - margin;
    int32_t right  = S->cam_x + 0x140 + margin;
    int32_t top    = S->cam_y - margin;
    int32_t bottom = S->cam_y + 0x00C8 + margin;
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
        /* 0x2177B mode 1: same y, x within radius; direction by x. */
        dy = (int16_t)(obj_y - S->player_y);
        if (dy != 0) return 0;
        dx = (int16_t)(obj_x - S->player_x);
        adx = dx < 0 ? (int16_t)-dx : dx;
        if (adx >= radius) return 0;
        return (dx > 0) ? -1 : 1;
    } else if (mode == 2) {
        dx = (int16_t)(obj_x - S->player_x);
        adx = dx < 0 ? (int16_t)-dx : dx;
        if (adx >= 8) return 0;
        dy = (int16_t)(obj_y - S->player_y);
        ady = dy < 0 ? (int16_t)-dy : dy;
        if (ady >= radius) return 0;
        return (dy > 0) ? -1 : 1;
    }
    return 0;
}

/* 0x1F9A:0x1DDB proximity helper mode 2 (used by 1FC2):
 * |x-player_x| < 8 and |y-player_y| < radius; returns 1 when the object is
 * at or above the player, -1 when below, 0 when out of range. */
static int16_t proximity_mode2(int16_t x, int16_t y, int16_t radius) {
    int16_t d;
    d = (int16_t)(x - S->player_x);
    if (d < 0) d = (int16_t)-d;
    if (d >= 8) return 0;
    d = (int16_t)(y - S->player_y);
    if (d < 0) d = (int16_t)-d;
    if (d >= radius) return 0;
    return ((int16_t)(y - S->player_y) > 0) ? -1 : 1;
}

/* Raw attempt log + host callback.  This is deliberately separate from the
 * Phase 6 driver state: the existing combat/cell/handler fixtures compare
 * the exact sequence of IDs passed to 0x1F9A:0x21F, regardless of the
 * sound-enable/priority decision inside the routine. */
void cc_sound_note(uint16_t id) {
    if (g_sound_log_count < 256)
        g_sound_log[g_sound_log_count++] = id;
    g_sound_log_total++;
    if (g_sound_callback) g_sound_callback(id);
}

/* 0x1F9A:0x21F play_sound_or_effect(id).  All logging and driver selection
 * live in cc_sound_play()/cc_sound_note() so shell-triggered sounds use the
 * same path. */
static void play_sound(uint16_t id) {
    cc_sound_play(id);
}

/* ---------------------------------------------------------------- input
 * cc_input_set(flag, pressed) - set a directional input flag.
 * Callers map physical keys to [0x338E]=left, [0x338F]=right,
 * [0x338B]=jump, [0x3390]=fire.  [0x338A] is joystick fire. */
void cc_input_set(uint16_t flag, int pressed) {
    uint8_t v = (uint8_t)(pressed ? 1 : 0);
    switch (flag) {
        case 0x338E: S->input_left = v; break;
        case 0x338F: S->input_right = v; break;
        case 0x338B: S->input_jump = v; break;
        case 0x338C: S->input_down = v; break;
        case 0x3390: S->input_fire = v; break;      /* keyboard fire */
        case 0x338A: S->input_joystick_fire = v; break; /* joystick fire */
        default: break;
    }
}

/* Apply the keyboard-ISR side effects once, after a whole input frame has
 * been latched.  Doing this in cc_input_set() resets the walk phase on the
 * intermediate left=0/right=0 state when the player switches direction. */
static void input_commit(void) {
    if (!S->input_left && !S->input_right) S->walk_phase = 0;
    if (S->input_left || S->input_right) S->anim_suppress = 0;
}

/* ---------------------------------------------------------------- cell collision
 * 0x1F9A:0x43 collision helper (load 0x1F9E3).
 * Tests 9 player-hitbox corners against cell byte 0x11 (solid flag):
 *   (x+3/8/12 + xoff) x (y+0/8/15 + yoff)
 * Returns 1 if ANY corner is solid (blocked), 0 if all clear.
 * Cell layout is column-major: cell = (col+1)*500 + (row+1)*20, cell size 16px.
 */
static int cc_cell_collide(int level, int32_t px, int32_t py,
                           int16_t xoff, int16_t yoff) {
    static const int8_t dx[3] = { 3, 8, 12 };
    (void)level;
    static const int8_t dy[3] = { 0, 8, 15 };
    int ci, cj;
    for (ci = 0; ci < 3; ci++) {
        int32_t cx = (px + dx[ci] + xoff) / 16;
        for (cj = 0; cj < 3; cj++) {
            int32_t cy = (py + dy[cj] + yoff) / 16;
            if (cell_solid_cur((int)cx, (int)cy)) return 1;
        }
    }
    return 0;
}

/* 0x1F9E3: returns 1 when the 9-point player box is clear at (xoff,yoff),
 * 0 when blocked.  A blocked move also clears [0xa0b4]. */
static int player_clear(int16_t xoff, int16_t yoff) {
    if (cc_cell_collide(S->cur_level, S->player_x, S->player_y, xoff, yoff)) {
        S->walk_phase = 0;
        return 0;
    }
    return 1;
}

/* Pixel -> cell helpers used by the vertical routine.  The original uses
 * `idiv 16` for signed values in the reversed-gravity/special paths and
 * logical shifts for the normal down path; all in-range values agree. */
static int cell_solid_px(int16_t x, int16_t y) {
    int col = (int)x / 16;
    int row = (int)y / 16;
    return cell_solid_cur(col, row);
}

static int cell_special_px(int16_t x, int16_t y) {
    int col = (int)x / 16;
    int row = (int)y / 16;
    if (col < 0 || col >= CELL_COLS || row < 0 || row >= CELL_ROWS) return 0;
    if (g_cells_asset) return S->cells[row][col][18] != 0;
    return 0;
}

/* 0x1F9A:0x310 anim_next(1): advance the player object animation frame. */
static void player_anim_next(void) {
    S->anim_timer = (uint16_t)(S->anim_timer + 1u);
    if (S->anim_timer > S->obj[1].p16) S->anim_timer = 1;
}

/* 0x1F9A:0x357 anim_select_shape(1): p1e==1 ? p14 : p12 -> shape. */
static void player_select_shape(void) {
    S->shape_player = (S->move_dir == 1) ? S->shape_a : S->shape_b;
}

/* Mirror aliased player globals into object slot 1 after a full frame. */
static void sync_player_object(void) {
    ObjectEntry *p = &S->obj[1];
    p->x = S->player_x;
    p->y = S->player_y;
    p->shape = S->shape_player;
    p->vx = (int16_t)S->move_dir;
    p->tile = S->player_block;
    p->p10 = S->anim_timer;
    p->p12 = S->shape_b;
    p->p14 = S->shape_a;
}

/* 0x6BE: walk acceleration phase -> speed 2 for phases 1-2, 4 afterwards. */
static void update_walk_speed(void) {
    S->walk_phase = (uint16_t)(S->walk_phase + 1u);
    if (S->walk_phase == 1 || S->walk_phase == 2) S->walk_speed = 2;
    else if (S->walk_phase >= 3 && S->walk_phase <= 0x7D00) S->walk_speed = 4;
}

/* 0xDCB9: falling/gravity half of the vertical routine. */
static void cc_vertical(void) {
    int16_t saved_y = S->player_y;
    int16_t delta;
    int move;

    S->jump_phase = (uint16_t)(S->jump_phase + 1u);
    if ((int16_t)S->jump_phase > 0x13) S->jump_phase = 0x13;
    S->c8 = (uint16_t)(S->c8 + 1u);
    S->airborne = 1;

    move = 0;
    if (S->g_5142 && (int16_t)S->jump_phase < 0xB) move = 1;
    if (S->g_513d && (int16_t)S->jump_phase < 0xB) move = 1;
    if (!S->g_513d && !S->g_5142 && (int16_t)S->jump_phase > 0) move = 1;
    if (move) {
        delta = wmul(g_vert_speed[S->jump_phase], S->gravity_scale);
        S->player_y = wadd(S->player_y, delta);
    }

    if (S->g_5142 || S->g_513d) S->airborne = 0;

    if (S->gravity_scale == 1) {
        int blocked = cell_solid_px((int16_t)(S->player_x + 3),
                                    (int16_t)(S->player_y + 0x10));
        if (!blocked)
            blocked = cell_solid_px((int16_t)(S->player_x + 0xC),
                                    (int16_t)(S->player_y + 0x10));
        if (blocked) {
            if (!S->g_5142 && !S->g_513d)
                S->player_y = (int16_t)(((int)S->player_y / 16) * 16);
            S->airborne = 0;
        }

        {
            int special = cell_special_px((int16_t)(S->player_x + 3),
                                          (int16_t)(S->player_y + 0x10));
            if (!special)
                special = cell_special_px((int16_t)(S->player_x + 0xC),
                                          (int16_t)(S->player_y + 0x10));
            if (special) {
                int16_t dx = (int16_t)((((int)S->player_y + 0x10) / 16) * 16 + 2);
                if (wadd(saved_y, 0x10) <= dx) {
                    S->player_y = (int16_t)(((int)S->player_y / 16) * 16);
                    S->airborne = 0;
                }
            }
        }
    } else if (S->gravity_scale == -1) {
        int blocked = cell_solid_px((int16_t)(S->player_x + 3),
                                    (int16_t)(S->player_y - 1));
        if (!blocked)
            blocked = cell_solid_px((int16_t)(S->player_x + 0xC),
                                    (int16_t)(S->player_y - 1));
        if (blocked) {
            S->player_y = (int16_t)((((int)S->player_y + 0xC) / 16) * 16);
            S->airborne = 0;
        }
    }

    /* 0xDED4: idle animation shape. */
    if ((int16_t)S->impulse <= 0 && !S->anim_suppress) {
        S->shape_player = (S->move_dir == 1) ? 0x19 : 0x1A;
    }

    /* 0xDEF7: camera-y follow using the same displacement profile. */
    if (S->gravity_scale == 1) {
        if (wadd(S->cam_y, 0x78) < S->player_y) {
            int16_t nd = wadd(S->cam_y, wmul(g_vert_speed[S->jump_phase],
                                             S->gravity_scale));
            if (nd >= 0xC8) S->cam_y = 0xC8;
            else S->cam_y = nd;
        }
    } else if (S->gravity_scale == -1) {
        if (wsub(S->player_y, 0x50) < S->cam_y) {
            int16_t t = wsub(S->cam_y, g_vert_speed[S->jump_phase]);
            if (t <= 0x10) S->cam_y = 0x10;
            else S->cam_y = wadd(S->cam_y, wmul(
                g_vert_speed[S->jump_phase], S->gravity_scale));
        }
    }

    if (!S->airborne) {
        S->c8 = 0;
        if ((int16_t)S->impulse <= 0 && !S->anim_suppress)
            player_select_shape();
    }

    /* 0xDFA2: first airborne frame aligns y to the 8px animation grid. */
    if (S->gravity_scale != -1 && S->c8 == 1) {
        int16_t rem = (int16_t)(S->player_y % 8);
        if (rem < 7) S->player_y = wsub(S->player_y, rem);
        else S->player_y = wadd(S->player_y, rem);
    }

    if (!S->airborne) {
        S->c8 = 0;
        if ((int16_t)S->impulse <= 0 && !S->anim_suppress)
            player_select_shape();
    }

    /* 0xDFF6: airborne disables platform attachment latches. */
    if (S->airborne) {
        S->anim_timer = 1;
        S->g_513d = 0;
        S->g_5142 = 0;
    }
}

/* 0xE36B: jump start + ascending half; falls through to 0xDCB9. */
static void cc_jump_vertical(void) {
    uint16_t saved_walk = S->walk_phase;

    if (!S->jump_flag && !S->airborne && S->input_jump) {
        int16_t up = wmul(S->gravity_scale, -4);
        if (player_clear(0, up)) {
            S->anim_suppress = 0;
            S->jump_flag = 1;
            S->c6 = 0;
            S->jump_phase = 0;
            S->g_513d = 0;
            S->g_5142 = 0;
            play_sound(1);
        } else {
            if (!S->g_a0b8) {
                S->g_a0fc = 1;
                S->g_a0b8 = 1;
            }
            S->walk_phase = saved_walk;
        }
    }

    if (!S->jump_flag) {
        cc_vertical();
        return;
    }

    S->anim_timer = 1;
    if (!S->anim_suppress) {
        S->shape_player = (S->move_dir == 1) ? 0x19 : 0x1A;
    }

    S->jump_phase = (uint16_t)(S->jump_phase + 1u);
    if (S->jump_phase == 0xA) {
        S->jump_flag = 0;
        S->jump_phase = 9;
    }
    if ((int16_t)S->jump_phase >= 0x14) {
        if (!S->jump_flag) cc_vertical();
        return;
    }

    {
        int16_t d = wmul(g_vert_speed[S->jump_phase], S->gravity_scale);
        S->player_y = wsub(S->player_y, d);

        if (S->gravity_scale == 1) {
            if (player_clear(0, wneg(d))) {
                if (S->cam_y > 0x10 && wadd(S->cam_y, 0x50) > S->player_y) {
                    int16_t ny = wsub(S->cam_y, wmul(
                        g_vert_speed[S->jump_phase], S->gravity_scale));
                    S->cam_y = (ny < 0x10) ? 0x10 : ny;
                }
            } else {
                S->jump_phase = 9;
                S->jump_flag = 0;
            }
        } else if (S->gravity_scale == -1) {
            if (player_clear(0, g_vert_speed[S->jump_phase])) {
                if (S->cam_y < 0xC8 && wadd(S->cam_y, 0x78) < S->player_y) {
                    int16_t ny = wadd(S->cam_y,
                                      g_vert_speed[S->jump_phase]);
                    S->cam_y = (ny > 0xC8) ? 0xC8 : ny;
                }
            } else {
                S->jump_phase = 9;
                S->jump_flag = 0;
            }
        }
    }

    if (!S->jump_flag) cc_vertical();
}

/* 0xE011 + 0xE2FD: horizontal movement, camera-x, platform carry, camera-y. */
static void cc_horizontal(void) {
    const int16_t right = (S->input_right || S->blocked_r) ? 1 : 0;
    const int16_t left = (S->input_left || S->blocked_l) ? 1 : 0;

    if ((int16_t)S->impulse > 0) {
        if (S->move_dir == 1) {
            int16_t off = wadd(wneg(S->impulse), S->g_5126);
            if (player_clear(off, 0)) {
                if (S->cam_x > 0 && wadd(S->cam_x, 0x96) > S->player_x) {
                    int16_t nx = wsub(S->cam_x,
                                      wadd(S->impulse, S->g_5126));
                    S->cam_x = (nx < 0) ? 0 : nx;
                }
                S->player_x = wsub(S->player_x, S->impulse);
            } else {
                S->impulse = ((int16_t)S->impulse > 1)
                             ? (uint16_t)((int16_t)S->impulse / 2) : 0;
            }
        } else {
            int16_t off = wsub(S->impulse, S->g_5126);
            if (player_clear(off, 0)) {
                if (S->cam_x < 0x140 && wadd(S->cam_x, 0xAA) < S->player_x) {
                    int16_t nx = wadd(S->cam_x,
                                      wsub(S->impulse, S->g_5126));
                    S->cam_x = (nx > 0x140) ? 0x140 : nx;
                }
                S->player_x = wadd(S->player_x, S->impulse);
            } else {
                S->impulse = ((int16_t)S->impulse > 1)
                             ? (uint16_t)((int16_t)S->impulse / 2) : 0;
            }
        }
        if ((int16_t)S->impulse > 0) S->impulse--;
        goto idle_carry;
    }

    if (right) {
        S->move_dir = 1;
        if (!S->jump_flag && !S->airborne) S->shape_player = S->shape_a;
        update_walk_speed();
        {
            int16_t off = wsub(S->walk_speed, S->g_5126);
            if (player_clear(off, 0)) {
                if (S->cam_x < 0x140 && wadd(S->cam_x, 0xAA) < S->player_x) {
                    int16_t nx = wadd(S->cam_x, wsub(S->walk_speed,
                                                     S->g_5126));
                    S->cam_x = (nx > 0x140) ? 0x140 : nx;
                }
                S->player_x = wadd(S->player_x, S->walk_speed);
                player_anim_next();
            }
        }
    }

    if (left) {
        S->move_dir = (uint16_t)-1;
        if (!S->jump_flag && !S->airborne) S->shape_player = S->shape_b;
        update_walk_speed();
        {
            int16_t off = wadd(wneg(S->walk_speed), S->g_5126);
            if (player_clear(off, 0)) {
                if (S->cam_x > 0 && wadd(S->cam_x, 0x96) > S->player_x) {
                    int16_t nx = wsub(S->cam_x,
                                      wadd(S->walk_speed, S->g_5126));
                    S->cam_x = (nx < 0) ? 0 : nx;
                }
                S->player_x = wsub(S->player_x, S->walk_speed);
                player_anim_next();
            }
        }
    }

    if (!left && !S->blocked_l && !right && !S->blocked_r)
        S->anim_timer = 1;

idle_carry:
    if (S->g_5118 != 0) {
        if ((int16_t)S->g_5118 < 0) {
            if (player_clear((int16_t)S->g_5118, 0)) {
                if (S->cam_x > 0 && wadd(S->cam_x, 0x96) > S->player_x) {
                    int16_t nx = wadd(S->cam_x, (int16_t)S->g_5118);
                    S->cam_x = (nx < 0) ? 0 : nx;
                    S->g_5118 = 0;
                }
            }
        } else {
            if (player_clear((int16_t)S->g_5118, 0)) {
                if (S->cam_x < 0x140 && wadd(S->cam_x, 0xAA) < S->player_x) {
                    int16_t nx = wadd(S->cam_x, (int16_t)S->g_5118);
                    S->cam_x = (nx > 0x140) ? 0x140 : nx;
                    S->g_5118 = 0;
                }
            }
        }
    }

    if (S->g_5128 != 0) {
        if ((int16_t)S->g_5128 < 0) {
            if (player_clear(0, (int16_t)S->g_5128)) {
                if (S->cam_y > 0x10 && wadd(S->cam_y, 0x50) > S->player_y) {
                    int16_t ny = wadd(S->cam_y, (int16_t)S->g_5128);
                    S->cam_y = (ny < 0x10) ? 0x10 : ny;
                }
            }
        } else {
            if (wadd(S->cam_y, 0x78) < S->player_y) {
                if (S->cam_y < 0xC8) S->cam_y = wadd(S->cam_y,
                                                     (int16_t)S->g_5128);
                else S->cam_y = 0xC8;
            }
        }
    }

    /* Mirror the physics position into the player object record so the
     * renderer draws the player at the correct place. */
    S->obj[1].x = S->player_x;
    S->obj[1].y = S->player_y;
}

/* ---------------------------------------------------------------- handlers
 * The original calls a handler with (y_new, x_new, object_index) on the
 * stack (retf 6).  C handlers receive the same values explicitly so they can
 * reproduce the original platform/player interaction.
 */

static int cell_solid_at(int32_t x, int32_t y) {
    return cell_solid_cur((int)(x / 16), (int)(y / 16));
}

/* 0x1F9A:0x1FB9 (kind 1): pure noop (retf 6). */
static void h_1fb9(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)o; (void)idx; (void)x; (void)y;
}

static void h_251d(ObjectEntry *o, int idx, int16_t x, int16_t y);
static void load_level(int level, int new_game, int fresh_image,
                       int sound_start);
void cc_warp_level(int level);
static void load_arg(int arg);

/* 0x1F9A:0x2850 (kind 2): ready gate around the slash update 0x251D. */
static void h_2850(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    if (o->ready == 0) h_251d(o, idx, x, y);
}

/* 0x1F9A:0x2877 (kind 3): transformed-object dissolve animation. */
static void h_2877(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    o->p18 = (uint16_t)(o->p18 + 1u);
    if (o->tile == 0x0D) {
        if (o->p18 > 0x10) o->shape = 0x19;
        if (o->p18 > 0x14) {
            o->p18 = 0;
            o->ready = 1;
            o->shape = 0x11;
        }
    } else {
        if (o->p18 > 8) {
            o->ready = 1;
            o->p18 = 0;
        }
    }
    if (o->x > 2 && o->x < 0x26C)
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    anim_next(o);
}

/* 0x1F9A:0x6FF6 (kind 32): moving hazard/platform.
 * Flips p1e at the x bounds; advances x on even p18 frames. */
static void h_6ff6(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    S->g_512a = (uint16_t)idx;

    if (!(o->x > 0x10 && o->x < 0x270)) {
        o->vx = (int16_t)(-o->vx);
        o->x = (int16_t)(o->x + (int32_t)o->vx * 2);
    }

    if (o->p18 < 0x7d00u) o->p18 = (uint16_t)(o->p18 + 1);
    else o->p18 = 0;

    if ((o->p18 & 1u) == 0u) {
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    }
}

/* 0x1F9A:0x70E4 (kind 33): partner mover.  It swaps its record with the
 * object last touched by 0x6FF6 at p18==3 and adjusts the partner shape. */
static void h_70e4(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    int16_t dx;
    int skip_flip;

    S->g_513a = (uint16_t)idx;
    anim_next(o);

    dx = (int16_t)(o->x - S->obj[S->g_512a].x);
    if (dx < 0) dx = (int16_t)-dx;

    if (dx > 0x23) {
        skip_flip = (o->p18 != 0 && o->x > 0x10 && o->x < 0x270);
    } else {
        skip_flip = (o->x > 0x10 && o->x < 0x270);
    }
    if (!skip_flip) {
        o->p18 = 1;
        o->vx = (int16_t)(-o->vx);
    }

    if (o->p18 != 0) {
        o->p18 = (uint16_t)(o->p18 + 1);
        if (o->p18 > 5) o->p18 = 0;
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);

        if (o->p18 == 3) {
            int other = (int)S->g_512a;
            ObjectEntry tmp = *o;
            *o = S->obj[other];
            S->obj[other] = tmp;

            {
                uint16_t old = S->g_513a;
                S->g_513a = S->g_512a;
                S->g_512a = old;
            }
            if ((int16_t)S->g_512a > (int16_t)S->g_513a) {
                S->obj[S->g_513a].shape++;
            } else {
                S->obj[S->g_513a].shape--;
            }
        }
    } else {
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    }
}

/* 0x1F9A:0x3F13 (kind 60): door/entrance animation. */
static void h_3f13(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    o->p18 = (uint16_t)(o->p18 + 1);
    if (o->p18 > 0x7d00) o->p18 = 0;
    if (o->p18 % 2 == 0) anim_next(o);
}

/* 0x1F9A:0x3D23 (kind 34): two-record door/lever animation. */
static void h_3d23(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    ObjectEntry *n = (idx + 1 < OBJ_MAX) ? &S->obj[idx + 1] : o;

    o->p18 = (uint16_t)(o->p18 + 1);
    if (o->p18 > 0x12c) {
        o->shape = 0x19;
        n->shape = 0x15;
        o->p10 = 0;
        n->p10 = 0;
        o->p18 = 0;
    }

    if (o->p18 % 3 == 0 && o->p18 < 0x51) {
        anim_next(o);
        anim_next(n);
    }

    if (o->p18 > 0x50) {
        o->shape = 0x32;
        n->shape = 0x32;
        o->p10 = 1;
        n->p10 = 1;
        mark_redraw(o->x, o->y);
        mark_redraw(n->x, n->y);
    }
}

/* 0x1F9A:0x6AFA (kind 19): horizontal moving platform. */
static void h_6afa(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    if (cell_solid_at(x + 3, y + 8) || cell_solid_at(x + 12, y + 8)) {
        o->vx = (int16_t)(-o->vx);
    }
    anim_next(o);

    if (S->player_y + 0x10 > o->y - 5) {
        if (S->player_y + 0x10 < o->y + 5 &&
            S->player_x + 8 > o->x - 4 &&
            S->player_x + 8 < o->x + 0x14 &&
            S->gravity_scale == 1 &&
            S->jump_phase > 0xA) {
            S->player_y = (int32_t)o->y - 0x10;
            S->g_513d = 1;
            o->p1a = 1;
            S->g_511a = (uint16_t)idx;
            goto move;
        }
    }

    if (S->g_513d) {
        if ((uint16_t)idx != S->g_511a) goto move;
    }
    S->g_513d = 0;
    if (o->p1a == 0) goto move;
    o->p1a = 0;
    S->g_5126 = 0;
    S->g_5118 = 0;

move:
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    if (o->p1a != 1) return;
    if (o->vx == 1) {
        S->g_5118 = o->speed;
        if (player_clear((int16_t)S->g_5118, 0)) {
            S->player_x += (int16_t)S->g_5118;
        }
    } else {
        S->g_5118 = (uint16_t)(-(int16_t)o->speed);
        if (player_clear((int16_t)S->g_5118, 0)) {
            S->player_x += (int16_t)S->g_5118;
        }
    }
}

/* 0x1F9A:0x6D31 (kind 20/0x27): vertical moving platform. */
static void h_6d31(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    if (cell_solid_at(x + 8, y - 0x14) || cell_solid_at(x + 8, y + 0x10)) {
        o->vy = (int16_t)(-o->vy);
    }
    anim_next(o);

    if (!S->g_5142) {
        if (S->player_y + 0x10 > o->y - 5 &&
            S->player_y + 0x10 < o->y + 5 &&
            S->player_x + 8 > o->x - 4 &&
            S->player_x + 8 < o->x + 0x14 &&
            S->gravity_scale == 1) {
            S->player_y = (int32_t)o->y - 0x10;
            S->g_5142 = 1;
            S->g_511c = (uint16_t)idx;
        }
    }

    if (S->player_y + 0x10 == o->y &&
        S->player_x + 8 > o->x - 4 &&
        S->player_x + 8 < o->x + 0x14 &&
        S->gravity_scale == 1) {
        S->g_5142 = 1;
        o->p1a = 1;
        S->g_511c = (uint16_t)idx;
        goto move;
    }

    if (!S->g_5142 || (uint16_t)idx == S->g_511c) {
        S->g_5142 = 0;
        o->p1a = 0;
        S->g_5128 = 0;
    }

move:
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    if (o->p1a != 1 || S->gravity_scale != 1) return;
    if (o->vy == 1) {
        S->g_5128 = o->speed;
        if (player_clear(0, (int16_t)S->g_5128)) {
            S->player_y += (int16_t)S->g_5128;
        }
    } else {
        S->g_5128 = (uint16_t)(-(int16_t)o->speed);
        if (player_clear(0, (int16_t)S->g_5128)) {
            S->player_y += (int16_t)S->g_5128;
        }
    }
}

/* 0x1F9A:0x831 (0x201D1): short-lived pickup/effect object. */
static void h_831(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    if (o->p18 > 0x10) o->ready = 1;
    o->p18 = (uint16_t)(o->p18 + 1);
    if ((int16_t)o->y > 0x12)
        o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
}

/* 0x59 SetCell state side: stamp the base cell at (xcell,ycell). */
static void base_cell_stamp(int xcell, int ycell) {
    int shape = S->g_5122;
    int cache = (shape == S->g_5122) ? 0 : 0xFFFF;
    if (shape == S->g_5122) {
        if (S->g_511e == 4) {
            if (ycell & 1) { shape += 4; cache += 2; }
            if (xcell & 1) { shape += 1; cache += 1; }
        } else if (S->g_511e == 6) {
            if (ycell & 1) { shape += 4; cache += 3; }
            if ((xcell % 3) == 0) { shape += 2; cache += 2; }
            else if ((xcell % 3) == 2) { shape += 1; cache += 1; }
        } else if (S->g_511e == 8) {
            if (ycell & 1) { shape += 4; cache += 4; }
            if ((xcell % 4) == 0) { shape += 3; cache += 3; }
            else if ((xcell % 4) == 3) { shape += 2; cache += 2; }
            else if ((xcell % 4) == 2) { shape += 1; cache += 1; }
        }
    }
    if (S->g_30d) cache = 0xFFFF;
    S->g_2f6 = (uint16_t)cache;
    cell_put(xcell, ycell, 0x00, 0);
    cell_put(xcell, ycell, 0x01, 0);
    cell_put(xcell, ycell, 0x02, cache & 0xFF);
    cell_put(xcell, ycell, 0x03, (cache >> 8) & 0xFF);
    cell_put(xcell, ycell, 0x04, shape & 0xFF);
    cell_put(xcell, ycell, 0x05, (shape >> 8) & 0xFF);
    cell_put(xcell, ycell, 0x06, 0);
    cell_put(xcell, ycell, 0x07, 0);
    cell_put(xcell, ycell, 0x08, S->g_2b8 & 0xFF);
    cell_put(xcell, ycell, 0x09, (S->g_2b8 >> 8) & 0xFF);
    cell_put(xcell, ycell, 0x0A, 0);
    cell_put(xcell, ycell, 0x0B, 0);
    cell_put(xcell, ycell, 0x0C, 0);
    cell_put(xcell, ycell, 0x0D, 0);
    cell_put(xcell, ycell, 0x0E, 0);
    cell_put(xcell, ycell, 0x0F, 0);
    cell_put(xcell, ycell, 0x10, 0);
    cell_put(xcell, ycell, 0x11, 0);
    cell_put(xcell, ycell, 0x12, 0);
    cell_put(xcell, ycell, 0x13, 0);
}

/* 0x1FFC8: walk the cursor back over reusable free slots. */
static void reuse_free_slot(void) {
    while (S->obj_cursor > 0) {
        ObjectEntry *o = &S->obj[S->obj_cursor];
        if (o->state != 2) break;
        if (!(o->kind == 3 || o->kind == 0x2A ||
              o->kind == 0x2D || o->kind == 0x2E)) break;
        S->obj_cursor--;
    }
}

/* 0x2002D: update the slot-3 template and copy it to the next slot. */
static void spawn_template_2002d(int mode) {
    ObjectEntry *t;
    reuse_free_slot();
    if (S->obj[3].state != 2 && S->obj_cursor + 1 < OBJ_MAX) {
        S->obj_cursor++;
        S->obj[S->obj_cursor] = S->obj[3];
    }
    t = &S->obj[3];
    t->x = S->obj[2].x;
    t->y = S->obj[2].y;
    t->x_prev = S->obj[2].x;
    t->y_prev = S->obj[2].y;
    t->vx = S->obj[2].vx;
    t->vy = -1;
    if (mode == 1) {
        t->shape = 0x11;
        t->p16 = 8;
        t->tile = 0x0D;
    } else {
        t->shape = 0x19;
        t->p16 = 9;
        t->tile = 7;
    }
    t->p10 = 1;
    t->p18 = 0;
    t->kind = 3;
    t->ready = 0;
    t->state = 0;
}

/* 0x47D: general object spawn (state side; drawing is renderer-only). */
typedef struct {
    uint16_t handler, handler_seg;
    uint16_t p18, speed, p16, p14, p12, vy, vx;
    uint16_t ycell, xcell, tile, kind;
    int slot;
} SpawnArgs;

static void spawn_object_47d(const SpawnArgs *a) {
    ObjectEntry *o;
    int16_t px = (int16_t)(a->xcell << 4);
    int16_t py = (int16_t)(a->ycell << 4);
    if (a->slot < 0 || a->slot >= OBJ_MAX) return;
    o = &S->obj[a->slot];
    o->shape = (a->vx == 1) ? a->p14 : a->p12;
    if (!S->g_a10a) base_cell_stamp(a->xcell, a->ycell);
    o->tile = a->tile;
    o->p14 = a->p14;
    o->p12 = a->p12;
    o->x = px;
    o->y = py;
    o->x_prev = px;
    o->y_prev = py;
    o->vx = (int16_t)a->vx;
    o->vy = (int16_t)a->vy;
    o->p10 = 1;
    o->p16 = a->p16;
    o->speed = a->speed;
    o->p18 = a->p18;
    o->p1a = 0;
    o->p02 = 0;
    o->p04 = 1;
    o->kind = a->kind;
    o->ready = 0;
    o->state = 0;
    o->handler = a->handler;
    o->handler_seg = a->handler_seg;
}

/* 0x20247: spawn the 0x831 effect object at cell (xcell,ycell). */
static void spawn_effect(int16_t shape, int16_t xcell, int16_t ycell) {
    int s;
    ObjectEntry *o;
    if (S->obj_cursor + 1 >= OBJ_MAX) return;
    s = ++S->obj_cursor;
    o = &S->obj[s];
    *o = (ObjectEntry){0};
    o->p04 = 0x7D00;
    o->x = (int16_t)(xcell * 16);
    o->y = (int16_t)(ycell * 16);
    o->x_prev = o->x;
    o->y_prev = o->y;
    o->tile = 0x12;
    o->p10 = 1;
    o->p12 = (uint16_t)shape;
    o->p14 = (uint16_t)shape;
    o->p16 = 1;
    o->p18 = 0;
    o->p1a = 0;
    o->shape = (uint16_t)shape;
    o->vx = 0;
    o->vy = -1;
    o->speed = 2;
    o->kind = 0x2a;
    o->ready = 0;
    o->state = 0;
    o->handler = 0x831;
    o->handler_seg = 0x1F9A;
}

/* 0x1F9A:0xA36 (0x203D6): apply a hit; loses one life/crystal unit. */
static void damage_life(void) {
    if ((int16_t)S->g_509e > 1) {
        S->g_a0ea = 0;
        S->g_509e--;
        S->obj[1].p02 = 1;
    } else if (S->g_5124 == 0) {
        S->g_5124 = 1;
        S->g_509e--;
    }
}

static int rect_overlap(int32_t ax0, int32_t ax1, int32_t bx0, int32_t bx1) {
    return ax0 <= bx1 && ax1 >= bx0;
}

/* 0x20406 slash matrices (ported in P2.4/P2.5). */
static void slash_state1(ObjectEntry *o, int idx, int16_t x, int16_t y);
static void slash_state2(ObjectEntry *o, int idx, int16_t x, int16_t y);

/* 0x2059A: transform an object into the kind-3 dissolve state. */
static void transform_object(ObjectEntry *o) {
    switch (o->kind) {
        case 9: case 7: case 0x0A: case 0x39: case 0x1A:
        case 5: case 0x35: case 0x30: case 0x17: case 0x23:
        case 0x1C: case 8: case 0x0B: case 0x0C:
            o->x = (int16_t)((o->x / 16) * 16);
            o->vx = 0;
            o->vy = 0;
            o->p18 = 0;
            o->p1a = 0;
            o->shape = 0x19;
            o->p16 = 9;
            o->tile = 7;
            o->p10 = 1;
            o->kind = 3;
            o->ready = 0;
            o->state = 0;
            o->handler = 0x2877;
            o->handler_seg = 0x1F9A;
            play_sound(6);
            break;
        default:
            break;
    }
}

/* 0x20707..0x2081A: slash-vs-object gate. */
static void slash_gate(ObjectEntry *o, int16_t x, int16_t y) {
    ObjectEntry *a = &S->obj[2];
    int kind;

    if (!rect_overlap(a->x + 3, a->x + 12, x + 3, x + 12)) return;
    if (!rect_overlap(a->y, a->y + 15, y, y + 15)) return;
    if (a->ready != 0) return;

    if (S->g_5140 == 0 && S->g_513c != 0 && o->kind == 0x24) {
        o->p10 = 1;
        o->p16 = 1;
        S->g_5124 = 2;
    }

    o->p04 = (uint16_t)(o->p04 - 1u);
    a->ready = 1;
    kind = o->kind;
    if (kind == 0x15 || kind == 0x1D || kind == 0x12 ||
        kind == 0x37 || kind == 0x19)
        a->ready = 0;

    if (a->p1a == 1) slash_state1(o, (int)(o - S->obj), x, y);
    else slash_state2(o, (int)(o - S->obj), x, y);
}

static int cell_border_byte(int col, int row, int off);
static void cell_border_put(int col, int row, int off, int value);

static ObjectEntry *obj_safe(int i) {
    return (i >= 0 && i < OBJ_MAX) ? &S->obj[i] : 0;
}

/* 0x20824..0x20F47: normal slash hit matrix. */
static void slash_state1(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int kind = o->kind;
    int side;
    ObjectEntry *p;
    int col, row;

    if ((int16_t)o->p04 >= 1) {
        int k = o->kind;
        if (k == 0x24 || k == 0x16 || k == 0x1D || k == 0x15 ||
            k == 0x0E || k == 0x10 || k == 0x19)
            return;
        if (k == 0x2F) {
            p = obj_safe(idx + 1);
            side = (p && p->kind == 0x2F) ? 1 : -1;
            p = obj_safe(idx + side);
            if (p) {
                p->p04 = (uint16_t)(p->p04 - 1u);
                p->p02 = 1;
            }
            o->p02 = 1;
            if (o->vx == S->obj[2].vx) {
                o->vx = (int16_t)-o->vx;
                anim_select_shape(o);
                if (p) {
                    p->vx = (int16_t)-p->vx;
                    anim_select_shape(p);
                }
            }
            return;
        }
        if (k == 0x30) {
            o->p02 = 1;
            if ((p = obj_safe(idx + 1))) p->p02 = 1;
            if ((p = obj_safe(idx + 2))) p->p02 = 1;
            if (o->vx == S->obj[2].vx) {
                o->vx = (int16_t)-o->vx;
                if ((p = obj_safe(idx + 1))) {
                    p->vx = (int16_t)-p->vx;
                    anim_select_shape(p);
                }
                if ((p = obj_safe(idx + 2))) {
                    p->vx = (int16_t)-p->vx;
                    anim_select_shape(p);
                }
            }
            return;
        }
        if (k == 0x39) {
            if (o->shape == 0x2D) o->p02 = 1;
            else o->p04 = (uint16_t)(o->p04 + 1u);
            if (o->vx == S->obj[2].vx) o->vx = (int16_t)-o->vx;
            return;
        }
        if (k == 0x35) {
            if (S->obj[2].vx == 1) {
                p = obj_safe(idx - 1);
                if (p && p->shape == 9) {
                    p->p04 = (uint16_t)(p->p04 - 1u);
                    if ((int16_t)p->p04 < 1) p->shape = 0x15;
                    else p->p02 = 1;
                }
            } else {
                p = obj_safe(idx + 1);
                if (p && p->shape == 0x11) {
                    p->p04 = (uint16_t)(p->p04 - 1u);
                    if ((int16_t)p->p04 < 1) p->shape = 0x19;
                    else p->p02 = 1;
                }
            }
            if ((obj_safe(idx + 1) && (int16_t)obj_safe(idx + 1)->p04 > 0) ||
                (obj_safe(idx - 1) && (int16_t)obj_safe(idx - 1)->p04 > 0))
                o->p04 = (uint16_t)(o->p04 + 1u);
            else
                o->p02 = 1;
            if (o->vx == S->obj[2].vx) {
                o->vx = (int16_t)-o->vx;
                if ((p = obj_safe(idx + 1))) p->vx = (int16_t)-p->vx;
                if ((p = obj_safe(idx - 1))) p->vx = (int16_t)-p->vx;
            }
            return;
        }
        o->p02 = 1;
        return;
    }

    play_sound(6);
    o->ready = 1;

    if (kind == 0x1A || kind == 0x23 || kind == 0x17 || kind == 0x0C ||
        kind == 0x32 || kind == 0x36 || kind == 0x3A || kind == 0x31) {
        spawn_template_2002d(2);
        score_add(100, 0);
        return;
    }
    if (kind == 0x2F) {
        p = obj_safe(idx + 1);
        side = (p && p->kind == 0x2F) ? 1 : -1;
        p = obj_safe(idx + side);
        if (p) p->ready = 1;
        spawn_template_2002d(1);
        if (p) {
            spawn_effect(0x1C, (int16_t)((p->x + 8) / 16),
                         (int16_t)(p->y / 16));
        }
        score_add(0x1388, 0);
        return;
    }
    if (kind == 0x09 || kind == 0x05) {
        col = o->x / 16 + 1;
        row = o->y / 16 + 1;
        if (cell_border_byte(col, row + 1, 0x11) == 0 &&
            cell_border_byte(col, row + 1, 0x12) == 0)
            col--;
        cell_border_put(col, row, 0x00, 0x9F);
        cell_border_put(col, row, 0x01, 0xFF);
        cell_border_put(col, row, 0x00, 0x9F);
        cell_border_put(col, row, 0x01, 0xFF);
        if (cell_border_byte(col, row, 0x0A) == 0 &&
            cell_border_byte(col, row, 0x0B) == 0) {
            cell_border_put(col, row, 0x0A, o->tile & 0xFF);
            cell_border_put(col, row, 0x0B, (o->tile >> 8) & 0xFF);
            cell_border_put(col, row, 0x06, kind == 0x09 ? 0x31 : 0x18);
            cell_border_put(col, row, 0x07, 0);
        } else {
            cell_border_put(col, row, 0x0C, o->tile & 0xFF);
            cell_border_put(col, row, 0x0D, (o->tile >> 8) & 0xFF);
            cell_border_put(col, row, 0x0E, kind == 0x09 ? 0x31 : 0x18);
            cell_border_put(col, row, 0x0F, 0);
        }
        mark_redraw((int16_t)(col * 16), (int16_t)(row * 16));
        spawn_template_2002d(kind == 0x09 ? 1 : 2);
        score_add(100, 0);
        return;
    }
    if (kind == 0x30) {
        if ((p = obj_safe(idx + 1))) p->ready = 1;
        if ((p = obj_safe(idx + 2))) p->ready = 1;
        spawn_template_2002d(1);
        if ((p = obj_safe(idx + 1)))
            spawn_effect(0x1C, (int16_t)((p->x + 8) / 16),
                         (int16_t)(p->y / 16));
        score_add(0x1388, 0);
        return;
    }
    if (kind == 0x39) {
        if (o->shape == 0x2D) {
            o->ready = 1;
            if ((p = obj_safe(idx - 2))) p->ready = 1;
            spawn_template_2002d(2);
            spawn_effect(0x1C, (int16_t)((o->x + 8) / 16),
                         (int16_t)(o->y / 16));
            score_add(0x1388, 0);
        } else {
            o->p04 = (uint16_t)(o->p04 + 1u);
            o->ready = 0;
            if (o->vx == S->obj[2].vx) o->vx = (int16_t)-o->vx;
        }
        return;
    }
    if (kind == 0x35) {
        if ((p = obj_safe(idx + 1))) p->ready = 1;
        if ((p = obj_safe(idx - 1))) p->ready = 1;
        o->ready = 1;
        spawn_template_2002d(1);
        spawn_effect(0x1C, (int16_t)((o->x + 8) / 16),
                     (int16_t)(o->y / 16));
        score_add(0x1388, 0);
        return;
    }
    if (kind == 0x33) {
        if (o->p14 == 0x2F) {
            p = obj_safe(idx + 1);
            if (p && p->kind == 0x33) {
                p->p14 = 0x2F;
                p->p12 = 0x25;
            }
            if (p) anim_select_shape(p);
            spawn_effect(0x18, (int16_t)((o->x + 8) / 16),
                         (int16_t)((o->y + 8) / 16));
            score_add(0x3E8, 0);
            spawn_template_2002d(1);
        } else {
            o->ready = 0;
            o->p02 = 1;
        }
        return;
    }
    if (kind == 0x2C) {
        play_sound(0x0D);
        col = o->x / 16 + 1;
        row = o->y / 16 + 1;
        {
            int v = S->g_512c + 9;
            cell_border_put(col, row, 0x0E, v & 0xFF);
            cell_border_put(col, row, 0x0F, (v >> 8) & 0xFF);
        }
        cell_border_put(col, row, 0x0C, 9);
        cell_border_put(col, row, 0x0D, 0);
        S->g_512c = (uint16_t)(S->g_512c + 1u);
        if (S->g_512c > 4) S->g_512c = 0;
        return;
    }

    spawn_template_2002d(1);
    score_add(100, 0);
}

/* 0x21342..0x21775: power slash hit matrix. */
static void slash_state2(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int kind = o->kind;
    int side;
    ObjectEntry *p;
    int col, row;
    (void)x; (void)y;

    if (kind == 0x16 || kind == 0x0E || kind == 0x24 || kind == 0x1D ||
        kind == 0x15 || kind == 0x10 || kind == 0x37 || kind == 0x12 ||
        kind == 0x19 || kind == 4)
        return;

    if (kind == 0x2C) {
        play_sound(0x0D);
        o->ready = 1;
        col = o->x / 16 + 1;
        row = o->y / 16 + 1;
        {
            int v = S->g_512c + 9;
            cell_border_put(col, row, 0x0E, v & 0xFF);
            cell_border_put(col, row, 0x0F, (v >> 8) & 0xFF);
        }
        cell_border_put(col, row, 0x0C, 9);
        cell_border_put(col, row, 0x0D, 0);
        S->g_512c = (uint16_t)(S->g_512c + 1u);
        if (S->g_512c > 4) S->g_512c = 0;
        return;
    }
    if (kind == 0x35) {
        if ((p = obj_safe(idx + 1))) p->ready = 1;
        if ((p = obj_safe(idx - 1))) p->ready = 1;
        o->ready = 1;
        spawn_template_2002d(2);
        spawn_effect(0x1C, (int16_t)((o->x + 8) / 16),
                     (int16_t)(o->y / 16));
        score_add(0x1388, 0);
        play_sound(6);
        return;
    }
    if (kind == 0x2F) {
        p = obj_safe(idx + 1);
        side = (p && p->kind == 0x2F) ? 1 : -1;
        p = obj_safe(idx + side);
        o->ready = 1;
        if (p) p->ready = 1;
        spawn_template_2002d(2);
        if (p)
            spawn_effect(0x1C, (int16_t)((p->x + 8) / 16),
                         (int16_t)(p->y / 16));
        score_add(0x1388, 0);
        play_sound(6);
        return;
    }
    if (kind == 0x33) {
        if (o->p14 == 0x2F) {
            p = obj_safe(idx + 1);
            if (p && p->kind == 0x33) {
                p->p14 = 0x2F;
                p->p12 = 0x25;
            }
            if (p) anim_select_shape(p);
            o->ready = 1;
            spawn_template_2002d(2);
            spawn_effect(0x18, (int16_t)((o->x + 8) / 16),
                         (int16_t)((o->y + 8) / 16));
            score_add(0x3E8, 0);
        } else {
            o->ready = 0;
            o->p02 = 1;
        }
        play_sound(6);
        return;
    }
    if (kind == 0x30) {
        o->ready = 1;
        if ((p = obj_safe(idx + 1))) p->ready = 1;
        if ((p = obj_safe(idx + 2))) p->ready = 1;
        spawn_template_2002d(2);
        if ((p = obj_safe(idx + 1)))
            spawn_effect(0x1C, (int16_t)((p->x + 8) / 16),
                         (int16_t)(p->y / 16));
        score_add(0x1388, 0);
        play_sound(6);
        return;
    }
    if (kind == 0x39) {
        o->ready = 1;
        if ((p = obj_safe(idx - 2))) p->ready = 1;
        spawn_template_2002d(2);
        spawn_effect(0x1C, (int16_t)((o->x + 8) / 16),
                     (int16_t)(o->y / 16));
        score_add(0x1388, 0);
        play_sound(6);
        return;
    }
    o->ready = 1;
    spawn_template_2002d(2);
    score_add(100, 0);
    play_sound(6);
}

/* 0x20406: player/object interaction. */
static void player_interact(ObjectEntry *o, int16_t x, int16_t y) {
    int kind;

    if (!rect_overlap(S->player_x + 3, S->player_x + 12, x + 3, x + 12) ||
        !rect_overlap(S->player_y, S->player_y + 15, y, y + 15))
        goto slash_gate;

    if (S->g_513c == 0) {
        transform_object(o);
        goto slash_gate;
    }
    if (S->g_5140 != 0) goto slash_gate;
    if (S->g_5124 != 0) goto slash_gate;
    if (S->g_a0ea != 0xFFFF) goto slash_gate;
    if (S->g_a0f6 != 0) goto slash_gate;

    damage_life();
    kind = o->kind;
    switch (kind) {
        case 0x10: case 0x0E:
            S->g_a0ea = 0xFFFF;
            S->g_5124 = 1;
            S->obj[1].p02 = 0;
            break;
        case 0x24:
            S->g_5124 = 0;
            S->g_a0ea = 0xFFFF;
            S->g_509e++;
            S->obj[1].p02 = 0;
            break;
        case 0x16:
            if (S->g_5124 == 1)
                S->g_5124 = (S->gravity_scale == 1) ? 3 : 1;
            break;
        case 0x2C:
            S->g_5124 = 0;
            S->g_a0ea = 0xFFFF;
            S->g_509e++;
            S->obj[1].p02 = 0;
            o->ready = 1;
            spawn_effect(0x18, (int16_t)(x / 16), (int16_t)(y / 16));
            play_sound(8);
            score_add(1000, 0);
            break;
        default:
            break;
    }
    if ((int16_t)S->g_5124 > 0) play_sound(5);

slash_gate:
    slash_gate(o, x, y);
}

/* 0x1F9A:0x3E70 (kind 36): two-frame ambient animation. */
static void h_3e70(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    o->p18 = (uint16_t)(o->p18 + 1);
    if (o->p18 > 0x7d00) o->p18 = 0;
    if (o->p18 % 2 == 0) anim_next(o);
    player_interact(o, x, y);
    player_interact(o, x, (int16_t)(y + 0x10));
}

/* 0x1F9A:0x4A4B (kind 31): two-frame ambient animation. */
static void h_4a4b(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    o->p18 = (uint16_t)(o->p18 + 1);
    if (o->p18 % 2 == 0) anim_next(o);
}

/* 0x1F9A:0x4A82 (kind 44): interaction helper with state toggles. */
static void h_4a82(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    uint8_t old_513c = S->g_513c;
    uint8_t old_5140 = (uint8_t)S->g_5140;
    S->g_513c = 1;
    S->g_5140 = 0;
    player_interact(o, x, y);
    S->g_513c = old_513c;
    S->g_5140 = old_5140;
}

/* 0x1F9A:0x1FC2 (kind 8): proximity-triggered companion spawner/mover. */
static void h_1fc2(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int16_t r;
    anim_next(o);

    r = proximity_mode2(x, y, 0x70);
    if (r == 1 && idx > 0) {
        ObjectEntry *c = &S->obj[idx - 1];
        if (c->ready != 0) {
            play_sound(0x10);
            c->ready = 0;
            c->state = 0;
            c->x = o->x;
            c->y = (int16_t)(o->y + 8);
        }
    }

    if (cell_solid_at(x + 8, y + 3) || cell_solid_at(x + 8, y + 0xc)) {
        o->vy = (int16_t)(-o->vy);
        o->shape = (o->vy == 1) ? 0x19 : 0x21;
    }
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    player_interact(o, o->x, o->y);
}

/* 0x1F9A:0x323C (kind 24): spawns a companion when the player is below. */
static void h_323c(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    if (o->y > S->player_y) return;
    if (idx + 1 < OBJ_MAX) {
        ObjectEntry *n = &S->obj[idx + 1];
        if (n->state == 2) anim_next(o);
        if (o->p10 == 6) {
            n->state = 0;
            n->ready = 0;
            n->x = o->x;
            n->y = (int16_t)(o->y + 8);
            o->p10 = 1;
        }
    }
}

/* 0x1F9A:0x2176 (kind 52): vertical mover / map companion. */
static void h_2176(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int solid;
    (void)idx;
    solid = cell_solid_at(x + 8, y + 3) || cell_solid_at(x + 8, y + 0xc);
    if (solid) o->ready = 1;
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    player_interact(o, o->x, o->y);
}

/* 0x1FD4B: is this cell part of the level's collectible/base tile set? */
static int cell_type_collectible(int col, int row) {
    int shape = cell_byte_cur(col, row, 0x04) |
                (cell_byte_cur(col, row, 0x05) << 8);
    int base = S->g_5122;
    switch (S->g_511e) {
        case 4:
            if (cell_byte_cur(col, row, 0x11) != 0) return 0;
            return shape == base || shape == base + 1 ||
                   shape == base + 4 || shape == base + 5;
        case 1:
            return shape == base;
        case 6:
            if (cell_byte_cur(col, row, 0x11) != 0) return 0;
            return shape == base || shape == base + 1 || shape == base + 2 ||
                   shape == base + 4 || shape == base + 5 || shape == base + 6;
        case 8:
            if (cell_byte_cur(col, row, 0x11) != 0) return 0;
            return shape >= base && shape <= base + 7;
        default:
            return 0;
    }
}

/* 0x200FE: terrain/cell test used by movers and slash paths. */
static int cell_test_200fe(int x, int y) {
    if (cell_byte_cur((x + 3) / 16, (y + 8) / 16, 0x11) != 0) return 1;
    if (cell_byte_cur((x + 0xC) / 16, (y + 8) / 16, 0x11) != 0) return 1;
    if (cell_type_collectible((x + 4) / 16, (y + 0x12) / 16) &&
        cell_byte_cur((x + 8) / 16, (y + 0x12) / 16, 0x12) == 0)
        return 1;
    return 0;
}

/* 0x1F9A:0x1F58: shove the player by `amount` in `dir` (1=left, else
 * right) if the cell immediately beside them is clear. */
static void push_player(int16_t amount, int16_t dir) {
    if (dir == 1) {
        if (!cell_test_200fe((int16_t)(S->player_x - 3), S->player_y)) {
            S->player_x = (int16_t)(S->player_x - amount);
            S->obj[1].x = S->player_x;
            S->g_5126 = (uint16_t)amount;
            S->g_5118 = (uint16_t)amount;
        }
    } else {
        if (!cell_test_200fe((int16_t)(S->player_x + 3), S->player_y)) {
            S->g_5126 = (uint16_t)(-(int16_t)amount);
            S->g_5118 = (uint16_t)(-(int16_t)amount);
            S->player_x = (int16_t)(S->player_x + amount);
            S->obj[1].x = S->player_x;
        }
    }
}

/* Turbo Pascal 32-bit multiply/divide helpers (0x2BB5:0x0279/0x0294). */
static int32_t tp_long_mul(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a * (uint32_t)b);
}
static int32_t tp_long_div(int32_t a, int32_t b) {
    if (b == 0) return 0;
    return (int32_t)(a / b);
}

/* 0x1F9A:0x1E74 helper: proximity test against the player using the object
 * record's current x/y.  mode 1: |dy| < r1 and |dx| < r2; mode 2:
 * |dx| < 8 and |dy| < r1.  Returns -1/1 for direction, 0 for no match. */
static int16_t proximity_obj_dir(int mode, int16_t r1, int16_t r2, int slot) {
    ObjectEntry *p;
    int16_t d;
    if (slot < 0 || slot >= OBJ_MAX) return 0;
    p = &S->obj[slot];
    if (mode == 1) {
        d = (int16_t)(p->y - S->player_y);
        if (d < 0) d = (int16_t)-d;
        if (d >= r1) return 0;
        d = (int16_t)(p->x - S->player_x);
        if (d < 0) d = (int16_t)-d;
        if (d >= r2) return 0;
        return ((int16_t)(p->x - S->player_x) > 0) ? -1 : 1;
    }
    if (mode == 2) {
        d = (int16_t)(p->x - S->player_x);
        if (d < 0) d = (int16_t)-d;
        if (d >= 8) return 0;
        d = (int16_t)(p->y - S->player_y);
        if (d < 0) d = (int16_t)-d;
        if (d >= r1) return 0;
        return ((int16_t)(p->y - S->player_y) > 0) ? -1 : 1;
    }
    return 0;
}

/* 0x1F9A:0x5855 (kind 48): three-record moving group. */
static void h_5855(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *n1 = (idx + 1 < OBJ_MAX) ? &S->obj[idx + 1] : o;
    ObjectEntry *n2 = (idx + 2 < OBJ_MAX) ? &S->obj[idx + 2] : o;
    int16_t x2 = (int16_t)(n2->x + (int32_t)n2->vx * n2->speed);
    int16_t y2 = (int16_t)(n2->y + (int32_t)n2->vy * n2->speed);
    int flip = 0;

    o->p18 = (uint16_t)(o->p18 + 1);
    if (o->p18 % 2 == 0) {
        anim_next(o);
        anim_next(n1);
        anim_next(n2);
    }

    /* 0x1F9A:0x595C: if the player is close in the travel direction and the
     * previous slot is active, Random(100) < 5 respawns it at the middle
     * record's position. */
    if (idx > 0) {
        int16_t dir = proximity_obj_dir(1, 6, 0x130, idx);
        ObjectEntry *prev = &S->obj[idx - 1];
        if (dir == o->vx && prev->ready != 0) {
            if (cc_rng_next(100) < 5) {
                play_sound(0x10);
                prev->vx = dir;
                prev->ready = 0;
                prev->state = 0;
                prev->x = n1->x;
                prev->y = n1->y;
            }
        }
    }

    if (o->vx == -1) {
        if (cell_test_200fe(x, y)) flip = 1;
    } else if (o->vx == 1) {
        if (cell_test_200fe(x2, y2)) flip = 1;
    }
    if (flip) {
        o->vx = (int16_t)-o->vx;
        anim_select_shape(o);
        n1->vx = (int16_t)-n1->vx;
        anim_select_shape(n1);
        n2->vx = (int16_t)-n2->vx;
        anim_select_shape(n2);
    }

    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    n1->x = (int16_t)(n1->x + (int32_t)n1->vx * n1->speed);
    n2->x = (int16_t)(n2->x + (int32_t)n2->vx * n2->speed);
    player_interact(o, o->x, o->y);
    player_interact(n1, n1->x, n1->y);
    player_interact(n2, n2->x, n2->y);
}

/* 0x1F9A:0x5BBE (kind 29/49/50/54/58): animated mover. */
static void h_5bbe(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    anim_next(o);
    if (cell_solid_at(x, y + 9) || cell_solid_at(x + 0xf, y + 9)) {
        o->ready = 1;
        return;
    }
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, o->x, o->y);
}

/* 0x1F9A:0x3BE8 (kind 21): expanding/contracting animator. */
static void h_3be8(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    o->p18 = (uint16_t)(o->p18 + 1);
    if (o->p18 == 0x19) o->p1a = 1;
    if (o->p1a == 0) return;

    if (o->p10 > 6) {
        o->shape = (uint16_t)(o->shape + 3);
        o->p16 = (uint16_t)(o->p16 - 3);
        o->p10 = 1;
    }
    if (o->p18 % 2 == 0) anim_next(o);
    o->p1a = (uint16_t)(o->p1a + 1);
    if (o->p1a > 0x3c) {
        o->p18 = 0;
        o->p1a = 0;
        o->shape = (uint16_t)(o->shape - 3);
        o->p16 = (uint16_t)(o->p16 + 3);
        o->p10 = 1;
    }
    player_interact(o, o->x, o->y);
}

/* Clear a dug cell: +0x06/+0x10/+0x11 and w0=0xFFA1, then redraw/sound. */
static void dig_cell(int col, int row) {
    if (cell_byte_cur(col, row, 0x10) == 0) return;
    cell_put(col, row, 0x06, 0);
    cell_put(col, row, 0x11, 0);
    cell_put(col, row, 0x10, 0);
    cell_put(col, row, 0x00, 0xA1);
    cell_put(col, row, 0x01, 0xFF);
    mark_redraw((int16_t)(col * 16), (int16_t)(row * 16));
    play_sound(0x11);
}

/* 0x1F9A:0x251D: slot-2 slash update (also the a0f8 direct path for kind 2). */
static void h_251d(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    /* Handler args arrive as (y_new, x_new); the slash hitbox tests the
     * bottom edge cells (x, y+9) and (x+0xF, y+9). */
    int col = x / 16;
    int row = (y + 9) / 16;
    int col2 = (x + 0xF) / 16;
    int row2 = (y + 9) / 16;
    (void)idx;

    o->p18 = (uint16_t)(o->p18 + 1);
    anim_next(o);

    if (cell_byte_cur(col, row, 0x11) != 0 ||
        cell_byte_cur(col2, row2, 0x11) != 0) {
        o->kind = 0x2B;
        o->handler = 0x2241;
        o->handler_seg = 0x1F9A;
        o->p18 = 0;
        dig_cell(col, row);
        dig_cell(col2, row2);
        return;
    }

    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    if (o->p1a == 1 && o->p18 > 6 && o->speed < 0xE)
        o->speed = (uint16_t)(o->speed + 2);
}

/* Border-indexed cell helpers: the original's col/row already include the
 * +1 border, so logical = raw - 1. */
static int cell_border_byte(int col, int row, int off) {
    return cell_byte_cur(col - 1, row - 1, off);
}
static void cell_border_put(int col, int row, int off, int value) {
    cell_put(col - 1, row - 1, off, value);
}
static int cell_w0_is_dig(int col, int row) {
    return cell_border_byte(col, row, 0x00) == 0xA1 &&
           cell_border_byte(col, row, 0x01) == 0xFF;
}

/* 0x1F9A:0x2241: dug-block placement and animation. */

static void h_2241(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    if (o->p18 == 0) {
        int col = o->x / 16 + 1;
        int row = (o->y + 8) / 16 + 1;
        int i = 0;

        play_sound(0x1C);
        o->tile = 1;
        o->shape = 0x1D;
        o->p14 = 0x1D;
        o->p12 = 0x1D;
        o->p10 = 0;
        o->p16 = 4;
        o->p18 = 1;

        if (o->vx == -1) {
            for (;;) {
                if (cell_border_byte(col, row, 0x11) != 0) break;
                if (cell_w0_is_dig(col + 1, row)) break;
                if (i >= 2) break;
                col--;
                i++;
            }
            o->x = (int16_t)(col * 16);
            if (cell_w0_is_dig(col + 1, row)) {
                cell_border_put(col + 1, row, 0x00, 0);
                cell_border_put(col + 1, row, 0x01, 0);
            }
            o->vx = 0;
        } else if (o->vx == 1) {
            for (;;) {
                if (cell_border_byte(col, row, 0x11) != 0) break;
                if (cell_w0_is_dig(col - 1, row)) break;
                if (i >= 3) break;
                col++;
                i++;
            }
            o->x = (int16_t)((col - 2) * 16);
            if (cell_w0_is_dig(col - 1, row)) {
                cell_border_put(col - 1, row, 0x00, 0);
                cell_border_put(col - 1, row, 0x01, 0);
            }
            o->vx = 0;
        }
    }

    if (o->p18 == 1) {
        if (o->p10 < o->p16) o->p10 = (uint16_t)(o->p10 + 1u);
        else o->p18 = (uint16_t)(o->p18 + 1u);
    } else if (o->p18 == 2) {
        if (o->p10 > 1) o->p10 = (uint16_t)(o->p10 - 1u);
        else o->ready = 1;
    }
}

typedef void (*handler_fn)(ObjectEntry *, int, int16_t, int16_t);

/* ---------------------------------------------------------------- cells
 * 0x0FC5:0xBB26 (linear 0x1B776): per-cell event handler.  The original is
 * called by 0xE5D3 for several player-relative points each frame.  All cell
 * fields are words except the solid/exit flags at +0x11/+0x13. */
static void helper_a110_clear(void) { S->g_a110 = 0; }
static void helper_a59d(void) { S->g_a110 = 0; S->g_3395 = 0; }
static void helper_toggle_gravity(void) {
    if (S->gravity_scale == -1) {
        S->gravity_scale = 1;
        S->player_block = 6;
    } else {
        S->gravity_scale = -1;
        S->player_block = 8;
    }
    S->g_3392 = 0;
    S->airborne = 1;
    S->obj[1].tile = S->player_block;
}
static void helper_toggle_kind(uint16_t kind) {
    int i, end = S->obj_cursor;
    if (end >= OBJ_MAX) end = OBJ_MAX - 1;
    if (kind == 0x27 || kind == 0x3B || kind == 0x26) {
        for (i = 4; i <= end; i++) {
            ObjectEntry *o = &S->obj[i];
            if (o->kind != kind) continue;
            o->state = (o->state == 0) ? 2 : 0;
        }
    } else if (kind == 0x28) {
        for (i = 4; i <= end; i++) {
            ObjectEntry *o = &S->obj[i];
            if (o->kind != kind) continue;
            o->state = (o->state == 3) ? 0 : 3;
        }
    }
}
static void switch_pair(uint16_t col1, uint16_t row1, int shape1, int shape2) {
    cell_word_raw_put(col1, row1, 0x0E, shape1);
    cell_word_raw_put(col1, row1 + 1, 0x0E, shape2);
    cell_word_raw_put(col1, row1, 0x06, 0);
    cell_word_raw_put(col1, row1 + 1, 0x06, 0);
    cell_word_raw_put(col1, row1, 0x0C, 0x16);
    cell_word_raw_put(col1, row1 + 1, 0x0C, 0x16);
    cell_put((int)col1 - 1, (int)row1 - 1, 0x11, 0);
    cell_put((int)col1 - 1, (int)row1, 0x11, 0);
    mark_redraw((int16_t)(((int)col1 - 1) * 16), (int16_t)(((int)row1 - 1) * 16));
    mark_redraw((int16_t)(((int)col1 - 1) * 16), (int16_t)((int)row1 * 16));
}
static void cell_event(int16_t x, int16_t y) {
    int col = (int)x / 16;
    int row = (int)y / 16;
    int w0 = cell_word_cur(col, row, 0x00);
    int w3 = cell_word_cur(col, row, 0x06);
    int w5 = cell_word_cur(col, row, 0x0A);
    int w6 = cell_word_cur(col, row, 0x0C);
    int w7 = cell_word_cur(col, row, 0x0E);

    if (w7 != 0) {
        if (w7 == 0x31 && w6 == 6) {
            play_sound(9);
            S->g_509c = (uint16_t)(S->g_509c + 5);
            cell_word_put(col, row, 0x0E, 0);
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 0x32 && w6 == 6) {
            play_sound(9);
            if (S->g_a104) {
                S->g_a104 = 0;
                helper_a110_clear();
                cc_shell_notify_power_shot();
            }
            S->power_shot = 1;
            cell_word_put(col, row, 0x0E, 0);
            mark_redraw(x, y);
            S->power_timer = S->g_a0c2;
            S->g_a0c6 = 0;
            goto phase2;
        }
        if (w7 == 0x26 && w6 == 0x0C) {
            score_add(0x32, 0);
            cell_word_put(col, row, 0x0E, 0);
            mark_redraw(x, y);
            helper_toggle_gravity();
            play_sound(0x13);
            if (S->g_a102) {
                helper_a110_clear();
                cc_shell_notify_gravity();
                S->g_a102 = 0;
            }
            S->g_a100 = 1;
            if (!S->g_513c && !S->g_5140) S->g_513c = 1;
            S->power_timer = S->g_a0c8;
            S->g_a0c6 = 0;
            goto phase2;
        }
        if (w6 == 0x0D && (w7 == 1 || w7 == 2 || w7 == 3 || w7 == 4)) {
            S->g_a0cc = (uint16_t)(S->g_a0cc - 1);
            play_sound(S->g_a0cc == 0 ? 0x20 : 8);
            score_add(0x32, 0);
            cell_word_put(col, row, 0x0E, 0);
            mark_redraw(x, y);
            S->g_50a2 = (uint16_t)(S->g_50a2 + 1);
            goto phase2;
        }
        if (w7 == 0x0B && w6 == 1) {
            spawn_effect(0x17, (int16_t)col, (int16_t)row);
            play_sound(9);
            score_add(0x320, 0);
            cell_word_put(col, row, 0x0E, 0);
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 4 && w6 == 1) {
            spawn_effect(0x18, (int16_t)col, (int16_t)row);
            score_add(0x3E8, 0);
            cell_word_put(col, row, 0x0E, 0);
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 9 && w6 == 1) {
            spawn_effect(0x1C, (int16_t)col, (int16_t)row);
            play_sound(9);
            score_add(0x1388, 0);
            cell_word_put(col, row, 0x0E, 0);
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 0x2C && w6 == 0x0D) {
            play_sound(9);
            cell_word_put(col, row, 0x0E, 0);
            mark_redraw(x, y);
            S->g_a0f9 = 1;
            goto phase2;
        }
        if (w7 >= 9 && w7 <= 0x0D && w6 == 9) {
            if (w7 == 0x0D) {
                spawn_effect(0x1F, (int16_t)col, (int16_t)row);
                score_add(0x2710, 0);
                play_sound(0x1B);
            } else {
                spawn_effect(0x10, (int16_t)col, (int16_t)row);
                play_sound(9);
                score_add(0x64, 0);
            }
            cell_word_put(col, row, 0x0E, 0);
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 0x31 && w6 == 0x0D) {
            spawn_effect(0x18, (int16_t)col, (int16_t)row);
            play_sound(0x16);
            score_add(0x3E8, 0);
            cell_word_put(col, row, 0x0E, 0);
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 0x2D && w6 == 0x0D) {
            if (S->g_513c) {
                S->g_a10d = 1;
                S->g_513c = 0;
                S->power_timer = S->g_a0ca;
                S->g_a0c6 = 0;
            }
            play_sound(0x16);
            cell_word_put(col, row, 0x0E, 0);
            mark_redraw(x, y);
            helper_a110_clear();
            cc_shell_notify_red_mushroom();
            goto phase2;
        }
        if (w7 == 0x2E && w6 == 0x0D) {
            play_sound(0x1D);
            cell_word_put(col, row, 0x0E, 0);
            mark_redraw(x, y);
            if (S->g_a105) S->g_a105 = 0;
            if (S->g_513c && !S->g_5140) {
                S->g_5124 = 1;
                cc_shell_notify_poison();
            }
            goto phase2;
        }
        if (w7 >= 0x25 && w7 <= 0x27 && w6 == 1) {
            S->g_a10a = 1;
            spawn_effect(0x1C, (int16_t)col, (int16_t)row);
            S->g_a10a = 0;
            play_sound(9);
            score_add(0x1388, 0);
            cell_word_put(col, row, 0x0E, 0);
            S->g_a0d0 = 0xFFFF;
            S->g_a0d2 = 0xFFFF;
            S->g_a10b = 0;
            S->g_a0ce = 0;
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 0x19 && w6 == 0x0E) {
            play_sound(0x12);
            S->a0f8 = 1;
            cell_word_put(col, row, 0x0E, 0);
            S->power_timer = S->g_a0e4;
            S->g_a0c6 = 0;
            if (S->g_a101) {
                helper_a110_clear();
                cc_shell_notify_stunned();
            }
            goto phase2;
        }
    }

phase2:
    if (w3 == 0) goto hazard;
    if ((w3 == 0x11 || w3 == 0x0D) && w5 == 0x0C) {
        if (S->g_a0f5) {
            if (!S->g_a0f6) {
                play_sound(4);
                if (S->g_509e == 3) {
                    helper_a59d();
                    cc_shell_notify_perfect_health();
                    score_add(0xC350, 0);
                }
                S->g_a0f6 = 1;
                S->exit_step = 0;
            }
        } else if (S->g_a106) {
            helper_a110_clear();
            cc_shell_notify_more_crystals();
            S->g_a106 = 0;
        }
        goto hazard;
    }
    if (w3 == 0x32 && w5 == 0x0D) {
        if (S->g_a0f9) {
            int shape;
            cell_word_put(col, row, 0x06, (uint16_t)(w3 - 2));
            play_sound(0x0B);
            shape = (int)cc_rng_next(3);
            if (shape == 0) {
                spawn_effect(0x1B, (int16_t)col, (int16_t)(row - 1));
                score_add(0x7D0, 0);
            } else if (shape == 1) {
                spawn_effect(0x18, (int16_t)col, (int16_t)(row - 1));
                score_add(0x3E8, 0);
            } else if (shape == 2) {
                spawn_effect(0x1C, (int16_t)col, (int16_t)(row - 1));
                score_add(0x1388, 0);
            }
        }
        goto hazard;
    }
    if (w3 == 0x11 && w5 == 9) {
        if (S->g_a109) {
            helper_a110_clear();
            cc_shell_notify_lever();
            S->g_a109 = 0;
        }
        if (S->input_joystick_fire && S->g_a0f4) {
            S->g_a0f4 = 0;
            S->g_a0e8 = (uint16_t)(S->g_a0e8 + 1);
            if (S->g_a0e8 % 3 == 0) {
                cell_word_put(col, row, 0x06, (uint16_t)(w3 + 1));
                S->g_a0e8 = 0;
                S->input_fire = 1;
            }
        }
        if (S->input_fire) {
            play_sound(0x15);
            S->input_fire = 0;
            cell_word_put(col, row, 0x06, (uint16_t)(w3 + 1));
            switch_pair(S->g_a0d4, S->g_a0d6, 0x2D, 0x2E);
        }
        goto hazard;
    }
    if (w3 == 0x15 && w5 == 9) {
        if (S->g_a109) {
            helper_a110_clear();
            cc_shell_notify_lever();
            S->g_a109 = 0;
        }
        if (S->input_joystick_fire && S->g_a0f4) {
            S->g_a0f4 = 0;
            S->g_a0e8 = (uint16_t)(S->g_a0e8 + 1);
            if (S->g_a0e8 % 3 == 0) {
                cell_word_put(col, row, 0x06, (uint16_t)(w3 + 1));
                S->g_a0e8 = 0;
                S->input_fire = 1;
            }
        }
        if (S->input_fire) {
            play_sound(0x15);
            S->input_fire = 0;
            cell_word_put(col, row, 0x06, (uint16_t)(w3 + 1));
            switch_pair(S->g_a0d8, S->g_a0da, 0x31, 0x32);
        }
        goto hazard;
    }
    if (w3 == 0x13 && w5 == 9) {
        if (S->g_a109) {
            helper_a110_clear();
            cc_shell_notify_lever();
            S->g_a109 = 0;
        }
        if (S->input_joystick_fire && S->g_a0f4) {
            S->g_a0f4 = 0;
            S->g_a0e8 = (uint16_t)(S->g_a0e8 + 1);
            if (S->g_a0e8 % 3 == 0) {
                cell_word_put(col, row, 0x06, (uint16_t)(w3 + 1));
                S->g_a0e8 = 0;
                S->input_fire = 1;
            }
        }
        if (S->input_fire) {
            play_sound(0x15);
            S->input_fire = 0;
            cell_word_put(col, row, 0x06, (uint16_t)(w3 + 1));
            switch_pair(S->g_a0dc, S->g_a0de, 0x2F, 0x30);
        }
        goto hazard;
    }
    if (w3 == 0x0F && w5 == 9) {
        if (S->g_a108) { helper_a110_clear(); S->g_a108 = 0; }
        if (S->input_joystick_fire && S->g_a0f4) {
            S->g_a0f4 = 0;
            S->g_a0e8 = (uint16_t)(S->g_a0e8 + 1);
            if (S->g_a0e8 % 3 == 0) { S->g_a0e8 = 0; S->input_fire = 1; }
        }
        if (S->input_fire) {
            play_sound(0x14);
            S->input_fire = 0;
            cell_word_put(col, row, 0x06, (uint16_t)(w3 - 1));
            S->g_a0f3 = 0;
        }
        goto hazard;
    }
    if (w3 == 0x0E && w5 == 9) {
        if (S->g_a108) { helper_a110_clear(); S->g_a108 = 0; }
        if (S->input_joystick_fire && S->g_a0f4) {
            S->g_a0f4 = 0;
            S->g_a0e8 = (uint16_t)(S->g_a0e8 + 1);
            if (S->g_a0e8 % 3 == 0) { S->g_a0e8 = 0; S->input_fire = 1; }
        }
        if (S->input_fire) {
            play_sound(0x14);
            S->input_fire = 0;
            cell_word_put(col, row, 0x06, (uint16_t)(w3 + 1));
            S->g_a0f3 = 1;
        }
        goto hazard;
    }
    if (w3 == 0x1F && w5 == 0x0C) {
        if (S->g_a108) { helper_a110_clear(); S->g_a108 = 0; }
        if (S->input_joystick_fire && S->g_a0f4) {
            S->g_a0f4 = 0;
            S->g_a0e8 = (uint16_t)(S->g_a0e8 + 1);
            if (S->g_a0e8 % 3 == 0) {
                cell_word_put(col, row, 0x06, (uint16_t)(w3 + 1));
                S->g_a0e8 = 0;
                S->input_fire = 1;
            }
        }
        if (S->input_fire) {
            play_sound(0x14);
            S->input_fire = 0;
            cell_word_put(col, row, 0x06, (uint16_t)(w3 + 1));
            helper_toggle_kind((uint16_t)w0);
        }
        goto hazard;
    }
    if (w3 == 0x20 && w5 == 0x0C) {
        if (S->g_a108) { helper_a110_clear(); S->g_a108 = 0; }
        if (S->input_joystick_fire && S->g_a0f4) {
            S->g_a0f4 = 0;
            S->g_a0e8 = (uint16_t)(S->g_a0e8 + 1);
            if (S->g_a0e8 % 3 == 0) {
                cell_word_put(col, row, 0x06, (uint16_t)(w3 - 1));
                S->g_a0e8 = 0;
                S->input_fire = 1;
            }
        }
        if (S->input_fire) {
            play_sound(0x14);
            S->input_fire = 0;
            cell_word_put(col, row, 0x06, (uint16_t)(w3 - 1));
            helper_toggle_kind((uint16_t)w0);
        }
        goto hazard;
    }
    if (w3 == 0x0D && w5 == 1) {
        if (cave_flag_get(w0) != 0) {
            S->exit_col = (int16_t)col;
            S->exit_row = (int16_t)row;
            S->exit_step = 0;
            S->g_a0f7 = 1;
            S->g_2f6 = 0xBEEF; /* DEBUG */
            play_sound(3);
            memcpy_(S->checkpoint_block, S->level_block, 0x21);
        }
        goto hazard;
    }

hazard:
    if (cell_byte_cur(col, row, 0x13) == 0) return;
    if (!S->g_513c) return;
    if (S->g_5140) return;
    if ((w7 == 0x31 || w7 == 0x32 || w7 == 0x1B || w7 == 0x1C) &&
        w6 == 0x0A) {
        S->g_5124 = 1;
        play_sound(5);
        return;
    }
    if (S->g_5124 == 0) {
        if ((int16_t)S->g_a0ea == -1) damage_life();
        if ((int16_t)S->g_5124 > 0) play_sound(5);
    }
}

/* 0xE51D: apply hazard-cell damage for the two lower player corners. */
static void hazard_cell(int16_t x, int16_t y) {
    int col = (int)x / 16;
    int row = (int)y / 16;
    if (cell_word_cur(col, row, 0x00) != (int)0xFF9F) return;
    if (!S->g_513c) return;
    if (S->g_5140) return;
    if ((int16_t)S->g_a0ea != -1) return;
    damage_life();
    if ((int16_t)S->g_5124 > 0) play_sound(5);
}

/* 0xE5D3: player map-cell contact, hazard cells and collectible pickup. */
static void contact_frame(void) {
    int16_t x = S->player_x;
    int16_t y = S->player_y;
    int col, row, w0, w3, w5;

    cell_event((int16_t)(x + 3), y);
    cell_event((int16_t)(x + 0x0C), y);
    cell_event((int16_t)(x + 3), (int16_t)(y + 0x0F));
    cell_event((int16_t)(x + 0x0C), (int16_t)(y + 0x0F));
    if (S->g_a106) cell_event((int16_t)(x + 0x0E), (int16_t)(y + 8));

    col = (x + 4) / 16;
    row = (y + 4) / 16;
    if (cell_word_cur(col, row, 0x00) == (int)0xFF9F) {
        hazard_cell((int16_t)(x + 3), (int16_t)(y + 2));
        hazard_cell((int16_t)(x + 0x0C), (int16_t)(y + 2));
    }

    col = (x + 5) / 16;
    row = (y - 5) / 16;
    if (cell_word_cur(col, row, 0x00) == (int)0xFF9D && S->jump_flag) {
        cell_word_put(col, row, 0x06, 3);
        cell_word_put(col, row, 0x0A, 1);
        cell_put(col, row, 0x11, 1);
        mark_redraw((int16_t)(x + 5), (int16_t)(y - 5));
        cell_word_put(col, row, 0x00, 0);
        goto after_hazards;
    }

    col = (x + 0x0A) / 16;
    row = (y - 5) / 16;
    if (cell_word_cur(col, row, 0x00) == (int)0xFF9D && S->jump_flag) {
        cell_word_put(col, row, 0x06, 3);
        cell_word_put(col, row, 0x0A, 1);
        cell_put(col, row, 0x11, 1);
        mark_redraw((int16_t)(x + 0x0A), (int16_t)(y - 5));
        cell_word_put(col, row, 0x00, 0);
    }

after_hazards:
    if (!((S->airborne && S->g_a0b8 == 0) || S->g_a0fc)) return;

    col = (x + 8) / 16;
    row = (y - 1) / 16;
    w0 = cell_word_cur(col, row, 0x00);
    w3 = cell_word_cur(col, row, 0x06);
    w5 = cell_word_cur(col, row, 0x0A);
    if (w3 < 4 || w3 > 0x0C) return;
    if (w5 != 0x14) return;

    if (S->obj_cursor < OBJ_MAX && S->obj[S->obj_cursor].kind == 0x2D)
        S->obj[S->obj_cursor].state = 2;

    S->g_a0fc = 0;
    S->g_a0b8 = 1;
    S->g_a10a = 1;

    if (w0 == (int)0xFF9E) {
        int shape = w3;
        play_sound(0x0A);
        cell_word_put(col, row, 0x00, 0);
        S->obj_cursor++;
        if (S->obj_cursor < OBJ_MAX) S->obj[S->obj_cursor].state = 0x63;
        {
            SpawnArgs a;
            a.handler = 0x672B; a.handler_seg = 0x1F9A;
            a.p18 = 0; a.speed = 2; a.p16 = 1;
            a.p14 = 0x25; a.p12 = 0x25;
            a.vy = 0xFFFF; a.vx = 0;
            a.ycell = (uint16_t)(row - 1);
            a.xcell = (uint16_t)col;
            a.tile = 0x0D; a.kind = 0x2E;
            a.slot = (int)S->obj_cursor;
            spawn_object_47d(&a);
        }
        (void)shape;
    }
    S->obj_cursor++;
    {
        SpawnArgs a;
        int shape = w3;
        a.handler = 0x69D3; a.handler_seg = 0x1F9A;
        a.p18 = 0; a.speed = 1; a.p16 = 1;
        a.p14 = (uint16_t)shape; a.p12 = (uint16_t)shape;
        a.vy = 0xFFFF; a.vx = 0;
        a.ycell = (uint16_t)row;
        a.xcell = (uint16_t)col;
        a.tile = 0x14; a.kind = 0x2D;
        a.slot = (int)S->obj_cursor;
        spawn_object_47d(&a);
    }
    S->g_a10a = 0;
    cell_word_put(col, row, 0x06, 0x32);
    S->anim_timer = 1;
    S->shape_player = (S->move_dir == 1) ? 0x19 : 0x1A;
}

/* 0x6681 (entry 0x26021): cell-erase effect; only y changes. */
static void h_6681(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    mark_redraw((int16_t)(o->x_prev), (int16_t)(o->y_prev + 0x11));
    mark_redraw((int16_t)(o->x_prev + 0x0F), (int16_t)(o->y_prev + 0x11));
    mark_redraw((int16_t)(o->x_prev + 0x0F), (int16_t)(o->y_prev));
}

/* 0x672B (entry 0x260CB): crystal collection score/bob effect. */
static void h_672b(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    o->p18 = (uint16_t)(o->p18 + 1u);
    if (o->p1a == 0) {
        if (o->p18 == 1) o->y = (int16_t)(o->y + 0x0C);
        else o->y = (int16_t)(o->y - 0x0C);
        if ((int16_t)o->p18 > 1) {
            o->p1a = 1;
            o->p18 = 0;
        }
        return;
    }
    if (o->p1a == 1) {
        int i = (int)o->p18 + 3;
        int16_t d = (int8_t)g_vert_speed[i];
        o->y = (int16_t)(o->y - d);
        if (i > 9) {
            o->p1a = 2;
            o->p18 = 0;
        }
        return;
    }
    if (o->p1a == 2) {
        int i = (int)o->p18 + 0x0A;
        int16_t d = (int8_t)g_vert_speed[i];
        o->y = (int16_t)(o->y + d);
        if (i > 0x10) {
            o->p1a = 3;
            o->p18 = 0;
        }
        return;
    }
    if (o->p1a == 3) {
        o->y = (int16_t)(o->y - (int16_t)o->speed);
        if ((int16_t)o->p18 > 1) {
            o->p1a = 4;
            o->p18 = 0;
        }
        return;
    }
    if (o->p1a == 4) {
        o->y = (int16_t)(o->y + (int16_t)o->speed);
        if ((int16_t)o->p18 > 1) {
            o->p1a = 5;
            o->p18 = 0;
        }
        return;
    }
    if (o->p1a == 5) {
        if ((int16_t)o->p18 > 1) {
            o->p1a = 6;
            o->p18 = 0;
        }
        return;
    }
    if (o->p1a == 6) {
        o->p1a = (uint16_t)(o->p1a + 1u);
        o->ready = 1;
        spawn_effect(0x1C, (int16_t)(o->x / 16), (int16_t)(o->y / 16));
        score_add(0x1388, 0);
    }
}

/* 0x69D3 (entry 0x26373): pickup sprite rise/fall and cell restore. */
static void h_69d3(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    if ((int16_t)o->p18 >= 0 && (int16_t)o->p18 <= 3) {
        o->y = (int16_t)(o->y - (int16_t)o->speed);
    } else if ((int16_t)o->p18 >= 4 && (int16_t)o->p18 <= 7) {
        o->y = (int16_t)(o->y + (int16_t)o->speed);
    } else if ((int16_t)o->p18 == 8) {
        o->ready = 1;
        cell_word_put((int)o->x / 16, (int)o->y / 16, 0x06, o->p14);
        mark_redraw(o->x, o->y);
        if (idx + 1 < OBJ_MAX) S->obj[idx + 1].state = 0;
    }
    o->p18 = (uint16_t)(o->p18 + 1u);
}

static uint32_t g_unknown_handler_hits = 0;

/* 0x1F9A:0x298A (kind 0x05): two-phase walker. */
static void h_298a(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    anim_next(o);
    if (cell_test_200fe(x, y))
        o->vx = (int16_t)(-o->vx);
    if ((int16_t)o->p1a < (int16_t)(o->p18 + 0xa)) {
        anim_select_shape(o);
        o->p16 = 0x10;
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    } else if ((int16_t)o->p1a < (int16_t)(o->p18 + 0x19)) {
        o->shape = 0x11;
        o->p16 = 6;
    } else {
        o->p1a = 0;
        o->p10 = 1;
    }
    o->p1a = (uint16_t)(o->p1a + 1u);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x403A (kind 0x1E): proximity player shover. */
static void h_403a(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int16_t dir;
    dir = proximity_test(1, (int16_t)(S->g_50dc << 4), x, y);
    S->g_5126 = 0;
    if (dir != 0) {
        push_player(5, dir);
        S->g_5118 = 5;
        goto common;
    }
    dir = proximity_test(1, (int16_t)(S->g_50de << 4), x, y);
    if (dir != 0) {
        if ((int16_t)o->vx == dir) {
            push_player(3, dir);
            S->g_5118 = 3;
        }
        goto common;
    }
    dir = proximity_test(1, (int16_t)(S->g_50e0 << 4), x, y);
    if (dir != 0 && (int16_t)o->vx == dir) {
        push_player(1, dir);
        S->g_5118 = 1;
    }
common:
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
    anim_next(o);
    if (idx + 1 < OBJ_MAX) anim_next(&S->obj[idx + 1]);
}

/* 0x1F9A:0x55EC (kind 0x36): paired horizontal mover. */
static void h_55ec(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *child;
    int16_t dir_p, dir_c;
    int spd;
    if (idx + 1 >= OBJ_MAX) return;
    child = &S->obj[idx + 1];
    dir_p = proximity_obj_dir(1, 0x20, 0x130, idx);
    dir_c = proximity_obj_dir(1, 0x20, 0x130, idx + 1);
    if ((int16_t)o->vx == dir_p || (int16_t)child->vx == dir_c) {
        o->speed = 4;
        child->speed = 4;
        spd = 1;
    } else {
        o->speed = 2;
        child->speed = 2;
        spd = 2;
    }
    o->p18 = (uint16_t)(o->p18 + 1u);
    if ((int16_t)o->p18 % spd == 0) {
        anim_next(o);
        anim_next(child);
    }
    if (cell_test_200fe(x, y)) {
        o->vx = (int16_t)(-o->vx);
        anim_select_shape(o);
        child->vx = (int16_t)(-child->vx);
        anim_select_shape(child);
    }
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    child->x = (int16_t)(child->x + (int32_t)child->vx * child->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
    player_interact(child, (int16_t)child->x, (int16_t)child->y);
}




/* 0x1F9A:0x3A2E (kind 0x1C): proximity lever with animation phases. */
static void h_3a2e(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    int16_t dir;
    if (o->state == 1) o->state = 3;
    if ((int16_t)o->vx == 1)
        dir = proximity_test(1, 0x30, (int16_t)(o->x - 0x10), (int16_t)o->y);
    else
        dir = proximity_test(1, 0x30, (int16_t)(o->x + 0x10), (int16_t)o->y);
    if (((int16_t)o->vx == 1 && dir == 1) ||
        ((int16_t)o->vx == -1 && dir == -1) || o->p18 != 0) {
        o->p18 = (uint16_t)(o->p18 + 1u);
        o->state = 0;
        if ((int16_t)o->p18 >= 0x1e) {
            o->p18 = 0;
        } else if ((int16_t)o->p10 < (int16_t)o->p16) {
            o->p10 = (uint16_t)(o->p10 + 1u);
        }
        player_interact(o, (int16_t)o->x, (int16_t)o->y);
        return;
    }
    if ((int16_t)o->p10 > 1) {
        o->p10 = (uint16_t)(o->p10 - 1u);
    } else {
        o->state = 1;
    }
}

/* 0x1F9A:0x3F6C (dynamic): proximity player shover (variant). */
static void h_3f6c(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    int16_t dir;
    S->g_5126 = 0;
    S->g_5118 = 0;
    dir = proximity_test(1, (int16_t)(S->g_50dc << 4), x, y);
    if (dir != 0) {
        push_player(5, dir);
        goto common;
    }
    dir = proximity_test(1, (int16_t)(S->g_50de << 4), x, y);
    if (dir != 0) {
        push_player(3, dir);
        goto common;
    }
    dir = proximity_test(1, (int16_t)(S->g_50e0 << 4), x, y);
    if (dir != 0) push_player(1, dir);
common:
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
    anim_next(o);
}

/* 0x1F9A:0x2E25 (kind 0x16): camera-relative spawner/mover. */
static void h_2e25(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    if (o->p18 == 0) {
        int32_t v = (int32_t)S->cam_x + (int32_t)cc_rng_next(0x140);
        v = tp_long_div(v, 16);
        v = tp_long_mul(v, 16);
        o->x = (int16_t)v;
        o->y = S->cam_y;
        o->x_prev = o->x;
        o->y_prev = o->y;
    }
    o->p18 = (uint16_t)(o->p18 + 1u);
    if ((int16_t)o->y > 0x174) {
        o->ready = 1;
        o->p18 = 0;
    }
    if ((int16_t)S->player_y < 0x64 && (int16_t)o->p18 <= 1) {
        o->ready = 1;
        o->p18 = 0;
        return;
    }
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x2F83 (kind 0x1A): paired vertical mover/trigger. */
static void h_2f83(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *child;
    if (idx + 1 >= OBJ_MAX) return;
    child = &S->obj[idx + 1];
    if (o->state == 1) o->state = 4;
    if (o->state == 3)
        o->vy = (uint16_t)proximity_test(2, 0x50, (int16_t)(x + 0xf), y);
    if ((int16_t)o->vy != 1) return;
    child->vy = 1;
    child->state = 0;
    o->state = 0;
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    child->y = (int16_t)(child->y + (int32_t)child->vy * child->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
    player_interact(child, (int16_t)child->x, (int16_t)child->y);
    if (cell_byte_cur((o->x + 2) / 16, (o->y + 0x11) / 16, 0x11) != 0 ||
        cell_byte_cur((o->x + 2) / 16, (o->y + 0x11) / 16, 0x12) != 0) {
        cell_put((o->x + 2) / 16, (o->y + 8) / 16, 0x12, 1);
        cell_put((child->x + 2) / 16, (child->y + 8) / 16, 0x12, 1);
        play_sound(0x17);
        o->state = 1;
        child->state = 1;
        o->vy = 0;
        o->vy = 0;
    }
}

/* 0x1F9A:0x4AD3 (kind 0x35): left/right animated walker. */
static void h_4ad3(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    if (cell_test_200fe(x, y)) o->vx = (int16_t)(-o->vx);
    o->p1a = (uint16_t)(o->p1a + 1u);
    if ((int16_t)o->p1a > 0x46) {
        o->p1a = 0;
        if (cc_rng_next(100) > 0x46) {
            o->vx = (int16_t)(-o->vx);
            anim_select_shape(o);
        }
    }
    if ((int16_t)o->vx == 1) {
        anim_next(o);
    } else {
        o->p10 = (uint16_t)(o->p10 - 1u);
        if ((int16_t)o->p10 < 1) o->p10 = o->p16;
    }
    o->speed = 4;
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x36B0 (kind 0x16): four-record vertical formation mover. */
static void h_36b0(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int k;
    if (idx + 3 >= OBJ_MAX) return;
    S->obj[idx + 1].state = 0xFFFF;
    S->obj[idx + 2].state = 0xFFFF;
    S->obj[idx + 3].state = 0xFFFF;
    if (cell_byte_cur((x + 0x10) / 16, y / 16, 0x11) ||
        cell_byte_cur((x + 0x10) / 16, (y + 0x1b) / 16, 0x11)) {
        o->vy = (int16_t)(-o->vy);
        if ((int16_t)o->vy == -1) {
            o->speed = 2;
            o->p18 = 1;
            o->p1a = 0;
            play_sound(0x17);
        }
    }
    if (o->p18 == 0) {
        for (k = 0; k <= 3; k++)
            S->obj[idx + k].y = (int16_t)(S->obj[idx + k].y +
                                          (int32_t)o->vy * o->speed);
    } else {
        o->p18 = (uint16_t)(o->p18 + 1u);
        o->p1a = (uint16_t)(o->p1a + 1u);
        if ((int16_t)o->p18 > 0x14) o->p18 = 0;
        if ((int16_t)o->p1a == 1) {
            for (k = 0; k <= 3; k++)
                S->obj[idx + k].y = (int16_t)(S->obj[idx + k].y - 3);
        } else if ((int16_t)o->p1a == 3) {
            for (k = 0; k <= 3; k++)
                S->obj[idx + k].y = (int16_t)(S->obj[idx + k].y + 3);
            play_sound(0x17);
        }
    }
    for (k = 0; k <= 3; k++)
        player_interact(o, (int16_t)S->obj[idx + k].x,
                        (int16_t)S->obj[idx + k].y);
}

/* 0x1F9A:0x4EEB (dynamic): simple companion walker. */
static void h_4eeb(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    anim_next(o);
    if (cell_byte_cur((x + 3) / 16, (y + 8) / 16, 0x11) ||
        cell_byte_cur((x + 0xc) / 16, (y + 8) / 16, 0x11))
        o->vx = (int16_t)(-o->vx);
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x52E7 (kind 0x37): projectile/phase state machine. */
static void h_52e7(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    int16_t dir;
    if (o->p18 == 1) {
        o->p1a = (uint16_t)(o->p1a + 1u);
        if ((int16_t)o->p1a > 0x2d) {
            o->ready = 1;
            o->p18 = 0;
            o->p1a = 0;
        }
    }
    if (o->p18 == 0 &&
        (cell_byte_cur((x + 8) / 16, (y + 3) / 16, 0x11) ||
         cell_byte_cur((x + 8) / 16, (y + 0xc) / 16, 0x11))) {
        play_sound(0x17);
        if (cc_rng_next(1000) < 0x64) {
            o->shape = 0x2e;
            dir = proximity_obj_dir(1, 0x20, 0x130, idx);
            o->vx = (dir == 0) ? (uint16_t)-1 : (uint16_t)dir;
            o->vy = 0;
            o->y = (int16_t)(((int16_t)(o->y + 6) / 16) << 4);
            o->kind = 0x38;
            o->p16 = 5;
            o->speed = 2;
            o->p04 = 1;
            o->p18 = 1;
            o->handler = 0x4EEB;
            o->handler_seg = 0x1F9A;
            mark_redraw((int16_t)o->x, (int16_t)(o->y + 0x10));
        } else {
            o->p18 = 1;
            o->y = (int16_t)(((int16_t)(o->y + 6) / 16) << 4);
            o->shape = 1;
        }
    }
    if (o->p18 == 1) {
        o->p1a = (uint16_t)(o->p1a + 1u);
        player_interact(o, (int16_t)o->x, (int16_t)(o->y + 0xc));
        return;
    }
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x6232 (kind 0x33): two-frame walker. */
static void h_6232(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    o->p18 = (uint16_t)(o->p18 + 1u);
    if ((int16_t)o->p18 % 2 == 0) anim_next(o);
    if (cell_test_200fe(x, y)) {
        o->vx = (int16_t)(-o->vx);
        anim_select_shape(o);
    }
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x4C92 (kind 0x37): spawner that converts a companion to 0x52E7. */
static void h_4c92(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *child;
    anim_next(o);
    if (proximity_test(2, 0x70, x, y) == 1 && idx > 0) {
        child = &S->obj[idx - 1];
        if (child->ready != 0) {
            play_sound(0x10);
            child->vx = 0;
            child->vy = 1;
            child->kind = 0x37;
            child->p16 = 1;
            child->p10 = 1;
            child->speed = 6;
            child->p04 = 0x7D00;
            child->handler = 0x52E7;
            child->handler_seg = 0x1F9A;
            child->ready = 0;
            child->state = 0;
            child->x = (int16_t)(((int16_t)(o->x + 8) / 16) << 4);
            child->y = o->y;
            child->p18 = 0;
            child->p1a = 0;
            child->shape = 0x27;
        }
    }
    if (cell_byte_cur((x + 3) / 16, (y + 8) / 16, 0x11) ||
        cell_byte_cur((x + 0xc) / 16, (y + 8) / 16, 0x11))
        o->vx = (int16_t)(-o->vx);
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x5C8F (kind 0x3A): three-object boss formation. */
static void h_5c8f(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *prev, *next;
    int16_t prev_x, prev_y, next_x, next_y, dir;
    int flip = 0;
    (void)x; (void)y;
    if (idx < 2 || idx + 1 >= OBJ_MAX) return;
    prev = &S->obj[idx - 1];
    next = &S->obj[idx + 1];
    prev_x = (int16_t)(prev->x + (int32_t)prev->vx * prev->speed);
    prev_y = (int16_t)(prev->y + (int32_t)prev->vy * prev->speed);
    next_x = (int16_t)(next->x + (int32_t)next->vx * next->speed);
    next_y = (int16_t)(next->y + (int32_t)next->vy * next->speed);

    if (S->g_a0f0 != 0) {
        if (cc_rng_next(1000) < 0x14 && (int16_t)prev->shape < 0x15) {
            prev->shape = 5;
            prev->p10 = (uint16_t)(prev->p16 + 1u);
            S->g_a0f0 = 0;
        }
    }
    if (S->g_a0fa != 0) {
        if (cc_rng_next(1000) < 0x14 && (int16_t)next->shape < 0x15) {
            next->shape = 0x0D;
            next->p10 = (uint16_t)(next->p16 + 1u);
            S->g_a0fa = 0;
        }
    }
    if (S->g_a0f0 == 0 && cc_rng_next(1000) < 0x14) S->g_a0f0 = 1;
    if (S->g_a0fa == 0 && cc_rng_next(1000) < 0x14) S->g_a0fa = 1;

    o->p18 = (uint16_t)(o->p18 + 1u);
    if ((int16_t)o->p18 % 2 == 0) {
        if (S->g_a0f0 == 0) {
            prev->p10 = (uint16_t)(prev->p10 - 1u);
            if ((int16_t)prev->p10 < 1) prev->p10 = 1;
        } else {
            if ((int16_t)prev->shape == 5 &&
                (int16_t)prev->p10 == (int16_t)prev->p16)
                prev->shape = 9;
            anim_next(prev);
        }
        anim_next(o);
        if (S->g_a0fa == 0) {
            next->p10 = (uint16_t)(next->p10 - 1u);
            if ((int16_t)next->p10 < 1) next->p10 = 1;
        } else {
            if ((int16_t)next->shape == 0x0D &&
                (int16_t)next->p10 == (int16_t)next->p16)
                next->shape = 0x11;
            anim_next(next);
        }
    }

    dir = proximity_obj_dir(1, 6, 0x130, idx);
    if ((int16_t)o->vx == dir && idx >= 2 &&
        S->obj[idx - 2].ready != 0 && cc_rng_next(100) < 5) {
        play_sound(0x10);
        S->obj[idx - 2].vx = (uint16_t)dir;
        S->obj[idx - 2].ready = 0;
        S->obj[idx - 2].state = 0;
        S->obj[idx - 2].x = o->x;
        S->obj[idx - 2].y = o->y;
        anim_select_shape(&S->obj[idx - 2]);
    }

    if ((int16_t)o->vx == -1) {
        if (cell_test_200fe(prev_x, prev_y)) flip = 1;
    } else if ((int16_t)o->vx == 1) {
        if (cell_test_200fe(next_x, next_y)) flip = 1;
    }
    if (flip) {
        o->vx = (int16_t)(-o->vx);
        next->vx = (int16_t)(-next->vx);
        prev->vx = (int16_t)(-prev->vx);
    }
    prev->x = (int16_t)(prev->x + (int32_t)prev->vx * prev->speed);
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    next->x = (int16_t)(next->x + (int32_t)next->vx * next->speed);
    player_interact(prev, (int16_t)prev->x, (int16_t)prev->y);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
    player_interact(next, (int16_t)next->x, (int16_t)next->y);
}

/* 0x1F9A:0x62F2 (kind 0x34): companion respawn walker. */
static void h_62f2(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int16_t dir;
    anim_next(o);
    if (cell_test_200fe(x, y)) {
        o->vx = (int16_t)(-o->vx);
        anim_select_shape(o);
    }
    dir = proximity_obj_dir(1, 6, 0x130, idx);
    if ((int16_t)o->vx == dir && idx > 0 &&
        S->obj[idx - 1].ready != 0 && cc_rng_next(100) < 5) {
        play_sound(0x10);
        S->obj[idx - 1].vx = (uint16_t)dir;
        S->obj[idx - 1].ready = 0;
        S->obj[idx - 1].state = 0;
        S->obj[idx - 1].x = o->x;
        S->obj[idx - 1].y = o->y;
    }
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x3416 (kind 0x0C): proximity walker with companion spawn. */
static void h_3416(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *child;
    o->p18 = (uint16_t)proximity_test(1, 0x15, x, y);
    if ((int16_t)o->x > (int16_t)S->player_x &&
        (int16_t)o->p18 == -1 && (int16_t)o->vx == 1)
        o->p18 = 0;
    if (idx + 1 >= OBJ_MAX) return;
    child = &S->obj[idx + 1];
    if (o->p18 != 0 && (int16_t)o->p18 == (int16_t)o->vx) {
        child->state = 0;
        child->ready = 0;
        child->y = o->y;
        player_interact(child, (int16_t)child->x, (int16_t)child->y);
        anim_next(child);
        if (o->p18 == 1) {
            o->p10 = 9;
            child->x = (int16_t)(o->x + 0x11);
        } else {
            o->p10 = 6;
            child->x = (int16_t)(o->x - 0x10);
        }
        return;
    }
    child->ready = 1;
    anim_next(o);
    o->p1a = (uint16_t)(o->p1a + 1u);
    if ((int16_t)o->p1a > 0x1e) {
        o->p1a = 0;
        if (cc_rng_next(100) > 0x32) {
            o->vx = (int16_t)(-o->vx);
            anim_select_shape(o);
        }
    }
    if (cell_test_200fe(x, y)) {
        o->vx = (int16_t)(-o->vx);
        anim_select_shape(o);
    }
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x2D37 (kind 0x10): proximity-triggered vertical mover. */
static void h_2d37(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    if (o->state == 3) {
        o->vy = (uint16_t)proximity_test(2, 0x70, x, y);
    }
    if ((int16_t)o->vy != 1) return;
    if (o->state == 3) play_sound(0x19);
    o->state = 0;
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
    if ((int16_t)o->y > 0x180) o->ready = 1;
}

/* 0x1F9A:0x2ACF (kind 0x06): flip-on-contact walker. */
static void h_2acf(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    if (cell_test_200fe(x, y) && o->p18 == 0) {
        o->vx = (int16_t)(-o->vx);
        o->p18 = 1;
    }
    if ((int16_t)o->p18 > 0) {
        o->p18 = (uint16_t)(o->p18 + 1u);
        o->p10 = 1;
        if ((int16_t)o->p18 > 0x39) o->p18 = 0;
        player_interact(o, (int16_t)o->x, (int16_t)o->y);
        return;
    }
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    anim_next(o);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x2BD4 (kind 0x07): proximity-triggered horizontal walker. */
static void h_2bd4(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    if (o->state == 3)
        o->vx = (uint16_t)proximity_test(1, 0x80, x, y);
    if ((int16_t)o->vx == 0) return;
    o->state = 0;
    if ((int16_t)o->p10 > 4 && o->p18 != 1) {
        o->p18 = 1;
        o->p12 = (uint16_t)(o->p12 + 4u);
        o->p14 = (uint16_t)(o->p14 + 4u);
        o->p16 = (uint16_t)(o->p16 - 4u);
    }
    anim_next(o);
    if (cell_test_200fe(x, y))
        o->vx = (int16_t)(-o->vx);
    anim_select_shape(o);
    if (o->p18 == 1)
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x3904 (kind 0x0F): proximity-triggered lever/switch. */
static void h_3904(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    int16_t dir;
    if (o->state == 1) o->state = 3;
    dir = proximity_test(2, 0x40, (int16_t)o->x, (int16_t)o->y);
    if (dir != 0 && (int16_t)(S->player_y - 1) < (int16_t)o->y) {
        o->state = 0;
        if ((int16_t)o->p10 < (int16_t)o->p16)
            o->p10 = (uint16_t)(o->p10 + 1u);
        if ((int16_t)o->p10 > 1 && (int16_t)o->p10 < 4)
            play_sound(0x1f);
        player_interact(o, (int16_t)o->x, (int16_t)o->y);
        return;
    }
    o->state = 1;
    o->p10 = 1;
}

/* 0x1F9A:0x47B9 (kind 0x0B): walker with two animation phases. */
static void h_47b9(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    anim_next(o);
    if (cell_test_200fe(x, y)) {
        o->vx = (int16_t)(-o->vx);
        anim_select_shape(o);
    }
    o->p1a = (uint16_t)(o->p1a + 1u);
    if ((int16_t)o->p1a < (int16_t)(o->p18 + 0xa)) {
        o->p16 = 9;
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    } else if ((int16_t)o->p1a < (int16_t)(o->p18 + 0x19)) {
        o->shape = 0x0e;
        o->p16 = 7;
    } else {
        o->p1a = 0;
        o->p10 = 1;
    }
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x32F6 (kind 0x19): companion-driven vertical mover. */
static void h_32f6(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    if (o->state == 2 && idx > 0) anim_next(&S->obj[idx - 1]);
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    anim_next(o);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
    if (cell_byte_cur((o->x + 2) / 16, (o->y + 8) / 16, 0x11) != 0 ||
        cell_byte_cur((o->x + 2) / 16, (o->y + 8) / 16, 0x12) != 0) {
        play_sound(0x18);
        o->ready = 1;
    }
}

/* 0x1F9A:0x4145 (kind 0x23): bouncing adversary. */
static int cell_blocked_4145(int col, int row) {
    return cell_byte_cur(col, row, 0x11) != 0 ||
           cell_word_cur(col, row, 0x00) == 0xFFA0;
}
static void h_4145(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    int pick = 0;
    anim_next(o);
    if ((int16_t)o->vy != 0) {
        if (cell_blocked_4145((x + 8) / 16, (y + 3) / 16) ||
            cell_blocked_4145((x + 8) / 16, (y + 0xc) / 16))
            pick = 1;
    }
    if (!pick) {
        if ((int16_t)o->vx == 0) {
            if ((int16_t)o->vy == 0) pick = 1;
        } else {
            if (cell_blocked_4145((x + 3) / 16, (y + 8) / 16) ||
                cell_blocked_4145((x + 0xc) / 16, (y + 8) / 16))
                pick = 1;
        }
    }
    if (pick) {
        switch (cc_rng_next(4)) {
            case 0:
                if (cell_blocked_4145((o->x - 4) / 16, (o->y + 8) / 16)) {
                    o->vx = 0; o->vy = 0;
                } else {
                    o->shape = 0x25; o->p10 = 1; o->vx = -1; o->vy = 0;
                }
                break;
            case 1:
                if (cell_blocked_4145((o->x + 0x14) / 16, (o->y + 8) / 16)) {
                    o->vx = 0; o->vy = 0;
                } else {
                    o->shape = 0x21; o->p10 = 1; o->vx = 1; o->vy = 0;
                }
                break;
            case 2:
                if (cell_blocked_4145((o->x + 8) / 16, (o->y - 4) / 16)) {
                    o->vx = 0; o->vy = 0;
                } else {
                    o->shape = 0x29; o->p10 = 1; o->vx = 0; o->vy = -1;
                }
                break;
            default:
                if (cell_blocked_4145((o->x + 8) / 16, (o->y + 0x14) / 16)) {
                    o->vx = 0; o->vy = 0;
                } else {
                    o->shape = 0x2d; o->p10 = 1; o->vx = 0; o->vy = 1;
                }
                break;
        }
    }
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x48FE (kind 0x0A): random walker; flips at walls/proximity. */
static void h_48fe(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    anim_next(o);
    o->p1a = (uint16_t)(o->p1a + 1u);
    if ((int16_t)o->p1a > 0x1e) {
        o->p1a = 0;
        if (cc_rng_next(100) > 0x32) {
            o->vx = (int16_t)(-o->vx);
            anim_select_shape(o);
        }
    }
    if (cell_byte_cur((x + 3) / 16, (y + 8) / 16, 0x11) ||
        cell_byte_cur((x + 0xc) / 16, (y + 8) / 16, 0x11)) {
        o->vx = (int16_t)(-o->vx);
        anim_select_shape(o);
    }
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x6458 (kind 0x38): companion spawn/move pair. */
static void h_6458(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *n;
    int16_t dir;
    int16_t rem;
    if (o->state == 3) return;
    if (idx + 1 >= OBJ_MAX) return;
    n = &S->obj[idx + 1];
    dir = proximity_obj_dir(1, 6, 0x130, idx);
    if ((int16_t)n->vx == dir && n->state == 2) {
        play_sound(0x10);
        n->ready = 0;
        n->state = 0;
        if ((int16_t)n->vx == 1)
            n->x = (int16_t)(o->x + 8);
        else
            n->x = (int16_t)(o->x - 8);
        rem = (int16_t)(o->y % 16);
        n->y = (int16_t)(((int16_t)(o->y + rem) / 16) << 4);
        o->p18 = 1;
    }
    anim_next(o);
    if (cell_byte_cur((x + 8) / 16, (y + 5) / 16, 0x11) ||
        cell_byte_cur((x + 8) / 16, (y + 0xa) / 16, 0x11) ||
        cell_byte_cur((x + 8) / 16, (y + 5) / 16, 0x12) != 0)
        o->vy = (int16_t)(-o->vy);
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
}

/* 0x1F9A:0x4FD4 (kind 0x39): multi-state boss/companion retargeter. */
static void h_4fd4(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *cm1 = (idx >= 1) ? &S->obj[idx - 1] : o;
    ObjectEntry *cm2 = (idx >= 2) ? &S->obj[idx - 2] : o;
    int16_t dir;

    if (o->p1a == 0) {
        if ((int16_t)o->p18 <= (int16_t)S->g_260) goto p1a_handling;
        if ((int16_t)(o->x % 16) != 0) goto p1a_handling;
        o->p18 = 0;
        o->p1a = 1;
        o->p10 = 1;
        o->shape = 0x2d;
        cm2->state = 0;
        cm2->ready = 0;
        cm2->x = o->x;
        cm2->y = (int16_t)(o->y - 0x10);
        S->g_260 = (uint16_t)(cc_rng_next(0x32) + 0x32);
    }

p1a_handling:
    if ((int16_t)o->p1a > 0x24) {
        o->p1a = 0;
        cm2->ready = 1;
        anim_select_shape(o);
        goto p1a_check;
    }
    if ((int16_t)o->p1a <= 0) goto p1a_check;
    o->p1a = (uint16_t)(o->p1a + 1u);
    if ((int16_t)o->p1a % 3 != 0) goto p1a_check;
    anim_next(cm2);

p1a_check:
    if (o->p1a != 0) goto interact_only;

    o->p18 = (uint16_t)(o->p18 + 1u);
    if ((int16_t)o->p18 % 2 == 0) anim_next(o);

    if (cell_test_200fe(x, y)) {
        o->vx = (int16_t)(-o->vx);
        anim_select_shape(o);
    }

    dir = proximity_obj_dir(1, 6, 0x130, idx);
    if ((int16_t)o->vx != dir) goto move_x;
    if (cm1->ready == 0) goto move_x;
    if (cc_rng_next(100) >= 5) goto move_x;
    cm1->vx = (uint16_t)dir;
    cm1->ready = 0;
    cm1->state = 0;
    cm1->x = o->x;
    cm1->y = o->y;

move_x:
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
interact_only:
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

static handler_fn handler_for(uint16_t off) {
    switch (off) {
        case 0x1fb9: return h_1fb9;
        case 0x2241: return h_2241;
        case 0x2850: return h_2850;
        case 0x2877: return h_2877;
        case 0x831: return h_831;
        case 0x1fc2: return h_1fc2;
        case 0x2176: return h_2176;
        case 0x323c: return h_323c;
        case 0x3be8: return h_3be8;
        case 0x3d23: return h_3d23;
        case 0x3e70: return h_3e70;
        case 0x3f13: return h_3f13;
        case 0x298a: return h_298a;
        case 0x2d37: return h_2d37;
        case 0x3416: return h_3416;
        case 0x403a: return h_403a;
        case 0x55ec: return h_55ec;
        case 0x62f2: return h_62f2;
        case 0x6232: return h_6232;
        case 0x4c92: return h_4c92;
        case 0x5c8f: return h_5c8f;
        case 0x4eeb: return h_4eeb;
        case 0x52e7: return h_52e7;
        case 0x2f83: return h_2f83;
        case 0x3a2e: return h_3a2e;
        case 0x3f6c: return h_3f6c;
        case 0x2e25: return h_2e25;
        case 0x4ad3: return h_4ad3;
        case 0x36b0: return h_36b0;
        case 0x2acf: return h_2acf;
        case 0x2bd4: return h_2bd4;
        case 0x3904: return h_3904;
        case 0x4145: return h_4145;
        case 0x47b9: return h_47b9;
        case 0x32f6: return h_32f6;
        case 0x4a4b: return h_4a4b;
        case 0x4a82: return h_4a82;
        case 0x48fe: return h_48fe;
        case 0x4fd4: return h_4fd4;
        case 0x6458: return h_6458;
        case 0x5855: return h_5855;
        case 0x5bbe: return h_5bbe;
        case 0x6681: return h_6681;
        case 0x672b: return h_672b;
        case 0x69d3: return h_69d3;
        case 0x6afa: return h_6afa;
        case 0x6d31: return h_6d31;
        case 0x6ff6: return h_6ff6;
        case 0x70e4: return h_70e4;
        default:
            g_unknown_handler_hits++;
            return h_1fb9;   /* inert fallback */
    }
}

/* ---------------------------------------------------------------- update loop
 * Function 0xD72A.  One call advances slots 2..59 then 0 (never slot 1,
 * which is the player).  Handlers are dispatched exactly as in the binary:
 * some kinds run their handler directly, the rest run it only when visible
 * (0xF38 with margin 0x20) and not yet marked ready.
 */
static void step_objects(void) {
    int i;

    for (i = 0; i < OBJ_MAX; i++) {
        int s = (2 + i) % OBJ_MAX;          /* 2..59, 0 */
        ObjectEntry *o = &S->obj[s];
        int32_t x_new = o->x + (int32_t)o->vx * o->speed;
        int32_t y_new = o->y + (int32_t)o->vy * o->speed;

        if (o->kind == 0) continue;         /* empty slot */

        handler_fn h = handler_for(o->handler);

        if (o->ready) {                     /* state advance for ready objects */
            if (o->state == 0) o->state = 1;
            else if (o->state == 1) o->state = 2;
        }

        /* Kinds whose handler runs directly (no visibility gate). */
        switch (o->kind) {
            case 0x20: case 0x2b: case 0x21: case 0x14: case 0x13:
            case 0x33: case 0x2d: case 0x27: case 0x3b: case 0x26: case 0x2a:
                if (!o->ready) h(o, s, (int16_t)x_new, (int16_t)y_new);
                continue;
        }

        /* Visible objects run their handler; visibility margin is 0x20. */
        /* 0xF38 tests the record's *current* x/y, before the handler moves it;
         * handlers still receive the predicted x_new/y_new. */
        if (!S->a0f8 && visible_obj(0x20, o->x, o->y)) {
            if (!o->ready) h(o, s, (int16_t)x_new, (int16_t)y_new);
            continue;
        }

        if (o->kind == 2 || o->kind == 0x2B) {
            if (S->a0f8 && !o->ready) h_251d(o, s, (int16_t)x_new, (int16_t)y_new);
            if (!o->ready && !visible_obj(0x20, o->x, o->y)) o->ready = 1;
            continue;
        }

        if (o->kind == 0x0d) {
            if (!o->ready && o->state != 3) o->ready = 1;
            continue;
        }

        if (o->kind == 3 || o->kind == 0x1d || o->kind == 0x32 ||
            o->kind == 0x37 || o->kind == 0x31) {
            if (!o->ready) {
                o->ready = 1;
                S->obj[(s + OBJ_MAX - 1) % OBJ_MAX].p18 = 0;
            }
            continue;
        }

        if (o->kind == 4) {
            if (!o->ready) {
                o->ready = 1;
                o->p18 = 0;
            }
            continue;
        }

        if (o->kind == 7) {
            if (o->state == 0) {
                int32_t ax;
                o->p18 = 0;
                o->state = 3;
                o->p12 = (uint16_t)(o->p12 - 4);
                o->p14 = (uint16_t)(o->p14 - 4);
                o->p16 = (uint16_t)(o->p16 + 4);
                o->vx = 1;
                anim_select_shape(o);
                ax = o->vx; if (ax < 0) ax = -ax;
                mark_redraw((int16_t)(o->x_prev + ax * 15),
                            (int16_t)(o->y_prev + ax * 15));
                ax = o->vy; if (ax < 0) ax = -ax;
                mark_redraw((int16_t)(o->x_prev + ax * 15),
                            (int16_t)(o->y_prev + ax * 15));
            }
            continue;
        }

        if (o->kind == 0x10) {
            S->g_5126 = 0;
        }
        if (o->kind == 0x16) {
            int k;
            for (k = 1; k <= 3; k++) {
                int t = (s + k) % OBJ_MAX;
                S->obj[t].state = 0;
            }
        }
    }
}

/* Level argument mapping: 0=hub, 1..16=caves, 0xffff=intro, 0xfffe=finale. */
static int count_crystal_cells(void) {
    int n = 0;
    for (int r = 0; r < CELL_ROWS; r++) {
        for (int c = 0; c < CELL_COLS; c++) {
            int w6 = cell_word_cur(c, r, 0x0C);
            int w7 = cell_word_cur(c, r, 0x0E);
            if (w6 == 0x0D && w7 >= 1 && w7 <= 4) n++;
        }
    }
    return n;
}

static void load_arg(int arg) {
    int level;
    uint16_t u = (uint16_t)arg;
    if (u == 0xFFFF) level = 0;
    else if (u == 0xFFFE) level = 1;
    else if (u == 0) level = 2;
    else if (u <= 16) level = (int)u + 2;
    else level = 2;
    S->next_level = (int16_t)arg;
    load_level(level, 0, 0, 1);
}

/* 0xD4E5: hub door opening/entry; caller has set a0f7. */
static void door_entry_frame(void) {
    int c1, r1, w3;
    S->exit_step = (uint16_t)(S->exit_step + 1u);
    if ((int16_t)S->exit_step < 3) {
        w3 = (int)S->exit_step + 0x0D;
        cell_word_put(S->exit_col, S->exit_row, 0x06, w3);
        mark_redraw((int16_t)(S->exit_col * 16), (int16_t)(S->exit_row * 16));
        return;
    }
    if ((int16_t)S->exit_step != 3) return;

    S->g_a0f7 = 0;
    mark_redraw((int16_t)(S->exit_col * 16), (int16_t)(S->exit_row * 16));
    S->spawn_col_override = (uint16_t)(S->exit_col - (int16_t)S->move_dir);
    S->spawn_row_override = (uint16_t)S->exit_row;
    play_sound(0x14);
    S->jump_flag = 0;
    S->g_a0f6 = 0;
    c1 = S->exit_col + 1;
    r1 = S->exit_row + 1;
    {
        int arg = cell_word_cur(S->exit_col, S->exit_row, 0x00);
        S->next_level = (int16_t)arg;
        load_arg(arg);
    }
    (void)c1; (void)r1;
}

/* 0xD360 + 0xD412: cave exit animation, completion flag and return to hub. */
static void exit_to_hub_frame(void) {
    int all;
    int i;
    S->exit_step = (uint16_t)(S->exit_step + 1u);
    if ((int16_t)S->exit_step < 4) {
        cell_word_put(S->exit_col, S->exit_row, 0x06,
                      (uint16_t)(S->exit_step + 0x0D));
        mark_redraw((int16_t)(S->exit_col * 16), (int16_t)(S->exit_row * 16));
        cell_word_put(S->exit_col, (int16_t)(S->exit_row + 1), 0x06,
                      (uint16_t)(S->exit_step + 0x11));
        mark_redraw((int16_t)(S->exit_col * 16),
                    (int16_t)((S->exit_row + 1) * 16));
        return;
    }
    if ((int16_t)S->exit_step == 4) {
        mark_redraw((int16_t)(S->exit_col * 16), (int16_t)(S->exit_row * 16));
        mark_redraw((int16_t)(S->exit_col * 16), (int16_t)((S->exit_row + 1) * 16));
        return;
    }

    S->player_block = 6;
    S->player_y = (int16_t)((S->exit_row + 1) * 16);
    S->shape_player = 1;
    if ((S->exit_step & 1) == 0) player_anim_next();
    S->player_x = (int16_t)(S->player_x + 1);
    if (S->player_x != (int16_t)((S->exit_col + 1) * 16)) return;

    play_sound(0x14);
    S->jump_flag = 0;
    S->g_a0f6 = 0;
    cave_flag_set(S->next_level, 0);
    all = 1;
    for (i = 1; i <= 16; i++) if (S->cave_flags[i] != 0) { all = 0; break; }
    S->g_a0fb = (uint8_t)all;
    if (S->g_a0fb) {
        S->g_a110 = 1;
        S->g_3396 = 0;
        S->g_3394 = 1;
    }
    S->next_level = 0;
    /* 0xD4CC loads the hub without touching [0x2B72]/[0x2B74]: they still
     * hold the hub-door entry cell, so the player returns beside that door. */
    play_sound(0x14);
    load_arg(0);
}

/* 0xCF6C: player fire.  The attack is object slot 2 (DS:0x4638). */
static void cc_fire(void) {
    ObjectEntry *a = &S->obj[2];

    if (S->g_509c <= 0 && S->power_shot == 0) {
        play_sound(0x1E);
        if (!S->input_left && !S->input_right) {
            S->shape_player = (S->move_dir == 1) ? 0x1B : 0x1C;
            S->anim_timer = 1;
        }
    }

    if (S->g_509c <= 0 && S->power_shot == 0 && S->g_5140 == 0) return;
    if (S->obj[2].ready == 0) return;
    if ((int16_t)S->cooldown <= 6) return;

    a->vx = (int16_t)S->move_dir;
    if (S->power_shot) {
        play_sound(0x0F);
        a->speed = 8;
        a->p16 = 4;
        a->p1a = 2;
        if (a->vx == 1) {
            a->x = (int16_t)(S->player_x + 8);
            a->shape = 0x1D;
            S->shape_player = 0x1B;
        } else {
            a->x = S->player_x;
            a->shape = 0x21;
            S->shape_player = 0x1C;
        }
    } else {
        play_sound(0x0E);
        a->speed = 4;
        a->p16 = 6;
        a->p1a = 1;
        if (a->vx == 1) {
            a->x = (int16_t)(S->player_x + 8);
            a->shape = 0x25;
            S->shape_player = 0x1B;
        } else {
            a->x = S->player_x;
            a->shape = 0x2B;
            S->shape_player = 0x1C;
        }
    }
    S->anim_timer = 1;
    if (S->power_shot == 0 && S->g_5140 == 0) S->g_509c--;

    a->y = S->player_y;
    a->x_prev = a->x;
    a->y_prev = a->y;
    a->kind = 2;
    S->cooldown = 0;
    S->anim_suppress = 1;
    S->obj[2].ready = 0;
    a->state = 0;
    a->p18 = 0;
    a->handler = 0x2850;
    a->handler_seg = 0x1F9A;
    a->tile = 6;
    S->impulse = S->fire_impulse;
}

/* 0xEBF2: cooldown, collectible spawn/expiry, exit unlock and fire. */
static void cc_fire_timers(void) {
    S->cooldown = (uint16_t)(S->cooldown + 1u);
    if ((int16_t)S->cooldown > 0x7D00) S->cooldown = 0x19;

    S->g_a0ce = (uint16_t)(S->g_a0ce + 1u);
    if ((int16_t)S->g_a0ce > 0x10E && S->g_a10b == 0) {
        if (cc_rng_next(100) < 0x23) S->g_a10b = 1;
        else S->g_a0ce = 0;
    }
    if (S->g_a10b != 0 && S->g_a126 == 0) {
        if ((int16_t)S->g_a0d0 == -1) {
            int rx = (int)cc_rng_next(0x140);
            int ry = (int)cc_rng_next(0xC8);
            int c1 = (rx + S->cam_x) / 16 + 1;
            int r1 = (ry + S->cam_y) / 16 + 1;
            if (cell_word_raw(c1, r1, 0x0E) == 0 &&
                cell_byte_cur(c1 - 1, r1 - 1, 0x11) == 0) {
                int shape = (int)cc_rng_next(3) + 0x25;
                cell_word_raw_put(c1, r1, 0x0E, shape);
                cell_word_raw_put(c1, r1, 0x0C, 1);
                S->g_a0d0 = (uint16_t)(c1 * 16 - 1);
                S->g_a0d2 = (uint16_t)(r1 * 16 - 1);
                mark_redraw((int16_t)S->g_a0d0, (int16_t)S->g_a0d2);
                play_sound(0x0C);
            }
        } else if ((int16_t)S->g_a0ce > 0x19E) {
            int c1 = (int)S->g_a0d0 / 16 + 1;
            int r1 = (int)S->g_a0d2 / 16 + 1;
            S->g_a10b = 0;
            S->g_a0ce = 0;
            cell_word_raw_put(c1, r1, 0x0E, 0);
            mark_redraw((int16_t)S->g_a0d0, (int16_t)S->g_a0d2);
            S->g_a0d0 = 0xFFFF;
            S->g_a0d2 = 0xFFFF;
        }
    }

    if ((int16_t)S->next_level > 0 && S->g_a0cc == 0 && S->g_a0f5 == 0) {
        play_sound(0x20);
        if (S->g_a107 && (int16_t)S->next_level > 0) {
            helper_a110_clear();
            S->g_a107 = 0;
        }
        S->g_a0f5 = 1;
        cc_shell_notify_last_crystal();
        cell_put(S->exit_col, S->exit_row, 0x11, 0);
        cell_put(S->exit_col, (int16_t)(S->exit_row + 1), 0x11, 0);
        cell_put(S->exit_col, S->exit_row, 0x11, 0);
    }

    if (S->power_shot != 0 && (int16_t)S->power_timer <= -1 &&
        S->obj[2].ready != 0)
        S->power_shot = 0;

    if (S->input_fire) cc_fire();
}

/* 0xEAEA: status/invulnerability timers. */
static void cc_status_timers(void) {
    if (S->a0f8 != 0 && (int16_t)S->power_timer <= -1) S->a0f8 = 0;
    if (S->g_a100 != 0 && (int16_t)S->power_timer <= -1) S->g_a100 = 0;

    if (S->status_30b != 0 && S->g_2f2 < OBJ_MAX) {
        ObjectEntry *o = &S->obj[S->g_2f2];
        if (o->state == 2) {
            o->ready = 0;
            o->state = 0;
            o->x = S->player_x;
            o->y = S->player_y;
        }
    }

    if ((int16_t)S->g_a0b8 > 0) {
        S->g_a0b8 = (uint16_t)(S->g_a0b8 + 1u);
        if ((int16_t)S->g_a0b8 > 9) S->g_a0b8 = 0;
    }

    if ((int16_t)S->g_a0ea > -1) {
        S->g_a0ea = (uint16_t)(S->g_a0ea + 1u);
        if ((int16_t)S->g_a0ea > 0x13) S->g_a0ea = 0xFFFF;
        if ((S->g_a0ea % 7) == 0) S->obj[1].p02 = 1;
    }
}

void cc_objects_step_objects(void) {
    input_commit();
    step_objects();
}

void cc_test_fill_source(int obj_byte, int cell_byte) {
    unsigned char *raw = (unsigned char *)&S->obj[0];
    unsigned i;
    for (i = 0; i < sizeof(S->obj); i++) raw[i] = (unsigned char)obj_byte;
    for (int r = 0; r < CELL_ROWS; r++)
        for (int c = 0; c < CELL_COLS; c++)
            for (i = 0; i < 20; i++)
                S->cells[r][c][i] = (unsigned char)cell_byte;
}
void cc_test_call_handler(int handler, int slot, int x, int y) {
    if (slot < 0 || slot >= OBJ_MAX) return;
    handler_for((uint16_t)handler)(&S->obj[slot], slot,
                                   (int16_t)x, (int16_t)y);
}
uint32_t cc_unknown_handler_hits(void) { return g_unknown_handler_hits; }
void cc_test_spawn_2002d(int mode) { spawn_template_2002d(mode); }
void cc_test_cell_event(int x, int y) { cell_event((int16_t)x, (int16_t)y); }
void cc_test_contact(void) { contact_frame(); }
void cc_test_spawn_47d(int handler, int handler_seg, int p18, int speed,
                       int p16, int p14, int p12, int vy, int vx,
                       int ycell, int xcell, int tile, int kind, int slot) {
    SpawnArgs a;
    a.handler = (uint16_t)handler; a.handler_seg = (uint16_t)handler_seg;
    a.p18 = (uint16_t)p18; a.speed = (uint16_t)speed;
    a.p16 = (uint16_t)p16; a.p14 = (uint16_t)p14; a.p12 = (uint16_t)p12;
    a.vy = (uint16_t)vy; a.vx = (uint16_t)vx;
    a.ycell = (uint16_t)ycell; a.xcell = (uint16_t)xcell;
    a.tile = (uint16_t)tile; a.kind = (uint16_t)kind; a.slot = slot;
    spawn_object_47d(&a);
}

void cc_objects_step_physics(void) {
    /* Normal-state physics subset: 0xE011 (horizontal + camera) then 0xE36B
     * (jump/vertical).  Cross-frame interactions/fire/exit are Phase 2/3. */
    input_commit();
    cc_horizontal();
    cc_jump_vertical();
}

void cc_objects_step(void) {
    /* Original normal frame order: physics first, then the object loop. */
    cc_objects_step_physics();
    step_objects();
}

/* 0xCF46: restore the restart checkpoint. */
static void restore_checkpoint(void) {
    S->g_509c = S->checkpoint_50be;
    S->score_lo = S->checkpoint_50c7;
    S->score_hi = S->checkpoint_50c9;
    S->g_50a2 = S->checkpoint_50c4;
    S->g_509e = 3;
}

/* 0xD15C: reload the current level through the loader-equivalent path. */
static void reload_current_level(void) {
    S->spawn_col_override = 0xFFFF;
    S->spawn_row_override = 0;
    load_level(S->cur_level, 0, 0, 1);
}

/* 0xD140: decrement the fade/lives latch. */
static void fade_latch(void) {
    S->checkpoint_50a0 = (uint16_t)(S->checkpoint_50a0 - 1u);
    if (S->checkpoint_50a0 == 0) {
        S->g_3394 = 1;
        S->g_a110 = 1;
    }
}

static void restart_sequence(void) {
    S->g_5124 = 0;
    reload_current_level();
    fade_latch();
    restore_checkpoint();
}

/* 0x0FC5:0xB7B7: player death animation.  Uses [0x4623]/[0x4625]
 * (slot-1 p18/p1a) as phase/frame and returns 1 when the final phase ends. */
static int death_animation(void) {
    uint16_t *phase = &S->obj[1].p18;
    uint16_t *frame = &S->obj[1].p1a;
    int done = 0;

    switch (*phase) {
        case 0:
            if (*frame == 0) {
                S->player_block = 0x11;
                S->anim_timer = 1;
                S->shape_player = 1;
                S->obj[1].p16 = 6;
            }
            if (*frame > 0xC && S->anim_timer <= S->obj[1].p16)
                S->anim_timer = (uint16_t)(S->anim_timer + 1u);
            (*frame)++;
            if (*frame > 0x18) { *frame = 0; (*phase)++; }
            break;
        case 1:
            if (*frame == 0) {
                S->anim_timer = 1;
                S->shape_player = 8;
                S->obj[1].p16 = 2;
            }
            if (*frame < 9) player_anim_next();
            (*frame)++;
            if (*frame > 0xE) { *frame = 0; (*phase)++; }
            break;
        case 2:
            if (*frame == 0) {
                S->anim_timer = 1;
                S->shape_player = 0xA;
            }
            (*frame)++;
            if (*frame > 0xE) { *frame = 0; (*phase)++; }
            break;
        case 3:
            if (*frame == 0) {
                S->anim_timer = 1;
                S->shape_player = 0xB;
                S->obj[1].p16 = 5;
            }
            if (*frame < 0xF) player_anim_next();
            (*frame)++;
            if (*frame == 0xF) { *frame = 0; (*phase)++; }
            break;
        case 4:
            if (*frame == 0) {
                S->anim_timer = 1;
                S->shape_player = 0x10;
                S->obj[1].p16 = 2;
            }
            if (*frame == 3) player_anim_next();
            (*frame)++;
            if (*frame > 0x14) done = 1;
            break;
        default:
            done = 1;
            break;
    }
    return done;
}

/* 0xD17D: death state 1 - play the death animation, then wait for restart. */
static void death_state1(void) {
    if (death_animation()) {
        restart_sequence();
        return;
    }
    if ((S->g_2b5e > 0 && S->g_2b5e < 0x80 && S->obj[1].p18 > 0) ||
        S->input_fire || S->input_joystick_fire)
        restart_sequence();
}

/* 0xD1C6: death state 2 - spawn 0x6681 and play the fall/restart sequence. */
static void death_state2(void) {
    uint16_t *phase = &S->obj[1].p18;
    uint16_t *frame = &S->obj[1].p1a;

    if (*phase == 0) {
        if (*frame == 0) {
            S->player_block = 0x11;
            S->anim_timer = 1;
            S->shape_player = 0x12;
            S->obj[1].p16 = 3;
        }
        if (*frame > 3 && (*frame % 3) == 0 &&
            S->anim_timer < S->obj[1].p16)
            S->anim_timer = (uint16_t)(S->anim_timer + 1u);
        *frame = (uint16_t)(*frame + 1u);
        if (*frame > 0x11) {
            *frame = 0;
            *phase = (uint16_t)(*phase + 1u);
        }
        return;
    }
    if (*phase != 1) return;

    if (*frame == 0) {
        int slot;
        S->obj_cursor++;
        slot = S->obj_cursor;
        if (slot < OBJ_MAX) {
            SpawnArgs a;
            S->obj[slot].state = 0x63;
            a.handler = 0x6681; a.handler_seg = 0x1F9A;
            a.p18 = 0; a.speed = 4; a.p16 = 1;
            a.p14 = 0x15; a.p12 = 0x15;
            a.vy = 0xFFFF; a.vx = 0;
            a.ycell = (uint16_t)(S->player_y / 16);
            a.xcell = (uint16_t)(S->player_x / 16 + 1);
            a.tile = 0x11; a.kind = 0x29; a.slot = slot;
            spawn_object_47d(&a);
            S->obj[slot].state = 0;
            S->obj[slot].x = S->player_x;
            S->obj[slot].y = (int16_t)(S->player_y - 0x10);
            S->obj[slot].x_prev = S->obj[slot].x;
            S->obj[slot].y_prev = S->obj[slot].y;
        }
        S->player_block = 7;
        S->anim_timer = 0;
        S->shape_player = 0x19;
        S->obj[1].p16 = 9;
        *frame = (uint16_t)(*frame + 1u);
    }
    if (S->anim_timer < S->obj[1].p16)
        S->anim_timer = (uint16_t)(S->anim_timer + 1u);
    else
        *frame = (uint16_t)(*frame + 1u);
    if (*frame >= 4) restart_sequence();
}

/* 0xD342: death state 3 - wait for the restart input. */
static void death_state3(void) {
    if (S->input_fire || S->input_jump) restart_sequence();
}

/* Exact 0xF4E6 frame dispatch. */
void cc_objects_step_full(void) {
    if (S->g_a0f6) {
        exit_to_hub_frame();
    } else if (S->g_a0f7) {
        door_entry_frame();
    } else if (S->g_a0fb) {
        /* 0xD710: story/finale transition. */
        cc_shell_finale_done();
        S->g_a0fb = 0;
        S->g_a110 = 1;
    } else if (S->g_a126) {
        /* 0xD6D4: story screen input; outside Phase 3. */
    } else if (S->g_5124) {
        if (S->g_5124 == 1) death_state1();
        else if (S->g_5124 == 2) death_state2();
        else death_state3();
    } else {
        cc_objects_step_physics();
        contact_frame();
        cc_fire_timers();
    }
    cc_status_timers();
    sync_player_object();
    step_objects();
    sync_player_object();
}

/* 0x0F74C player sprite-draw sound.  The original takes this path only when
 * the player's draw state ([0x45E0], the object record's state word) is
 * non-zero, i.e. during a hit/transform/dissolve animation -- NOT while
 * walking.  [0xA0EA] >= 4 suppresses it for the player.  The C draw is
 * host-side, so the page calls this after each step. */
void cc_player_draw_sound(void) {
    ObjectEntry *p = &S->obj[1];
    if (p->state == 0 || p->state == 2) return;
    if (S->g_a0ea >= 4) return;
    play_sound(7);
}

/* ---------------------------------------------------------------- collision
 * Check if any object is in proximity to the player (using helper 0x1F9A:0x177B).
 * Returns the object index, or -1 if none.  `radius` is the proximity
 * threshold in pixels; `mode` selects the test (1 = same-x, 2 = box). */
int cc_collide_player(int mode, int16_t radius) {
    int i;
    for (i = 0; i < OBJ_MAX; i++) {
        ObjectEntry *o = &S->obj[i];
        if (i == 1) continue;          /* slot 1 is the player itself */
        if (o->kind == 0) continue;    /* empty slot */
        if (o->state == 2) continue;   /* inactive/free slot */
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
/* ---------------------------------------------------------------- level loading
 * Mirrors the loader (0xB1A9) and the player-start paths:
 *   * initial load  -> player spawns at the level's Y-token cell
 *   * transition    -> player spawns at [0x2B72]/[0x2B74] if set (spawn
 *     override written by the exit code), otherwise at the Y-token cell.
 *
 * The spawn override ([0x2B72]/[0x2B74]; 0xffff = none) is consulted here so
 * that the exit transition can place the player at a specific cell on the
 * next level (original: 0x6E05 / 0xD572-0xD578). */
/* Apply the exact 0xC3E new-game defaults captured in startup_data.h. */
static void apply_startup_state(void) {
    S->blocked_l = g_startup.f_a0f1;
    S->blocked_r = g_startup.f_a0f2;
    S->g_a0f6 = g_startup.f_a0f6;
    S->a0f8 = g_startup.f_a0f8;
    S->g_a0fc = g_startup.f_a0fc;
    S->airborne = g_startup.f_a0fd;
    S->g_a0ea = g_startup.f_a0ea;
    S->walk_phase = g_startup.f_a0b4;
    S->g_a0b8 = g_startup.f_a0b8;
    S->walk_speed = g_startup.f_2ba;
    S->impulse = g_startup.f_2ca;
    S->c8 = g_startup.f_2c8;
    S->c6 = g_startup.f_2c6;
    S->jump_flag = g_startup.f_306;
    S->anim_suppress = g_startup.f_30a;
    S->status_30b = g_startup.f_30b;
    S->paused_30c = g_startup.f_30c;
    S->anim_timer = g_startup.f_461b;
    S->shape_b = g_startup.f_461d;
    S->shape_a = g_startup.f_461f;
    S->player_block = g_startup.f_4619;
    S->shape_player = g_startup.f_4627;
    S->move_dir = g_startup.f_4629;
    S->g_5118 = g_startup.f_5118;
    S->g_5126 = g_startup.f_5126;
    S->g_5128 = g_startup.f_5128;
    S->g_5124 = g_startup.f_5124;
    S->cam_x = (int16_t)g_startup.f_512e;
    S->cam_y = (int16_t)g_startup.f_5130;
    S->gravity_scale = (int16_t)g_startup.f_5132;
    S->jump_phase = g_startup.f_5134;
    S->g_513c = g_startup.f_513c;
    S->g_513d = g_startup.f_513d;
    S->g_5140 = g_startup.f_5140;
    S->g_5142 = g_startup.f_5142;
    S->g_509c = g_startup.f_509c;
    S->g_509e = g_startup.f_509e;

    S->score_lo = g_startup.f_50a5;
    S->score_hi = g_startup.f_50a7;
    S->rng_lo = g_startup.f_2a6;
    S->rng_hi = g_startup.f_2a8;
    S->cooldown = g_startup.f_2e0;
    S->g_260 = g_startup.f_260;
    S->g_50dc = g_startup.f_50dc;
    S->g_50de = g_startup.f_50de;
    S->g_50e0 = g_startup.f_50e0;
    S->g_a0f0 = g_startup.f_a0f0;
    S->g_a0fa = g_startup.f_a0fa;
    S->g_2cc = g_startup.f_2cc; S->g_2ce = g_startup.f_2ce;
    S->g_2d4 = g_startup.f_2d4; S->g_2d6 = g_startup.f_2d6;
    S->g_2d8 = g_startup.f_2d8; S->g_2fa = g_startup.f_2fa;
    S->g_2fc = g_startup.f_2fc; S->g_2fe = g_startup.f_2fe;
    S->fire_impulse = g_startup.f_2da;
    S->g_a100 = 0;
    S->power_shot = g_startup.f_a0ff;
    S->power_timer = g_startup.f_a0c4;
    S->checkpoint_50a0 = g_startup.f_50a0;
    S->checkpoint_50be = g_startup.f_50be;
    S->checkpoint_50c4 = g_startup.f_50c4;
    S->checkpoint_50c7 = g_startup.f_50c7;
    S->checkpoint_50c9 = g_startup.f_50c9;
    S->g_2b5e = g_startup.f_2b5e;
    S->g_2b5c = 0;
    S->g_50a2 = g_startup.f_50a2;
    S->g_a0cc = g_startup.f_a0cc;
    S->next_level = (int16_t)g_startup.f_a0be;
    S->g_a0f5 = g_startup.f_a0f5;
    S->g_a0f6 = g_startup.f_a0f6;
    S->g_a0f7 = g_startup.f_a0f7;
    S->g_a0f9 = g_startup.f_a0f9;
    S->g_a0f3 = g_startup.f_a0f3;
    S->g_a0fb = g_startup.f_a0fb;
    S->g_a126 = g_startup.f_a126;
    S->g_a0e8 = g_startup.f_a0e8;
    S->g_a0d0 = g_startup.f_a0d0;
    S->g_a0d2 = g_startup.f_a0d2;
    S->g_a0d4 = g_startup.f_a0d4;
    S->g_a0d6 = g_startup.f_a0d6;
    S->g_a0d8 = g_startup.f_a0d8;
    S->g_a0da = g_startup.f_a0da;
    S->g_a0dc = g_startup.f_a0dc;
    S->g_a0de = g_startup.f_a0de;
    S->g_a0ce = g_startup.f_a0ce;
    S->g_a10b = g_startup.f_a10b;
    S->g_a0c2 = g_startup.f_a0c2;
    S->g_a0c8 = g_startup.f_a0c8;
    S->g_a0ca = g_startup.f_a0ca;
    S->g_a0e4 = g_startup.f_a0e4;
    S->g_a0c6 = 0;
    S->g_a10a = 0;
    S->g_a100 = 0;
    S->g_a101 = 1;
    S->g_a102 = 1;
    S->g_a104 = 1;
    S->g_a105 = 1;
    S->g_a106 = 1;
    S->g_a107 = 1;
    S->g_a108 = 1;
    S->g_a109 = 1;
    S->g_a10d = 0;
    S->g_a0f4 = 0;
    S->g_3395 = 0;
    S->g_3396 = 0;
    S->cave_flags[0] = 0;
    for (int i = 1; i <= 16; i++) S->cave_flags[i] = 1;
    S->g_3394 = 0;
    S->g_a110 = 0;
    S->g_512c = 0;
    cc_sound_log_clear();

    S->g_512a = 0;
    S->g_513a = 0;
    S->g_511a = 0;
    S->g_511c = 0;
    S->exit_col = 0;
    S->exit_row = 0;
    S->exit_step = 0;
    S->input_left = 0;
    S->input_right = 0;
    S->input_jump = 0;
    S->input_down = 0;
    S->input_fire = 0;
    S->input_joystick_fire = 0;
}

/* Apply the loader's per-level player/camera/spawn state. */
static void apply_level_state(int level, int use_start, int fresh) {
    const cc_level_start_t *st = &g_level_start_state[level];

    if (use_start) {
        /* Initial spawn cell from the Y token / generated level-start data. */
        S->player_x = st->f_4611;
        S->player_y = st->f_4613;
        S->cam_x = st->f_512e;
        S->cam_y = st->f_5130;
    } else {
        /* Transition spawn override ([0x2B72]/[0x2B74]).  The loader uses the
         * cell and centres the camera on it (0xB1A9): cam_x = col*16 - 0xA0,
         * cam_y = row*16 - 0x64, clamped to the 320x200 view bounds. */
        S->player_x = (int16_t)(S->spawn_col_override * 16);
        S->player_y = (int16_t)(S->spawn_row_override * 16);
        S->cam_x = (int16_t)(S->player_x - 0xA0);
        S->cam_y = (int16_t)(S->player_y - 0x64);
        if (S->cam_x < 0) S->cam_x = 0;
        if (S->cam_x > 0x140) S->cam_x = 0x140;
        if (S->cam_y < 0) S->cam_y = 0;
        if (S->cam_y > 0xC8) S->cam_y = 0xC8;
        /* Do not clear [0x2B72]/[0x2B74]: the loader only reads them, and the
         * cave exit reuses the hub door entry value to place the player next
         * to the door on return. */
    }
    S->player_block = st->f_4619;
    S->anim_timer = st->f_461b;
    S->shape_b = st->f_461d;
    S->shape_a = st->f_461f;
    S->shape_player = st->f_4627;
    S->move_dir = st->f_4629;
    /* Prologue state is reloaded with every level.  RNG is only seeded on a
     * fresh game load; transitions preserve the live LCG state. */
    S->fire_impulse = st->f_2da;
    S->g_2f2 = st->f_2f2;
    S->g_2b8 = st->f_2b8;
    S->g_511e = st->f_511e;
    S->g_5122 = st->f_5122;
    S->g_50dc = st->f_50dc;
    S->g_50de = st->f_50de;
    S->g_50e0 = st->f_50e0;
    /* The hub transition preserves these two cave-companion flags; every
     * other destination reloads them from its level prologue. */
    if (fresh || level != 2) {
        S->g_a0f0 = st->f_a0f0;
        S->g_a0fa = st->f_a0fa;
    }
    S->g_30d = st->f_30d;
    S->status_30b = st->f_30b;
    if (fresh) {
        S->g_2cc = st->f_2cc; S->g_2ce = st->f_2ce;
        S->g_2d4 = st->f_2d4; S->g_2d6 = st->f_2d6;
        S->g_2d8 = st->f_2d8; S->g_2fa = st->f_2fa;
        S->g_2fc = st->f_2fc; S->g_2fe = st->f_2fe;
    }
    S->checkpoint_50a0 = st->f_50a0;
    S->g_a0cc = st->f_a0cc;
    S->g_50a2 = st->f_50a2;
    S->next_level = (int16_t)st->f_a0be;
    S->g_a0f5 = st->f_a0f5;
    S->g_a0f6 = st->f_a0f6;
    S->g_a0f7 = st->f_a0f7;
    S->g_a0f9 = st->f_a0f9;
    S->g_a0f3 = st->f_a0f3;
    if (fresh) {
        S->exit_col = (int16_t)st->f_a0e0;
        S->exit_row = (int16_t)st->f_a0e2;
        S->exit_step = st->f_a0e6;
    }
    S->g_a0e8 = st->f_a0e8;
    S->g_a0d0 = st->f_a0d0;
    S->g_a0d2 = st->f_a0d2;
    S->g_a0d4 = st->f_a0d4;
    S->g_a0d6 = st->f_a0d6;
    S->g_a0d8 = st->f_a0d8;
    S->g_a0da = st->f_a0da;
    S->g_a0dc = st->f_a0dc;
    S->g_a0de = st->f_a0de;
    S->g_a0ce = st->f_a0ce;
    S->g_a10b = st->f_a10b;
    S->g_a0c2 = st->f_a0c2;
    S->g_a0c8 = st->f_a0c8;
    S->g_a0ca = st->f_a0ca;
    S->g_a0e4 = st->f_a0e4;
    S->g_a0c6 = 0;
    S->g_a100 = 0;
    S->g_a101 = 1;
    S->g_a102 = 1;
    S->g_a104 = 1;
    S->g_a105 = 1;
    S->g_a106 = 1;
    S->g_a107 = 1;
    S->g_a108 = 1;
    S->g_a109 = 1;
    S->g_a10a = 0;
    S->g_a10d = 0;
    S->g_a0f4 = 0;
    S->g_a0fb = st->f_a0fb;
    S->g_a126 = st->f_a126;
    if (fresh) {
        S->rng_lo = st->f_2a6;
        S->rng_hi = st->f_2a8;
    }
}


/* ------------------------------------------------------- transition overlay
 * Generated by tools/export_level_overlay.py from the real loader.  The
 * destination loader writes only these cell/object bytes; everything else is
 * preserved from the source level.  A few entries are replayed at runtime:
 * RNG-selected spawn fields, map-command shapes that read carried globals,
 * the hub's two RNG decoration recipes, and flag-driven hub door cells.
 */
static const unsigned char *g_level_overlay = 0;
void cc_set_level_overlay(const unsigned char *data) { g_level_overlay = data; }

static uint16_t ov_rd16(const unsigned char *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t ov_rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void ov_obj_byte(int slot, int off, unsigned char value) {
    unsigned char *raw = (unsigned char *)&S->obj[slot];
    raw[off] = value;
}

static void apply_level_overlay(int level) {
    const unsigned char *base = g_level_overlay;
    const unsigned char *p, *bits, *values;
    const unsigned char *cell_tag_p, *obj_lit_p, *obj_tag_p, *glob_p, *draw_p;
    uint16_t cell_tag_n, obj_lit_n, obj_tag_n, glob_n, draw_n, draw_n_raw;
    uint16_t draws[1024];
    uint16_t vi = 0, i, version;
    struct { uint16_t bit; uint8_t tag; uint16_t payload; } ct[256];
    struct { uint8_t slot, off, tag; uint16_t payload; } ot[256];
    uint16_t ct_n = 0, ot_n = 0;

    if (!base || level < 0 || level >= OBJ_LEVELS) return;
    if (ov_rd32(base) != 0x4F4C4343u) return;  /* "CCLO" */
    version = ov_rd16(base + 4);
    p = base + ov_rd32(base + 8 + level * 4);
    bits = p;
    values = p + 2500;
    for (i = 0; i < 20000; i++) {
        if (bits[i >> 3] & (1u << (i & 7))) {
            unsigned cell = i / 20, boff = i % 20;
            S->cells[cell / 40][cell % 40][boff] = values[vi++];
        }
    }
    p = values + vi;
    cell_tag_n = ov_rd16(p); p += 2;
    cell_tag_p = p; p += (unsigned)cell_tag_n * 5;
    for (i = 0; i < cell_tag_n && ct_n < 256; i++) {
        ct[ct_n].bit = ov_rd16(cell_tag_p + i * 5);
        ct[ct_n].tag = cell_tag_p[i * 5 + 2];
        ct[ct_n].payload = ov_rd16(cell_tag_p + i * 5 + 3);
        ct_n++;
    }
    obj_lit_n = ov_rd16(p); p += 2;
    obj_lit_p = p; p += (unsigned)obj_lit_n * 3;
    for (i = 0; i < obj_lit_n; i++)
        ov_obj_byte(obj_lit_p[i * 3], obj_lit_p[i * 3 + 1],
                    obj_lit_p[i * 3 + 2]);
    obj_tag_n = ov_rd16(p); p += 2;
    obj_tag_p = p; p += (unsigned)obj_tag_n * 5;
    for (i = 0; i < obj_tag_n && ot_n < 256; i++) {
        ot[ot_n].slot = obj_tag_p[i * 5];
        ot[ot_n].off = obj_tag_p[i * 5 + 1];
        ot[ot_n].tag = obj_tag_p[i * 5 + 2];
        ot[ot_n].payload = ov_rd16(obj_tag_p + i * 5 + 3);
        ot_n++;
    }
    glob_n = ov_rd16(p); p += 2;
    glob_p = p; p += (unsigned)glob_n * 5;
    for (i = 0; i < glob_n; i++) {
        uint16_t off = ov_rd16(glob_p + i * 5);
        uint8_t size = glob_p[i * 5 + 2];
        uint16_t val = ov_rd16(glob_p + i * 5 + 3);
        (void)size;
        if (off == 0x02A6 || off == 0x02A8) continue;  /* live RNG */
        /* The camera depends on the actual spawn override ([0x2B72]/[0x2B74]);
         * apply_level_state owns it.  The fixture overlay was captured without
         * an override, so replaying its camera would undo a cave->hub spawn. */
        if (off == 0x512E || off == 0x5130) continue;
        if (off == 0xA0E0) S->g_2f6 = 0xDEAD; /* DEBUG */
        cc_global_set(off, val);
    }
    draw_n_raw = ov_rd16(p); p += 2;
    draw_p = p;
    draw_n = draw_n_raw;
    if (draw_n > 1024) draw_n = 1024;
    if (version >= 2) {
        const unsigned char *decor_p = draw_p + (unsigned)draw_n_raw * 4;
        uint16_t decor_n = ov_rd16(decor_p);
        decor_p += 2;
        for (i = 0; i < decor_n; i++, decor_p += 6) {
            uint16_t bit = ov_rd16(decor_p);
            uint8_t kind = decor_p[2];
            uint8_t baseval = decor_p[3];
            uint8_t range = decor_p[4];
            uint8_t special = decor_p[5];
            uint16_t r = (uint16_t)cc_rng_next(range);
            unsigned char value;
            if (kind == 0) {
                if (r == 2) r = (uint16_t)cc_rng_next(range);
                value = (unsigned char)(baseval + r);
            } else {
                value = (r == 4) ? special : (unsigned char)(baseval + r);
            }
            S->cells[(bit / 20) / 40][(bit / 20) % 40][bit % 20] = value;
        }
        {
            uint16_t door_n = ov_rd16(decor_p);
            decor_p += 2;
            for (i = 0; i < door_n; i++, decor_p += 3) {
                uint16_t bit = ov_rd16(decor_p);
                uint8_t index = decor_p[2];
                unsigned cell = bit / 20;
                unsigned open = cc_global_get(0x50A8 + index) != 0;
                /* Hub door cells store the door number in w0 and use shape
                 * 0x0D when the cave flag is set, 0x10 when it is clear. */
                S->cells[cell / 40][cell % 40][bit % 20] = open ? index : 0;
                S->cells[cell / 40][cell % 40][6] = open ? 0x0D : 0x10;
            }
        }
    }
    for (i = 0; i < draw_n; i++) {
        uint16_t n = ov_rd16(draw_p + i * 4);
        uint16_t add = ov_rd16(draw_p + i * 4 + 2);
        draws[i] = (uint16_t)(cc_rng_next(n) + add);
    }
    for (i = 0; i < ct_n; i++) {
        unsigned cell = ct[i].bit / 20, boff = ct[i].bit % 20;
        unsigned char value;
        if (ct[i].tag == 2) {
            value = (unsigned char)cc_global_get(ct[i].payload);
        } else if (ct[i].tag == 1) {
            uint16_t di = ct[i].payload;
            value = (unsigned char)(di < draw_n ? draws[di] : 0);
        } else {
            continue;
        }
        S->cells[cell / 40][cell % 40][boff] = value;
    }
    for (i = 0; i < ot_n; i++) {
        unsigned char value;
        if (ot[i].tag == 2) {
            value = (unsigned char)cc_global_get(ot[i].payload);
        } else if (ot[i].tag == 1) {
            uint16_t di = ot[i].payload;
            uint16_t v = di < draw_n ? draws[di] : 0;
            value = (ot[i].off & 1) ? (unsigned char)(v >> 8)
                                    : (unsigned char)v;
        } else {
            continue;
        }
        ov_obj_byte(ot[i].slot, ot[i].off, value);
    }
}

static void load_level(int level, int new_game, int fresh_image, int sound_start) {
    if (level < 0 || level >= OBJ_LEVELS) level = 0;
    /* The original clears [0x513F] before reloading a level (0xEFCF). */
    cc_sound_reset();
    /* 0xCF18: the level-start effect (ID 0x24).  Shell story backgrounds
     * opt out so the finale effect (0x1A) can play instead. */
    if (sound_start) cc_sound_play(36);
    if (new_game) apply_startup_state();
    /* Fresh loads use the baked image.  Real transitions preserve the source
     * memory and replay only the bytes the loader actually writes.  A debug
     * warp also uses the baked image but keeps the current progress. */
    if (new_game || fresh_image || !g_level_overlay) {
        memcpy_(S->obj, g_obj_levels[level], sizeof(S->obj));
        {
            unsigned i;
            unsigned char *dst = (unsigned char *)S->cells;
            for (i = 0; i < sizeof(S->cells); i++) dst[i] = 0;
            if (g_cells_asset) {
                memcpy_(S->cells,
                        g_cells_asset + (unsigned)level * CELL_ROWS * CELL_COLS * 20,
                        sizeof(S->cells));
            }
        }
    }
    S->obj_cursor = g_level_cursor[level];
    S->a0f8 = 0;
    S->cur_level = (int16_t)level;
    /* The destination entry cell decides whether the door override is used:
     * cave -> hub spawns at the matching hub door ([0x2B72]/[0x2B74]),
     * while hub -> cave always uses the cave's Y-token start (the loader's
     * comparison fails because a hub cell has no meaning in a cave). */
    int use_start = (S->spawn_col_override == 0xffff) || (level != 2);
    apply_level_state(level, use_start, new_game || fresh_image);
    if (!new_game && !fresh_image) apply_level_overlay(level);
    if (level >= 2) S->g_a0cc = (uint16_t)count_crystal_cells();
}

void cc_objects_init(int level) {
    S->spawn_col_override = 0xffff;   /* no override on a fresh load */
    S->spawn_row_override = 0;
    load_level(level, 1, 1, 1);
}

void cc_objects_init_silent(int level) {
    S->spawn_col_override = 0xffff;
    S->spawn_row_override = 0;
    load_level(level, 1, 1, 0);
}

/* Debug warp: use the destination's baked image instead of replaying a real
 * door/exit transition, which would preserve source cave cells. */
void cc_warp_level(int level) {
    S->spawn_col_override = 0xffff;
    S->spawn_row_override = 0;
    load_level(level, 0, 1, 1);
}

/* Transition using the original loader argument (0=hub, 1..16=caves). */
void cc_level_exit(int arg) { load_arg(arg); }
void cc_load_arg(int arg) { load_arg(arg); }

void cc_sound_set_callback(void (*cb)(uint16_t id)) {
    g_sound_callback = cb;
}
void cc_sound_log_clear(void) {
    g_sound_log_count = 0;
    g_sound_log_total = 0;
}
int cc_sound_log_count(void) { return g_sound_log_count; }
int cc_sound_log_total(void) { return (int)g_sound_log_total; }
uint16_t cc_sound_log_at(int i) {
    return (i >= 0 && i < g_sound_log_count) ? g_sound_log[i] : 0;
}

int cc_obj_count(void) { return OBJ_MAX; }
uint16_t cc_obj_state(int i)  { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].state  : 0; }
int32_t  cc_obj_x(int i)      { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].x      : 0; }
int32_t  cc_obj_y(int i)      { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].y      : 0; }
uint16_t cc_obj_kind(int i)   { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].kind   : 0; }
uint16_t cc_obj_shape(int i)  { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].shape  : 0; }
uint16_t cc_obj_tile(int i)   { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].tile   : 0; }
uint16_t cc_obj_p10(int i)    { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].p10    : 0; }
/* Shape actually selected by the original draw loop: base + p10 - 1. */
uint16_t cc_obj_draw_shape(int i) {
    if (i < 0 || i >= OBJ_MAX) return 0;
    uint16_t base = S->obj[i].shape;
    if (!base) return 0;
    int sh = (int)base - 1 + (int)S->obj[i].p10;
    return sh < 1 ? 0 : (uint16_t)sh;
}
uint16_t cc_obj_p12(int i)    { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].p12    : 0; }
uint16_t cc_obj_p14(int i)    { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].p14    : 0; }
uint16_t cc_obj_p16(int i)    { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].p16    : 0; }
uint16_t cc_obj_p18(int i)    { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].p18    : 0; }
uint16_t cc_obj_p1a(int i)    { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].p1a    : 0; }
uint16_t cc_obj_ready(int i)  { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].ready  : 0; }
int32_t  cc_obj_vx(int i)     { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].vx     : 0; }
int32_t  cc_obj_vy(int i)     { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].vy     : 0; }
uint16_t cc_obj_speed(int i)  { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].speed  : 0; }
uint16_t cc_obj_p24(int i)    { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].p24    : 0; }
uint16_t cc_obj_cursor(void)  { return S->obj_cursor; }
void     cc_obj_set_cursor(int value) { S->obj_cursor = (uint16_t)value; }
/* Current level index (updated by cc_level_exit / cc_objects_init). */
int32_t  cc_cur_level(void)    { return S->cur_level; }
int cc_cell_byte(int col, int row, int off) {
    if (col < 0 || col >= CELL_COLS || row < 0 || row >= CELL_ROWS ||
        off < 0 || off >= 20) return -1;
    if (g_cells_asset) return S->cells[row][col][off];
    if (off == 17) return cc_cell_solid(S->cur_level, col, row);
    if (off == 19) return cc_cell_exit(S->cur_level, col, row);
    return 0;
}
void cc_cell_set(int col, int row, int off, int value) {
    if (col < 0 || col >= CELL_COLS || row < 0 || row >= CELL_ROWS ||
        off < 0 || off >= 20) return;
    S->cells[row][col][off] = (unsigned char)value;
}

/* ---------------------------------------------------------------- test seeding
 * Generic field access mirrors tools/emulate_load.OBJ_FIELDS offsets.
 * Returns signed 16-bit values for x/y/x_prev/y_prev/vx/vy and unsigned for
 * the word fields; callers compare with & 0xFFFF. */
int32_t cc_obj_get(int i, int off) {
    if (i < 0 || i >= OBJ_MAX) return 0;
    const ObjectEntry *o = &S->obj[i];
    switch (off) {
        case 0x00: return o->state;
        case 0x02: return o->p02;
        case 0x04: return o->p04;
        case 0x06: return (int16_t)o->x;
        case 0x08: return (int16_t)o->y;
        case 0x0a: return (int16_t)o->x_prev;
        case 0x0c: return (int16_t)o->y_prev;
        case 0x0e: return o->tile;
        case 0x10: return o->p10;
        case 0x12: return o->p12;
        case 0x14: return o->p14;
        case 0x16: return o->p16;
        case 0x18: return o->p18;
        case 0x1a: return o->p1a;
        case 0x1c: return o->shape;
        case 0x1e: return (int16_t)o->vx;
        case 0x20: return (int16_t)o->vy;
        case 0x22: return o->speed;
        case 0x24: return o->p24;
        case 0x26: return o->kind;
        case 0x28: return o->ready;
        case 0x29: return o->handler;
        case 0x2b: return o->handler_seg;
        default: return 0;
    }
}
void cc_obj_set(int i, int off, int32_t v) {
    if (i < 0 || i >= OBJ_MAX) return;
    ObjectEntry *o = &S->obj[i];
    switch (off) {
        case 0x00: o->state = (uint16_t)v; break;
        case 0x02: o->p02 = (uint16_t)v; break;
        case 0x04: o->p04 = (uint16_t)v; break;
        case 0x06: o->x = (int16_t)v; break;
        case 0x08: o->y = (int16_t)v; break;
        case 0x0a: o->x_prev = (int16_t)v; break;
        case 0x0c: o->y_prev = (int16_t)v; break;
        case 0x0e: o->tile = (uint16_t)v; break;
        case 0x10: o->p10 = (uint16_t)v; break;
        case 0x12: o->p12 = (uint16_t)v; break;
        case 0x14: o->p14 = (uint16_t)v; break;
        case 0x16: o->p16 = (uint16_t)v; break;
        case 0x18: o->p18 = (uint16_t)v; break;
        case 0x1a: o->p1a = (uint16_t)v; break;
        case 0x1c: o->shape = (uint16_t)v; break;
        case 0x1e: o->vx = (int16_t)v; break;
        case 0x20: o->vy = (int16_t)v; break;
        case 0x22: o->speed = (uint16_t)v; break;
        case 0x24: o->p24 = (uint16_t)v; break;
        case 0x26: o->kind = (uint16_t)v; break;
        case 0x28: o->ready = (uint8_t)v; break;
        case 0x29: o->handler = (uint16_t)v; break;
        case 0x2b: o->handler_seg = (uint16_t)v; break;
        default: break;
    }
}

/* Generic DS-offset access for the captured gameplay globals. */
int32_t cc_global_get(int off) {
    switch (off & 0xFFFF) {
        case 0x4611: return (int16_t)S->player_x;
        case 0x4613: return (int16_t)S->player_y;
        case 0xA0F1: return S->blocked_l;
        case 0xA0F2: return S->blocked_r;
        case 0xA0F6: return S->g_a0f6;
        case 0xA0F8: return S->a0f8;
        case 0xA0FC: return S->g_a0fc;
        case 0xA0FD: return S->airborne;
        case 0xA0EA: return S->g_a0ea;
        case 0xA0B4: return S->walk_phase;
        case 0xA0B8: return S->g_a0b8;
        case 0x02BA: return S->walk_speed;
        case 0x02CC: return S->g_2cc;
        case 0x02CE: return S->g_2ce;
        case 0x02D4: return S->g_2d4;
        case 0x02D6: return S->g_2d6;
        case 0x02D8: return S->g_2d8;
        case 0x02FA: return S->g_2fa;
        case 0x02FC: return S->g_2fc;
        case 0x02FE: return S->g_2fe;
        case 0x02CA: return S->impulse;
        case 0x02C8: return S->c8;
        case 0x02C6: return S->c6;
        case 0x02E0: return S->cooldown;
        case 0x0260: return S->g_260;
        case 0x50DC: return S->g_50dc;
        case 0x50DE: return S->g_50de;
        case 0x50E0: return S->g_50e0;
        case 0xA0F0: return S->g_a0f0;
        case 0xA0FA: return S->g_a0fa;
        case 0x0306: return S->jump_flag;
        case 0x030A: return S->anim_suppress;
        case 0x030B: return S->status_30b;
        case 0x030C: return S->paused_30c;
        case 0x461B: return S->anim_timer;
        case 0x461D: return S->shape_b;
        case 0x461F: return S->shape_a;
        case 0x4619: return S->player_block;
        case 0x4627: return S->shape_player;
        case 0x4629: return S->move_dir;
        case 0x460D: return S->obj[1].p02;
        case 0x5118: return S->g_5118;
        case 0x5126: return S->g_5126;
        case 0x5128: return S->g_5128;
        case 0x5124: return S->g_5124;
        case 0x512E: return (int16_t)S->cam_x;
        case 0x5130: return (int16_t)S->cam_y;
        case 0x5132: return S->gravity_scale;
        case 0x5134: return S->jump_phase;
        case 0x513C: return S->g_513c;
        case 0x513D: return S->g_513d;
        case 0x5140: return S->g_5140;
        case 0x5142: return S->g_5142;
        case 0x509C: return S->g_509c;
        case 0x509E: return S->g_509e;
        case 0x4660: return S->obj[2].ready;
        case 0x4652: return S->obj[2].p1a;
        case 0x02A6: return S->rng_lo;
        case 0x02A8: return S->rng_hi;
        case 0x02DA: return S->fire_impulse;
        case 0x02F2: return S->g_2f2;
        case 0x02B8: return S->g_2b8;
        case 0x2B5E: return S->g_2b5e;
        case 0x2B5C: return (int16_t)S->g_2b5c;
        case 0x511E: return S->g_511e;
        case 0x5122: return S->g_5122;
        case 0x50A5: return S->score_lo;
        case 0x50A7: return S->score_hi;
        case 0x50A0: return S->checkpoint_50a0;
        case 0x50BE: return S->checkpoint_50be;
        case 0x50C4: return S->checkpoint_50c4;
        case 0x50C7: return S->checkpoint_50c7;
        case 0x50C9: return S->checkpoint_50c9;
        case 0xA0FF: return S->power_shot;
        case 0xA0C4: return S->power_timer;
        case 0xA0CC: return S->g_a0cc;
        case 0x50A2: return S->g_50a2;
        case 0xA0F5: return S->g_a0f5;
        case 0xA0F7: return S->g_a0f7;
        case 0xA0F9: return S->g_a0f9;
        case 0xA0F3: return S->g_a0f3;
        case 0xA0FB: return S->g_a0fb;
        case 0xA126: return S->g_a126;
        case 0xA10B: return S->g_a10b;
        case 0xA10A: return S->g_a10a;
        case 0xA0E8: return S->g_a0e8;
        case 0xA0D0: return S->g_a0d0;
        case 0xA0D2: return S->g_a0d2;
        case 0xA0D4: return S->g_a0d4;
        case 0xA0D6: return S->g_a0d6;
        case 0xA0D8: return S->g_a0d8;
        case 0xA0DA: return S->g_a0da;
        case 0xA0DC: return S->g_a0dc;
        case 0xA0DE: return S->g_a0de;
        case 0xA0CE: return S->g_a0ce;
        case 0xA0C2: return S->g_a0c2;
        case 0xA0C8: return S->g_a0c8;
        case 0xA0CA: return S->g_a0ca;
        case 0xA0E4: return S->g_a0e4;
        case 0xA0C6: return S->g_a0c6;
        case 0xA0E0: return (uint16_t)S->exit_col;
        case 0xA0E2: return (uint16_t)S->exit_row;
        case 0xA0E6: return S->exit_step;
        case 0xA0BE: return (uint16_t)S->next_level;
        case 0xA100: return S->g_a100;
        case 0xA101: return S->g_a101;
        case 0xA102: return S->g_a102;
        case 0xA104: return S->g_a104;
        case 0xA105: return S->g_a105;
        case 0xA106: return S->g_a106;
        case 0xA107: return S->g_a107;
        case 0xA108: return S->g_a108;
        case 0xA109: return S->g_a109;
        case 0xA10D: return S->g_a10d;
        case 0xA0F4: return S->g_a0f4;
        case 0x3392: return S->g_3392;
        case 0x3395: return S->g_3395;
        case 0x3396: return S->g_3396;
        default:
            if ((off & 0xFFFF) >= 0x50A8 && (off & 0xFFFF) <= 0x50B8)
                return cave_flag_get((off & 0xFFFF) - 0x50A8);
            return 0;
    }
}
void cc_global_set(int off, int32_t v) {
    switch (off & 0xFFFF) {
        case 0x4611: S->player_x = (int16_t)v; S->obj[1].x = (int16_t)v; break;
        case 0x4613: S->player_y = (int16_t)v; S->obj[1].y = (int16_t)v; break;
        case 0xA0F1: S->blocked_l = (uint8_t)v; break;
        case 0xA0F2: S->blocked_r = (uint8_t)v; break;
        case 0xA0F6: S->g_a0f6 = (uint8_t)v; break;
        case 0xA0F8: S->a0f8 = (uint8_t)v; break;
        case 0xA0FC: S->g_a0fc = (uint8_t)v; break;
        case 0xA0FD: S->airborne = (uint8_t)v; break;
        case 0xA0EA: S->g_a0ea = (uint16_t)v; break;
        case 0xA0B4: S->walk_phase = (uint16_t)v; break;
        case 0xA0B8: S->g_a0b8 = (uint16_t)v; break;
        case 0x02BA: S->walk_speed = (uint16_t)v; break;
        case 0x02CC: S->g_2cc = (uint16_t)v; break;
        case 0x02CE: S->g_2ce = (uint16_t)v; break;
        case 0x02D4: S->g_2d4 = (uint16_t)v; break;
        case 0x02D6: S->g_2d6 = (uint16_t)v; break;
        case 0x02D8: S->g_2d8 = (uint16_t)v; break;
        case 0x02FA: S->g_2fa = (uint16_t)v; break;
        case 0x02FC: S->g_2fc = (uint16_t)v; break;
        case 0x02FE: S->g_2fe = (uint16_t)v; break;
        case 0x02CA: S->impulse = (uint16_t)v; break;
        case 0x02C8: S->c8 = (uint16_t)v; break;
        case 0x02C6: S->c6 = (uint16_t)v; break;
        case 0x02E0: S->cooldown = (uint16_t)v; break;
        case 0x0260: S->g_260 = (uint16_t)v; break;
        case 0x50DC: S->g_50dc = (uint16_t)v; break;
        case 0x50DE: S->g_50de = (uint16_t)v; break;
        case 0x50E0: S->g_50e0 = (uint16_t)v; break;
        case 0xA0F0: S->g_a0f0 = (uint8_t)v; break;
        case 0xA0FA: S->g_a0fa = (uint8_t)v; break;
        case 0x0306: S->jump_flag = (uint8_t)v; break;
        case 0x030A: S->anim_suppress = (uint8_t)v; break;
        case 0x030B: S->status_30b = (uint8_t)v; break;
        case 0x030C: S->paused_30c = (uint8_t)v; break;
        case 0x461B: S->anim_timer = (uint16_t)v; S->obj[1].p10 = (uint16_t)v; break;
        case 0x461D: S->shape_b = (uint16_t)v; S->obj[1].p12 = (uint16_t)v; break;
        case 0x461F: S->shape_a = (uint16_t)v; S->obj[1].p14 = (uint16_t)v; break;
        case 0x4619: S->player_block = (uint16_t)v; S->obj[1].tile = (uint16_t)v; break;
        case 0x4627: S->shape_player = (uint16_t)v; S->obj[1].shape = (uint16_t)v; break;
        case 0x4629: S->move_dir = (uint16_t)v; S->obj[1].vx = (int16_t)v; break;
        case 0x460D: S->obj[1].p02 = (uint16_t)v; break;
        case 0x5118: S->g_5118 = (uint16_t)v; break;
        case 0x5126: S->g_5126 = (uint16_t)v; break;
        case 0x5128: S->g_5128 = (uint16_t)v; break;
        case 0x5124: S->g_5124 = (uint16_t)v; break;
        case 0x512E: S->cam_x = (int16_t)v; break;
        case 0x5130: S->cam_y = (int16_t)v; break;
        case 0x5132: S->gravity_scale = (int16_t)v; break;
        case 0x5134: S->jump_phase = (uint16_t)v; break;
        case 0x513C: S->g_513c = (uint8_t)v; break;
        case 0x513D: S->g_513d = (uint8_t)v; break;
        case 0x5140: S->g_5140 = (uint8_t)v; break;
        case 0x5142: S->g_5142 = (uint8_t)v; break;
        case 0x509C: S->g_509c = (uint16_t)v; break;
        case 0x509E: S->g_509e = (uint16_t)v; break;
        case 0x4660: S->obj[2].ready = (uint8_t)v; break;
        case 0x4652: S->obj[2].p1a = (uint16_t)v; break;
        case 0x02A6: S->rng_lo = (uint16_t)v; break;
        case 0x02A8: S->rng_hi = (uint16_t)v; break;
        case 0x02DA: S->fire_impulse = (uint16_t)v; break;
        case 0x02F2: S->g_2f2 = (uint16_t)v; break;
        case 0x02B8: S->g_2b8 = (uint16_t)v; break;
        case 0x2B5E: S->g_2b5e = (uint16_t)v; break;
        case 0x2B5C: S->g_2b5c = (int16_t)v; break;
        case 0x511E: S->g_511e = (uint16_t)v; break;
        case 0x5122: S->g_5122 = (uint16_t)v; break;
        case 0x50A5: S->score_lo = (uint16_t)v; break;
        case 0x50A7: S->score_hi = (uint16_t)v; break;
        case 0x50A0: S->checkpoint_50a0 = (uint16_t)v; break;
        case 0x50BE: S->checkpoint_50be = (uint16_t)v; break;
        case 0x50C4: S->checkpoint_50c4 = (uint16_t)v; break;
        case 0x50C7: S->checkpoint_50c7 = (uint16_t)v; break;
        case 0x50C9: S->checkpoint_50c9 = (uint16_t)v; break;
        case 0xA0FF: S->power_shot = (uint8_t)v; break;
        case 0xA0C4: S->power_timer = (uint16_t)v; break;
        case 0xA0CC: S->g_a0cc = (uint16_t)v; break;
        case 0x50A2: S->g_50a2 = (uint16_t)v; break;
        case 0xA0F5: S->g_a0f5 = (uint8_t)v; break;
        case 0xA0F7: S->g_a0f7 = (uint8_t)v; break;
        case 0xA0F9: S->g_a0f9 = (uint8_t)v; break;
        case 0xA0F3: S->g_a0f3 = (uint8_t)v; break;
        case 0xA0FB: S->g_a0fb = (uint8_t)v; break;
        case 0xA126: S->g_a126 = (uint8_t)v; break;
        case 0xA10B: S->g_a10b = (uint8_t)v; break;
        case 0xA10A: S->g_a10a = (uint8_t)v; break;
        case 0xA0E8: S->g_a0e8 = (uint16_t)v; break;
        case 0xA0D0: S->g_a0d0 = (uint16_t)v; break;
        case 0xA0D2: S->g_a0d2 = (uint16_t)v; break;
        case 0xA0D4: S->g_a0d4 = (uint16_t)v; break;
        case 0xA0D6: S->g_a0d6 = (uint16_t)v; break;
        case 0xA0D8: S->g_a0d8 = (uint16_t)v; break;
        case 0xA0DA: S->g_a0da = (uint16_t)v; break;
        case 0xA0DC: S->g_a0dc = (uint16_t)v; break;
        case 0xA0DE: S->g_a0de = (uint16_t)v; break;
        case 0xA0CE: S->g_a0ce = (uint16_t)v; break;
        case 0xA0C2: S->g_a0c2 = (uint16_t)v; break;
        case 0xA0C8: S->g_a0c8 = (uint16_t)v; break;
        case 0xA0CA: S->g_a0ca = (uint16_t)v; break;
        case 0xA0E4: S->g_a0e4 = (uint16_t)v; break;
        case 0xA0C6: S->g_a0c6 = (uint16_t)v; break;
        case 0xA0E0: S->exit_col = (int16_t)v; break;
        case 0xA0E2: S->exit_row = (int16_t)v; break;
        case 0xA0E6: S->exit_step = (uint16_t)v; break;
        case 0xA0BE: S->next_level = (int16_t)v; break;
        case 0xA100: S->g_a100 = (uint8_t)v; break;
        case 0xA101: S->g_a101 = (uint8_t)v; break;
        case 0xA102: S->g_a102 = (uint8_t)v; break;
        case 0xA104: S->g_a104 = (uint8_t)v; break;
        case 0xA105: S->g_a105 = (uint8_t)v; break;
        case 0xA106: S->g_a106 = (uint8_t)v; break;
        case 0xA107: S->g_a107 = (uint8_t)v; break;
        case 0xA108: S->g_a108 = (uint8_t)v; break;
        case 0xA109: S->g_a109 = (uint8_t)v; break;
        case 0xA10D: S->g_a10d = (uint8_t)v; break;
        case 0xA0F4: S->g_a0f4 = (uint8_t)v; break;
        case 0x3392: S->g_3392 = (uint8_t)v; break;
        case 0x3395: S->g_3395 = (uint8_t)v; break;
        case 0x3396: S->g_3396 = (uint8_t)v; break;
        default:
            if ((off & 0xFFFF) >= 0x50A8 && (off & 0xFFFF) <= 0x50B8)
                cave_flag_set((off & 0xFFFF) - 0x50A8, (int)v);
            break;
    }
}

/* ---------------------------------------------------------------- RNG
 * 0x2BB5:0x980 / generator at 0x2C51F.  The high word is computed by the
 * original's byte-wise multiply sequence, not a plain 32-bit multiply; the
 * two differ from the second draw onward.  Reproduce the register operations
 * exactly.  Returns (high word) % n; n == 0 returns 0. */
static uint16_t rng_next_hi(void) {
    uint16_t ax = S->rng_lo;
    uint16_t bx = S->rng_hi;
    uint16_t cx = (uint16_t)(ax << 3);
    uint32_t prod = (uint32_t)ax * 0x8405u;
    uint16_t dx = (uint16_t)(prod >> 16);
    uint8_t dh, ch;
    ax = (uint16_t)prod;

    ch = (uint8_t)((cx >> 8) + (cx & 0xFF));
    cx = (uint16_t)((ch << 8) | (cx & 0xFF));
    dx = (uint16_t)(dx + cx);
    dx = (uint16_t)(dx + bx);

    bx = (uint16_t)(bx << 2);
    dx = (uint16_t)(dx + bx);
    dh = (uint8_t)((dx >> 8) + (bx & 0xFF));
    dx = (uint16_t)((dh << 8) | (dx & 0xFF));

    bx = (uint16_t)(bx << 5);
    dh = (uint8_t)((dx >> 8) + (bx & 0xFF));
    dx = (uint16_t)((dh << 8) | (dx & 0xFF));

    ax = (uint16_t)(ax + 1);
    if (ax == 0) dx = (uint16_t)(dx + 1);

    S->rng_lo = ax;
    S->rng_hi = dx;
    return dx;
}
uint16_t cc_rng_next(uint16_t n) {
    uint16_t hi = rng_next_hi();
    return n ? (uint16_t)(hi % n) : 0;
}
void cc_rng_seed(uint16_t lo, uint16_t hi) {
    S->rng_lo = lo;
    S->rng_hi = hi;
}
uint16_t cc_rng_lo(void) { return S->rng_lo; }
uint16_t cc_rng_hi(void) { return S->rng_hi; }
/* Handler offset (behaviour ID, segment 0x1F9A) for slot i. */
uint16_t cc_obj_handler(int i)  { return (i >= 0 && i < OBJ_MAX) ? S->obj[i].handler : 0; }

/* Coverage metric: fraction of active (non-empty, non-inert-state) object
 * slots whose handler has a real implementation rather than the inert
 * fallback.  Used to track handler porting progress (Phase 5). */
static int is_real_handler(uint16_t off) {
    switch (off) {
        case 0x1fb9: case 0x2241: case 0x2850: case 0x2877:
        case 0x831: case 0x1fc2: case 0x2176: case 0x323c: case 0x3be8:
        case 0x3d23: case 0x3e70: case 0x3f13: case 0x4a4b: case 0x4a82:
        case 0x298a: case 0x2d37: case 0x3416: case 0x2acf: case 0x2bd4:
        case 0x403a: case 0x55ec: case 0x62f2: case 0x6232: case 0x4c92:
        case 0x5c8f: case 0x4eeb: case 0x52e7: case 0x2f83: case 0x4ad3:
        case 0x36b0: case 0x3a2e: case 0x3f6c: case 0x2e25:
        case 0x3904: case 0x4145: case 0x47b9: case 0x32f6:
        case 0x48fe: case 0x4fd4: case 0x6458:
        case 0x5855: case 0x5bbe: case 0x6681: case 0x672b: case 0x69d3:
        case 0x6afa: case 0x6d31: case 0x6ff6: case 0x70e4:
            return 1;
        default: return 0;
    }
}
int cc_handler_coverage_num(void) {
    int n = 0, d = 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        if (S->obj[i].kind == 0) continue;
        if (S->obj[i].state == 2) continue;
        d++;
        if (is_real_handler(S->obj[i].handler)) n++;
    }
    return d ? (n * 100) / d : 100;
}

/* C4 exports. */
void     cc_set_player(int32_t x, int32_t y) { S->player_x = (int16_t)x; S->player_y = (int16_t)y; S->obj[1].x = (int16_t)x; S->obj[1].y = (int16_t)y; }
int32_t  cc_player_x(void) { return S->player_x; }
int32_t  cc_player_y(void) { return S->player_y; }
int32_t  cc_cam_x(void)    { return S->cam_x; }
int32_t  cc_cam_y(void)    { return S->cam_y; }
int16_t  cc_move_dir(void) { return S->move_dir; }
int      cc_collide_player(int mode, int16_t radius);

/* C5 exports: score / collectibles / lives / sound. */
uint32_t cc_score(void)       { return ((uint32_t)S->score_hi << 16) | S->score_lo; }
uint16_t cc_score_lo(void)    { return S->score_lo; }
uint16_t cc_score_hi(void)    { return S->score_hi; }
uint16_t cc_cooldown(void)    { return S->cooldown; }
uint16_t cc_g_2da(void)       { return S->fire_impulse; }
uint16_t cc_g_a0c4(void)      { return S->power_timer; }
uint8_t  cc_g_a0ff(void)      { return S->power_shot; }
uint16_t cc_collectibles_total(void)  {
    return (uint16_t)(S->g_50a2 + S->g_a0cc);
}
uint16_t cc_collectibles_left(void)   { return S->g_a0cc; }
uint16_t cc_lives(void)             { return S->g_509e; }
uint16_t cc_health(void)            { return S->g_509e; }
uint16_t cc_ammo(void)              { return S->g_509c; }
uint16_t cc_crystals_collected(void) { return S->g_50a2; }
uint16_t cc_crystals_remaining(void) { return S->g_a0cc; }
int16_t  cc_level_arg(void)          { return S->next_level; }
uint8_t  cc_exit_unlocked(void)      { return S->g_a0f5; }
uint8_t  cc_exit_active(void)        { return S->g_a0f6; }
uint8_t  cc_door_active(void)        { return S->g_a0f7; }
int      cc_cave_flag(int idx)       { return cave_flag_get(idx); }
int      cc_level_complete(void)     { return S->g_a0fb; }

/* ---------------------------------------------------------------- physics debug
 * Field order must stay in sync with enum cc_phys_field / tools fixtures. */
int32_t cc_phys_get(int f) {
    switch (f) {
        case CC_PHYS_X: return S->player_x;
        case CC_PHYS_Y: return S->player_y;
        case CC_PHYS_CAM_X: return S->cam_x;
        case CC_PHYS_CAM_Y: return S->cam_y;
        case CC_PHYS_MOVE_DIR: return (int16_t)S->move_dir;
        case CC_PHYS_WALK_SPEED: return S->walk_speed;
        case CC_PHYS_IMPULSE: return S->impulse;
        case CC_PHYS_WALK_PHASE: return S->walk_phase;
        case CC_PHYS_JUMP_PHASE: return S->jump_phase;
        case CC_PHYS_JUMP_FLAG: return S->jump_flag;
        case CC_PHYS_AIRBORNE: return S->airborne;
        case CC_PHYS_ANIM_SUPPRESS: return S->anim_suppress;
        case CC_PHYS_SHAPE: return S->shape_player;
        case CC_PHYS_SHAPE_A: return S->shape_a;
        case CC_PHYS_SHAPE_B: return S->shape_b;
        case CC_PHYS_PLAYER_BLOCK: return S->player_block;
        case CC_PHYS_ANIM_TIMER: return S->anim_timer;
        case CC_PHYS_CARRY_X: return S->g_5118;
        case CC_PHYS_SCROLL_X: return S->g_5126;
        case CC_PHYS_CARRY_Y: return S->g_5128;
        case CC_PHYS_RIDE_H: return S->g_513d;
        case CC_PHYS_RIDE_V: return S->g_5142;
        case CC_PHYS_BLOCKED_L: return S->blocked_l;
        case CC_PHYS_BLOCKED_R: return S->blocked_r;
        case CC_PHYS_GRAVITY: return S->gravity_scale;
        case CC_PHYS_C8: return S->c8;
        case CC_PHYS_C6: return S->c6;
        case CC_PHYS_LIVES: return S->g_509e;
        case CC_PHYS_AMMO: return S->g_509c;
        case CC_PHYS_INVULN: return S->g_a0ea;
        case CC_PHYS_HIT_LOCK: return S->obj[2].ready;
        case CC_PHYS_EXIT_STATE: return S->g_5124;
        default: return 0;
    }
}

void cc_phys_set(int f, int32_t v) {
    switch (f) {
        case CC_PHYS_X: S->player_x = (int16_t)v; break;
        case CC_PHYS_Y: S->player_y = (int16_t)v; break;
        case CC_PHYS_CAM_X: S->cam_x = (int16_t)v; break;
        case CC_PHYS_CAM_Y: S->cam_y = (int16_t)v; break;
        case CC_PHYS_MOVE_DIR: S->move_dir = (uint16_t)v; break;
        case CC_PHYS_WALK_SPEED: S->walk_speed = (uint16_t)v; break;
        case CC_PHYS_IMPULSE: S->impulse = (uint16_t)v; break;
        case CC_PHYS_WALK_PHASE: S->walk_phase = (uint16_t)v; break;
        case CC_PHYS_JUMP_PHASE: S->jump_phase = (uint16_t)v; break;
        case CC_PHYS_JUMP_FLAG: S->jump_flag = (uint8_t)v; break;
        case CC_PHYS_AIRBORNE: S->airborne = (uint8_t)v; break;
        case CC_PHYS_ANIM_SUPPRESS: S->anim_suppress = (uint8_t)v; break;
        case CC_PHYS_SHAPE: S->shape_player = (uint16_t)v; break;
        case CC_PHYS_SHAPE_A: S->shape_a = (uint16_t)v; break;
        case CC_PHYS_SHAPE_B: S->shape_b = (uint16_t)v; break;
        case CC_PHYS_PLAYER_BLOCK: S->player_block = (uint16_t)v; break;
        case CC_PHYS_ANIM_TIMER: S->anim_timer = (uint16_t)v; break;
        case CC_PHYS_CARRY_X: S->g_5118 = (uint16_t)v; break;
        case CC_PHYS_SCROLL_X: S->g_5126 = (uint16_t)v; break;
        case CC_PHYS_CARRY_Y: S->g_5128 = (uint16_t)v; break;
        case CC_PHYS_RIDE_H: S->g_513d = (uint8_t)v; break;
        case CC_PHYS_RIDE_V: S->g_5142 = (uint8_t)v; break;
        case CC_PHYS_BLOCKED_L: S->blocked_l = (uint8_t)v; break;
        case CC_PHYS_BLOCKED_R: S->blocked_r = (uint8_t)v; break;
        case CC_PHYS_GRAVITY: S->gravity_scale = (int16_t)v; break;
        case CC_PHYS_C8: S->c8 = (uint16_t)v; break;
        case CC_PHYS_C6: S->c6 = (uint16_t)v; break;
        case CC_PHYS_LIVES: S->g_509e = (uint16_t)v; break;
        case CC_PHYS_AMMO: S->g_509c = (uint16_t)v; break;
        case CC_PHYS_INVULN: S->g_a0ea = (uint16_t)v; break;
        case CC_PHYS_HIT_LOCK: S->obj[2].ready = (uint8_t)v; break;
        case CC_PHYS_EXIT_STATE: S->g_5124 = (uint16_t)v; break;
        default: break;
    }
}

/* ---------------------------------------------------------------- save state
 * Versioned deterministic snapshot.
 *
 * v2 header (12 bytes, little-endian):
 *   0..3   magic "CCS2"
 *   4..5   version 2
 *   6..7   payload size (sizeof(cc_state_t))
 *   8..11  CRC-32/ISO-HDLC of the payload
 *   12..   raw cc_state_t image
 *
 * v1 (Phase 5) used an 8-byte "CCS1" header and a 23,540-byte payload that
 * contained host-only sound fields after the deterministic prefix.  v1 is
 * accepted and migrated by copying the first sizeof(cc_state_t) bytes. */
#define CC_SAVE_MAGIC_V1   0x31534343u /* "CCS1" */
#define CC_SAVE_MAGIC_V2   0x32534343u /* "CCS2" */
#define CC_SAVE_VERSION_V1 1u
#define CC_SAVE_VERSION_V2 2u
#define CC_SAVE_HDR_V1     8
#define CC_SAVE_HDR_V2     12
#define CC_SAVE_V1_PAYLOAD 23540

static cc_state_t g_load_tmp;
static int g_state_error = CC_STATE_OK;

static uint16_t rd16_(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd32_(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16_(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)(v >> 8);
}
static void wr32_(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}
static uint32_t crc32_(const unsigned char *p, int n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n-- > 0) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static int state_valid_(const cc_state_t *st) {
    if (st->cur_level < 0 || st->cur_level > 18) return 0;
    if (st->next_level < -2 || st->next_level > 18) return 0;
    if (st->obj_cursor >= OBJ_MAX) return 0;
    for (int i = 0; i < 17; i++)
        if (st->cave_flags[i] > 1) return 0;
    if (st->spawn_col_override != 0xFFFF && st->spawn_col_override > 40)
        return 0;
    if (st->spawn_row_override != 0xFFFF && st->spawn_row_override > 24)
        return 0;
    if (st->exit_col < 0 || st->exit_col >= CELL_COLS) return 0;
    if (st->exit_row < 0 || st->exit_row >= CELL_ROWS) return 0;
    if (st->g_5124 > 3) return 0;
    return 1;
}

int cc_state_size(void) {
    int v2 = CC_SAVE_HDR_V2 + (int)sizeof(cc_state_t);
    int v1 = CC_SAVE_HDR_V1 + CC_SAVE_V1_PAYLOAD;
    return v2 > v1 ? v2 : v1;
}

int cc_state_save(unsigned char *out, int cap) {
    int need = CC_SAVE_HDR_V2 + (int)sizeof(cc_state_t);
    g_state_error = CC_STATE_OK;
    if (!out || cap < need) {
        g_state_error = CC_STATE_ERR_ARG;
        return 0;
    }
    memcpy_(out + CC_SAVE_HDR_V2, S, sizeof(cc_state_t));
    wr32_(out, CC_SAVE_MAGIC_V2);
    wr16_(out + 4, CC_SAVE_VERSION_V2);
    wr16_(out + 6, (uint16_t)sizeof(cc_state_t));
    wr32_(out + 8, crc32_(out + CC_SAVE_HDR_V2, (int)sizeof(cc_state_t)));
    return need;
}

int cc_state_load_ex(const unsigned char *in, int len, int flags) {
    g_state_error = CC_STATE_OK;
    if (!in || len < CC_SAVE_HDR_V1) {
        g_state_error = CC_STATE_ERR_ARG;
        return 0;
    }
    uint32_t magic = rd32_(in);
    uint16_t ver = rd16_(in + 4);
    uint16_t sz = rd16_(in + 6);
    if (magic == CC_SAVE_MAGIC_V2) {
        if (ver != CC_SAVE_VERSION_V2) {
            g_state_error = CC_STATE_ERR_VERSION;
            return 0;
        }
        if (sz != (uint16_t)sizeof(cc_state_t) ||
            len < CC_SAVE_HDR_V2 + (int)sz) {
            g_state_error = CC_STATE_ERR_SIZE;
            return 0;
        }
        if (rd32_(in + 8) != crc32_(in + CC_SAVE_HDR_V2, (int)sz)) {
            g_state_error = CC_STATE_ERR_CRC;
            return 0;
        }
        memcpy_(&g_load_tmp, in + CC_SAVE_HDR_V2, sizeof(cc_state_t));
    } else if (magic == CC_SAVE_MAGIC_V1) {
        if (ver != CC_SAVE_VERSION_V1) {
            g_state_error = CC_STATE_ERR_VERSION;
            return 0;
        }
        if (sz != CC_SAVE_V1_PAYLOAD || len < CC_SAVE_HDR_V1 + (int)sz) {
            g_state_error = CC_STATE_ERR_SIZE;
            return 0;
        }
        /* The v1 gameplay prefix is layout-identical; the old tail held
         * host-only sound state and dead collectible fields. */
        memcpy_(&g_load_tmp, in + CC_SAVE_HDR_V1, sizeof(cc_state_t));
    } else {
        g_state_error = CC_STATE_ERR_MAGIC;
        return 0;
    }
    if (!state_valid_(&g_load_tmp)) {
        g_state_error = CC_STATE_ERR_INVALID;
        return 0;
    }
    if ((flags & CC_STATE_LOAD_HUB_ONLY) && g_load_tmp.cur_level != 2) {
        g_state_error = CC_STATE_ERR_NOT_HUB;
        return 0;
    }
    memcpy_(S, &g_load_tmp, sizeof(cc_state_t));
    cc_sound_reset();
    cc_sound_log_clear();
    return 1;
}

int cc_state_load(const unsigned char *in, int len) {
    return cc_state_load_ex(in, len, 0);
}

int cc_state_last_error(void) { return g_state_error; }
