#ifndef GAME_STATE_H
#define GAME_STATE_H

/* Shared gameplay state.
 *
 * This is the single cc_state_t image mirrored by the original DS globals.
 * Field comments give the original memory location each mirrors; the layout
 * is frozen by the asserts in state.c.
 */
#include <stdint.h>

#include "object.h"
#include "cell.h"

#define SCREEN_W 320
#define SCREEN_H 200
#define CC_OFFSETOF(type, field) __builtin_offsetof(type, field)

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
    uint16_t carry_x_delta;             /* [0x5118] horizontal carry delta */
    uint16_t carry_y_delta;             /* [0x5128] vertical carry delta */
    uint16_t scroll_x_accum;             /* [0x5126] horizontal scroll accumulator */

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
    uint16_t crystals_remaining;        /* [0xA0CC] crystals remaining in this cave */
    uint16_t crystals_collected;        /* [0x50A2] crystals collected */
    uint8_t  exit_unlocked;             /* [0xA0F5] exit unlocked (all crystals) */
    uint8_t  hub_door_active;           /* [0xA0F7] hub-door entry sequence */
    uint8_t  switch_latch;              /* [0xA0F9] switch/door latch */
    uint8_t  lever_state;               /* [0xA0F3] lever/door state */
    uint8_t  all_caves_complete;        /* [0xA0FB] all caves complete/finale */
    uint8_t  story_transition;          /* [0xA126] story/menu transition */
    uint8_t  collectible_spawn_active;  /* [0xA10B] collectible spawn active */
    uint8_t  suppress_base_stamp;       /* [0xA10A] spawn base-cell suppress */
    uint16_t switch_repeat;             /* [0xA0E8] switch repeat counter */
    uint16_t spawn_cell_x;              /* [0xA0D0] spawned-cell px */
    uint16_t spawn_cell_y;              /* [0xA0D2] spawned-cell py */
    uint16_t switch_a_col;              /* [0xA0D4] switch A cell (raw col+1) */
    uint16_t switch_a_row;              /* [0xA0D6] switch A cell (raw row+1) */
    uint16_t switch_b_col;              /* [0xA0D8] switch B cell (raw col+1) */
    uint16_t switch_b_row;              /* [0xA0DA] switch B cell (raw row+1) */
    uint16_t switch_c_col;              /* [0xA0DC] switch C cell (raw col+1) */
    uint16_t switch_c_row;              /* [0xA0DE] switch C cell (raw row+1) */
    uint16_t collectible_spawn_timer;   /* [0xA0CE] collectible spawn timer */
    uint16_t power_shot_timer_preset;   /* [0xA0C2] power-shot timer preset */
    uint16_t gravity_timer_preset;      /* [0xA0C8] gravity-flip timer preset */
    uint16_t red_mushroom_timer_preset; /* [0xA0CA] red-mushroom timer preset */
    uint16_t stun_timer_preset;         /* [0xA0E4] stun timer preset */
    uint16_t power_subtimer;            /* [0xA0C6] power-shot sub-timer */
    uint8_t  g_a100;                    /* [0xA100] timer event latch */
    uint8_t  g_a101;                    /* [0xA101] switch animation latch */
    uint8_t  g_a102;                    /* [0xA102] switch animation latch */
    uint8_t  g_a104;                    /* [0xA104] power-up animation latch */
    uint8_t  g_a105;                    /* [0xA105] switch animation latch */
    uint8_t  g_a106;                    /* [0xA106] exit-open latch */
    uint8_t  g_a107;                    /* [0xA107] exit-open latch */
    uint8_t  g_a108;                    /* [0xA108] lever animation latch */
    uint8_t  g_a109;                    /* [0xA109] switch animation latch */
    uint8_t  g_a10d;                    /* [0xA10D] gravity switch latch */
    uint8_t  g_a0f4;                    /* [0xA0F4] joystick fire latch */
    uint8_t  g_3392;                    /* [0x3392] joystick latch */
    uint8_t  g_3395;                    /* [0x3395] story/exit key latch */
    uint8_t  g_3396;                    /* [0x3396] story/exit key latch */
    uint8_t  cave_flags[17];            /* [0x50A8 + arg], 1 = available/uncompleted */
    uint8_t  level_block[0x21];         /* [0x5098] checkpoint/level state block */
    uint8_t  checkpoint_block[0x21];    /* [0x50BA] saved copy */

    /* Handler cross-reference ([0x512A]/[0x513A]) used by kind-33 objects. */
    uint16_t g_512a;
    uint16_t g_513a;

    /* [0x511a]/[0x511c] platform attachment slots. */
    uint16_t platform_slot_h;
    uint16_t platform_slot_v;
    uint8_t  interaction_enabled;       /* [0x513c] interaction enabled */
    uint8_t  interaction_suppressed;    /* [0x5140] interaction suppression */
    uint8_t  riding_horizontal;         /* [0x513d] riding a horizontal platform */
    uint8_t  riding_vertical;           /* [0x5142] riding a vertical platform */
    uint16_t death_state;               /* [0x5124] interaction/death state */
    uint16_t invuln_timer;              /* [0xa0ea] hit/invulnerability state */
    uint8_t  interaction_lock;          /* [0xa0f6] interaction lock */
    uint16_t health;                    /* [0x509e] life counter (HUD border cell) */
    uint16_t ammo;                      /* [0x509c] ammo counter (HUD border cell) */

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
    uint16_t restart_timer;             /* [0x2b5e] restart timer */
    int16_t  text_scroll;             /* [0x2b5c] text-window vertical scroll */
    uint8_t  restart_input_latch;             /* [0x3394] restart input latch */
    uint8_t  restart_latch;             /* [0xa110] restart latch */
    uint8_t  power_shot_active;         /* [0xa0ff] power-shot active */
    uint16_t power_timer;        /* [0xa0c4] power-shot expiry */
    uint16_t checkpoint_50a0;    /* [0x50a0] restart checkpoint */
    uint16_t checkpoint_50be;    /* [0x50be] */
    uint16_t checkpoint_50c4;    /* [0x50c4] */
    uint16_t checkpoint_50c7;    /* [0x50c7] */
    uint16_t checkpoint_50c9;    /* [0x50c9] */
} cc_state_t;

/* Shared file-scope state.  Definitions live in the owning module. */
extern cc_state_t g_state;
extern const unsigned char *g_cells_asset;
extern uint32_t g_unknown_handler_hits;
extern const unsigned char *g_level_overlay;
extern cc_state_t g_load_tmp;
extern int g_state_error;

#endif /* GAME_STATE_H */
