/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"


/* ---------------------------------------------------------------- cells
 * 0x0FC5:0xBB26 (linear 0x1B776): per-cell event handler.  The original is
 * called by 0xE5D3 for several player-relative points each frame.  All cell
 * fields are words except the solid/exit flags at +0x11/+0x13. */
void helper_a110_clear(void) { g_state.restart_latch = 0; }
void helper_a59d(void) { g_state.restart_latch = 0; g_state.g_3395 = 0; }
void helper_toggle_gravity(void) {
    if (g_state.gravity_scale == -1) {
        g_state.gravity_scale = 1;
        g_state.player_block = 6;
    } else {
        g_state.gravity_scale = -1;
        g_state.player_block = 8;
    }
    g_state.g_3392 = 0;
    g_state.airborne = 1;
    g_state.obj[1].block = g_state.player_block;
}
/* 0x0FC5:0xB607: a switch cell toggles the objects of kind `kind` in slots
 * 4..obj_cursor.  For the moving kinds (0x27/0x3B/0x26) the original flips
 * *speed* (+0x22) between 0 and 2 -- the loader parks switchable elevators at
 * speed 0, so they stand still until the switch starts them.  `state` is never
 * touched there (toggling it would hide the platform: state 2 is the free-slot
 * marker both draw loops skip).  Kind 0x28 is the other switch type: it flips
 * state 0 <-> 3 and queues two cell redraws, which the port's full redraw
 * makes unnecessary. */
void helper_toggle_kind(uint16_t kind) {
    int i, end = g_state.obj_cursor;
    if (end >= OBJ_MAX) end = OBJ_MAX - 1;
    if (kind == 0x27 || kind == 0x3B || kind == 0x26) {
        for (i = 4; i <= end; i++) {
            ObjectEntry *o = &g_state.obj[i];
            if (o->kind != kind) continue;
            o->speed = (o->speed == 0) ? 2 : 0;
        }
    } else if (kind == 0x28) {
        for (i = 4; i <= end; i++) {
            ObjectEntry *o = &g_state.obj[i];
            if (o->kind != kind) continue;
            o->state = (o->state == 3) ? 0 : 3;
        }
    }
}
void switch_pair(uint16_t col1, uint16_t row1, int shape1, int shape2) {
    cell_word_raw_put(col1, row1, CELL_W7, shape1);
    cell_word_raw_put(col1, row1 + 1, CELL_W7, shape2);
    cell_word_raw_put(col1, row1, CELL_W3, 0);
    cell_word_raw_put(col1, row1 + 1, CELL_W3, 0);
    cell_word_raw_put(col1, row1, CELL_W6, 0x16);
    cell_word_raw_put(col1, row1 + 1, CELL_W6, 0x16);
    cell_put((int)col1 - 1, (int)row1 - 1, CELL_FLAG_SOLID, 0);
    cell_put((int)col1 - 1, (int)row1, CELL_FLAG_SOLID, 0);
    mark_redraw((int16_t)(((int)col1 - 1) * 16), (int16_t)(((int)row1 - 1) * 16));
    mark_redraw((int16_t)(((int)col1 - 1) * 16), (int16_t)((int)row1 * 16));
}
/*
 * Per-contact pipeline.  The original runs three phases over the cell:
 *   1. collectible cells keyed by (w6, w7)  - first match consumes the cell,
 *   2. switch/exit cells keyed by the snapshot (w3, w5),
 *   3. a gas/fall hazard tail.
 * Phases 2 and 3 read the *snapshot* words the original captured up front,
 * so the tables can stay data-driven without re-reading mutated cells.
 */
typedef struct {
    int16_t x, y;            /* contact point (mark_redraw keeps the exact px) */
    int col, row;            /* cell containing (x, y) */
    int w0, w3, w5, w6, w7;  /* snapshot of the cell words before any mutation */
} CellEventCtx;

/*
 * Phase 1: collectible cells keyed by the *layer pair* (w6, w7).  The
 * original is an ordered if-chain; the first match wins and consumes the
 * cell (w7 -> 0).  g_cell_pickup_rules below preserves that order exactly
 * (the original 9..0x0D/w6=9 branch is two rows because it pays two
 * different awards).  Every rule is byte-for-byte the original branch.
 */
static void cell_pick_ammo(CellEventCtx *c) {       /* w6=6,   w7=0x31 */
    play_sound(SND_PICKUP);
    g_state.ammo = (uint16_t)(g_state.ammo + 5);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
}
static void cell_pick_power_shot(CellEventCtx *c) { /* w6=6,   w7=0x32 */
    play_sound(SND_PICKUP);
    if (g_state.g_a104) {
        g_state.g_a104 = 0;
        helper_a110_clear();
        cc_shell_notify_power_shot();
    }
    g_state.power_shot_active = 1;
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
    g_state.power_timer = g_state.power_shot_timer_preset;
    g_state.power_subtimer = 0;
}
static void cell_pick_gravity(CellEventCtx *c) {    /* w6=0x0C, w7=0x26 */
    score_add(SCORE_CRYSTAL, 0);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
    helper_toggle_gravity();
    play_sound(SND_GRAVITY);
    if (g_state.g_a102) {
        helper_a110_clear();
        cc_shell_notify_gravity();
        g_state.g_a102 = 0;
    }
    g_state.g_a100 = 1;
    if (!g_state.interaction_enabled && !g_state.interaction_suppressed) g_state.interaction_enabled = 1;
    g_state.power_timer = g_state.gravity_timer_preset;
    g_state.power_subtimer = 0;
}
static void cell_pick_crystal(CellEventCtx *c) {    /* w6=0x0D, w7=1..4 */
    g_state.crystals_remaining = (uint16_t)(g_state.crystals_remaining - 1);
    play_sound(g_state.crystals_remaining == 0 ? SND_FANFARE : SND_CRYSTAL);
    score_add(SCORE_CRYSTAL, 0);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
    g_state.crystals_collected = (uint16_t)(g_state.crystals_collected + 1);
}
static void cell_pick_item_800(CellEventCtx *c) {   /* w6=1,   w7=0x0B */
    spawn_effect(0x17, (int16_t)c->col, (int16_t)c->row);
    play_sound(SND_PICKUP);
    score_add(SCORE_ITEM_800, 0);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
}
static void cell_pick_item_1000(CellEventCtx *c) {  /* w6=1,   w7=4 */
    spawn_effect(0x18, (int16_t)c->col, (int16_t)c->row);
    score_add(SCORE_MUSHROOM, 0);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
}
static void cell_pick_item_5000(CellEventCtx *c) {  /* w6=1,   w7=9 */
    spawn_effect(0x1C, (int16_t)c->col, (int16_t)c->row);
    play_sound(SND_PICKUP);
    score_add(SCORE_GEM_5000, 0);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
}
static void cell_pick_prize_key(CellEventCtx *c) {  /* w6=0x0D, w7=0x2C: arms the prize cell */
    play_sound(SND_PICKUP);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
    g_state.switch_latch = 1;
}
static void cell_pick_gem_100(CellEventCtx *c) {    /* w6=9,   w7=9..0x0C */
    spawn_effect(0x10, (int16_t)c->col, (int16_t)c->row);
    play_sound(SND_PICKUP);
    score_add(SCORE_GEM_SMALL, 0);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
}
static void cell_pick_gem_10000(CellEventCtx *c) {  /* w6=9,   w7=0x0D */
    spawn_effect(0x1F, (int16_t)c->col, (int16_t)c->row);
    score_add(SCORE_GEM_10000, 0);
    play_sound(SND_BIG_GEM);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
}
static void cell_pick_mushroom(CellEventCtx *c) {   /* w6=0x0D, w7=0x31 */
    spawn_effect(0x18, (int16_t)c->col, (int16_t)c->row);
    play_sound(SND_MUSHROOM);
    score_add(SCORE_MUSHROOM, 0);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
}
static void cell_pick_red_mushroom(CellEventCtx *c) {/* w6=0x0D, w7=0x2D: ends the power-up */
    if (g_state.interaction_enabled) {
        g_state.g_a10d = 1;
        g_state.interaction_enabled = 0;
        g_state.power_timer = g_state.red_mushroom_timer_preset;
        g_state.power_subtimer = 0;
    }
    play_sound(SND_MUSHROOM);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
    helper_a110_clear();
    cc_shell_notify_red_mushroom();
}
static void cell_pick_poison_mushroom(CellEventCtx *c) { /* w6=0x0D, w7=0x2E */
    play_sound(SND_POISON);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    mark_redraw(c->x, c->y);
    if (g_state.g_a105) g_state.g_a105 = 0;
    if (g_state.interaction_enabled && !g_state.interaction_suppressed) {
        g_state.death_state = 1;
        cc_shell_notify_poison();
    }
}
static void cell_pick_gem_5000(CellEventCtx *c) {   /* w6=1, w7=0x25..0x27; stops the respawn timer */
    g_state.suppress_base_stamp = 1;
    spawn_effect(0x1C, (int16_t)c->col, (int16_t)c->row);
    g_state.suppress_base_stamp = 0;
    play_sound(SND_PICKUP);
    score_add(SCORE_GEM_5000, 0);
    cell_word_put(c->col, c->row, CELL_W7, 0);
    g_state.spawn_cell_x = 0xFFFF;
    g_state.spawn_cell_y = 0xFFFF;
    g_state.collectible_spawn_active = 0;
    g_state.collectible_spawn_timer = 0;
    mark_redraw(c->x, c->y);
}
static void cell_pick_stun_web(CellEventCtx *c) {   /* w6=0x0E, w7=0x19: arms the slash, stuns */
    play_sound(SND_STUN);
    g_state.a0f8 = 1;
    cell_word_put(c->col, c->row, CELL_W7, 0);
    g_state.power_timer = g_state.stun_timer_preset;
    g_state.power_subtimer = 0;
    if (g_state.g_a101) {
        helper_a110_clear();
        cc_shell_notify_stunned();
    }
}

typedef struct {
    uint16_t w6;              /* layer-2 shape selector */
    uint16_t w7_lo, w7_hi;    /* inclusive layer-1 shape range */
    void (*apply)(CellEventCtx *);
} CellPickupRule;

/* Original if-chain order; first match wins. */
static const CellPickupRule g_cell_pickup_rules[] = {
    { 0x06, 0x31, 0x31, cell_pick_ammo },
    { 0x06, 0x32, 0x32, cell_pick_power_shot },
    { 0x0C, 0x26, 0x26, cell_pick_gravity },
    { 0x0D, 0x01, 0x04, cell_pick_crystal },
    { 0x01, 0x0B, 0x0B, cell_pick_item_800 },
    { 0x01, 0x04, 0x04, cell_pick_item_1000 },
    { 0x01, 0x09, 0x09, cell_pick_item_5000 },
    { 0x0D, 0x2C, 0x2C, cell_pick_prize_key },
    { 0x09, 0x09, 0x0C, cell_pick_gem_100 },
    { 0x09, 0x0D, 0x0D, cell_pick_gem_10000 },
    { 0x0D, 0x31, 0x31, cell_pick_mushroom },
    { 0x0D, 0x2D, 0x2D, cell_pick_red_mushroom },
    { 0x0D, 0x2E, 0x2E, cell_pick_poison_mushroom },
    { 0x01, 0x25, 0x27, cell_pick_gem_5000 },
    { 0x0E, 0x19, 0x19, cell_pick_stun_web },
};

/*
 * Phase 2: switch/exit cells keyed by the snapshot (w3, w5).  The original
 * evaluates these against the values captured before phase 1, and every
 * branch falls through to the hazard block.
 */
static void cell_sw_locked_exit(CellEventCtx *c) {  /* w3=0x11/0x0D, w5=0x0C */
    if (g_state.exit_unlocked) {
        if (!g_state.interaction_lock) {
            play_sound(SND_EXIT_LOCKED);
            if (g_state.health == 3) {
                helper_a59d();
                cc_shell_notify_perfect_health();
                score_add(SCORE_FULL_HEALTH, 0);
            }
            g_state.interaction_lock = 1;
            g_state.exit_step = 0;
        }
    } else if (g_state.g_a106) {
        helper_a110_clear();
        cc_shell_notify_more_crystals();
        g_state.g_a106 = 0;
    }
}
static void cell_sw_prize(CellEventCtx *c) {        /* w3=0x32, w5=0x0D; needs the prize key */
    if (g_state.switch_latch) {
        int shape;
        cell_word_put(c->col, c->row, CELL_W3, (uint16_t)(c->w3 - 2));
        play_sound(SND_PRIZE_CELL);
        shape = (int)cc_rng_next(3);
        if (shape == 0) {
            spawn_effect(0x1B, (int16_t)c->col, (int16_t)(c->row - 1));
            score_add(SCORE_PRIZE, 0);
        } else if (shape == 1) {
            spawn_effect(0x18, (int16_t)c->col, (int16_t)(c->row - 1));
            score_add(SCORE_MUSHROOM, 0);
        } else if (shape == 2) {
            spawn_effect(0x1C, (int16_t)c->col, (int16_t)(c->row - 1));
            score_add(SCORE_GEM_5000, 0);
        }
    }
}
/* The three lever cells (w5=9) share the original body verbatim; they
 * differ only in w3 and in which switch_pair target they flip. */
static void cell_lever_body(CellEventCtx *c, uint16_t pair_col, uint16_t pair_row,
                            uint16_t shape1, uint16_t shape2) {
    if (g_state.g_a109) {
        helper_a110_clear();
        cc_shell_notify_lever();
        g_state.g_a109 = 0;
    }
    if (g_state.input_joystick_fire && g_state.g_a0f4) {
        g_state.g_a0f4 = 0;
        g_state.switch_repeat = (uint16_t)(g_state.switch_repeat + 1);
        if (g_state.switch_repeat % 3 == 0) {
            cell_word_put(c->col, c->row, CELL_W3, (uint16_t)(c->w3 + 1));
            g_state.switch_repeat = 0;
            g_state.input_fire = 1;
        }
    }
    if (g_state.input_fire) {
        play_sound(SND_SWITCH_TOGGLE);
        g_state.input_fire = 0;
        cell_word_put(c->col, c->row, CELL_W3, (uint16_t)(c->w3 + 1));
        switch_pair(pair_col, pair_row, shape1, shape2);
    }
}
static void cell_sw_lever_a(CellEventCtx *c) {      /* w3=0x11, w5=9 */
    cell_lever_body(c, g_state.switch_a_col, g_state.switch_a_row, 0x2D, 0x2E);
}
static void cell_sw_lever_b(CellEventCtx *c) {      /* w3=0x15, w5=9 */
    cell_lever_body(c, g_state.switch_b_col, g_state.switch_b_row, 0x31, 0x32);
}
static void cell_sw_lever_c(CellEventCtx *c) {      /* w3=0x13, w5=9 */
    cell_lever_body(c, g_state.switch_c_col, g_state.switch_c_row, 0x2F, 0x30);
}
/* The passage switches (w5=9) walk their w3 counter and set [0xA0F3];
 * unlike the levers, the joystick warm-up does not touch w3. */
static void cell_sw_passage_close(CellEventCtx *c) { /* w3=0x0F, w5=9 */
    if (g_state.g_a108) { helper_a110_clear(); g_state.g_a108 = 0; }
    if (g_state.input_joystick_fire && g_state.g_a0f4) {
        g_state.g_a0f4 = 0;
        g_state.switch_repeat = (uint16_t)(g_state.switch_repeat + 1);
        if (g_state.switch_repeat % 3 == 0) { g_state.switch_repeat = 0; g_state.input_fire = 1; }
    }
    if (g_state.input_fire) {
        play_sound(SND_DOOR);
        g_state.input_fire = 0;
        cell_word_put(c->col, c->row, CELL_W3, (uint16_t)(c->w3 - 1));
        g_state.lever_state = 0;
    }
}
static void cell_sw_passage_open(CellEventCtx *c) { /* w3=0x0E, w5=9 */
    if (g_state.g_a108) { helper_a110_clear(); g_state.g_a108 = 0; }
    if (g_state.input_joystick_fire && g_state.g_a0f4) {
        g_state.g_a0f4 = 0;
        g_state.switch_repeat = (uint16_t)(g_state.switch_repeat + 1);
        if (g_state.switch_repeat % 3 == 0) { g_state.switch_repeat = 0; g_state.input_fire = 1; }
    }
    if (g_state.input_fire) {
        play_sound(SND_DOOR);
        g_state.input_fire = 0;
        cell_word_put(c->col, c->row, CELL_W3, (uint16_t)(c->w3 + 1));
        g_state.lever_state = 1;
    }
}
/* w3=0x1F/0x20, w5=0x0C: start/stop the objects whose kind the cell's w0
 * records (helper_toggle_kind reads the snapshot w0). */
static void cell_sw_elevator_start(CellEventCtx *c) {
    if (g_state.g_a108) { helper_a110_clear(); g_state.g_a108 = 0; }
    if (g_state.input_joystick_fire && g_state.g_a0f4) {
        g_state.g_a0f4 = 0;
        g_state.switch_repeat = (uint16_t)(g_state.switch_repeat + 1);
        if (g_state.switch_repeat % 3 == 0) {
            cell_word_put(c->col, c->row, CELL_W3, (uint16_t)(c->w3 + 1));
            g_state.switch_repeat = 0;
            g_state.input_fire = 1;
        }
    }
    if (g_state.input_fire) {
        play_sound(SND_DOOR);
        g_state.input_fire = 0;
        cell_word_put(c->col, c->row, CELL_W3, (uint16_t)(c->w3 + 1));
        helper_toggle_kind((uint16_t)c->w0);
    }
}
static void cell_sw_elevator_stop(CellEventCtx *c) {
    if (g_state.g_a108) { helper_a110_clear(); g_state.g_a108 = 0; }
    if (g_state.input_joystick_fire && g_state.g_a0f4) {
        g_state.g_a0f4 = 0;
        g_state.switch_repeat = (uint16_t)(g_state.switch_repeat + 1);
        if (g_state.switch_repeat % 3 == 0) {
            cell_word_put(c->col, c->row, CELL_W3, (uint16_t)(c->w3 - 1));
            g_state.switch_repeat = 0;
            g_state.input_fire = 1;
        }
    }
    if (g_state.input_fire) {
        play_sound(SND_DOOR);
        g_state.input_fire = 0;
        cell_word_put(c->col, c->row, CELL_W3, (uint16_t)(c->w3 - 1));
        helper_toggle_kind((uint16_t)c->w0);
    }
}
static void cell_sw_checkpoint(CellEventCtx *c) {   /* w3=0x0D, w5=1: exit found */
    if (cave_flag_get(c->w0) != 0) {
        g_state.exit_col = (int16_t)c->col;
        g_state.exit_row = (int16_t)c->row;
        g_state.exit_step = 0;
        g_state.hub_door_active = 1;
        g_state.g_2f6 = 0xBEEF; /* DEBUG */
        play_sound(SND_EXIT_FOUND);
        memcpy_(g_state.checkpoint_block, g_state.level_block, 0x21);
    }
}

typedef struct {
    uint16_t w3;              /* layer-1 shape of the snapshot */
    uint16_t w5;              /* layer-2 shape selector */
    void (*apply)(CellEventCtx *);
} CellSwitchRule;

/* Original if-chain order; first match wins. */
static const CellSwitchRule g_cell_switch_rules[] = {
    { 0x11, 0x0C, cell_sw_locked_exit },
    { 0x0D, 0x0C, cell_sw_locked_exit },
    { 0x32, 0x0D, cell_sw_prize },
    { 0x11, 0x09, cell_sw_lever_a },
    { 0x15, 0x09, cell_sw_lever_b },
    { 0x13, 0x09, cell_sw_lever_c },
    { 0x0F, 0x09, cell_sw_passage_close },
    { 0x0E, 0x09, cell_sw_passage_open },
    { 0x1F, 0x0C, cell_sw_elevator_start },
    { 0x20, 0x0C, cell_sw_elevator_stop },
    { 0x0D, 0x01, cell_sw_checkpoint },
};

static void cell_event_switches(CellEventCtx *c) {
    if (c->w3 == 0)
        return;
    for (const CellSwitchRule *r = g_cell_switch_rules;
         r != g_cell_switch_rules + sizeof(g_cell_switch_rules) / sizeof(g_cell_switch_rules[0]); ++r) {
        if (c->w3 == (int)r->w3 && c->w5 == (int)r->w5) {
            r->apply(c);
            return;
        }
    }
}

/* Hazard tail: gas cells ([0x13] flag set, layer pair (0x0A, 0x31/0x32/
 * 0x1B/0x1C)) poison the player; otherwise an uncovered walk may cost a
 * life.  Reads the *current* exit flag but the snapshot w6/w7. */
static void cell_event_hazard(CellEventCtx *c) {
    if (cell_byte_cur(c->col, c->row, CELL_FLAG_EXIT) == 0) return;
    if (!g_state.interaction_enabled) return;
    if (g_state.interaction_suppressed) return;
    if ((c->w7 == 0x31 || c->w7 == 0x32 || c->w7 == 0x1B || c->w7 == 0x1C) &&
        c->w6 == 0x0A) {
        g_state.death_state = 1;
        play_sound(SND_HURT);
        return;
    }
    if (g_state.death_state == 0) {
        if ((int16_t)g_state.invuln_timer == -1) damage_life();
        if ((int16_t)g_state.death_state > 0) play_sound(SND_HURT);
    }
}

void cell_event(int16_t x, int16_t y) {
    CellEventCtx c;
    c.x = x;
    c.y = y;
    c.col = (int)x / 16;
    c.row = (int)y / 16;
    c.w0 = cell_word_cur(c.col, c.row, CELL_W0);
    c.w3 = cell_word_cur(c.col, c.row, CELL_W3);
    c.w5 = cell_word_cur(c.col, c.row, CELL_W5);
    c.w6 = cell_word_cur(c.col, c.row, CELL_W6);
    c.w7 = cell_word_cur(c.col, c.row, CELL_W7);

    for (const CellPickupRule *r = g_cell_pickup_rules;
         r != g_cell_pickup_rules + sizeof(g_cell_pickup_rules) / sizeof(g_cell_pickup_rules[0]); ++r) {
        if (c.w6 == (int)r->w6 && c.w7 >= (int)r->w7_lo && c.w7 <= (int)r->w7_hi) {
            r->apply(&c);
            break;
        }
    }
    cell_event_switches(&c);
    cell_event_hazard(&c);
}

/* 0xE51D: apply hazard-cell damage for the two lower player corners. */
void hazard_cell(int16_t x, int16_t y) {
    int col = (int)x / 16;
    int row = (int)y / 16;
    if (cell_word_cur(col, row, CELL_W0) != (int)0xFF9F) return;
    if (!g_state.interaction_enabled) return;
    if (g_state.interaction_suppressed) return;
    if ((int16_t)g_state.invuln_timer != -1) return;
    damage_life();
    if ((int16_t)g_state.death_state > 0) play_sound(SND_HURT);
}

/* 0xE5D3: player map-cell contact, hazard cells and collectible pickup. */
void contact_frame(void) {
    int16_t x = g_state.player_x;
    int16_t y = g_state.player_y;
    int col, row, w0, w3, w5;

    cell_event((int16_t)(x + 3), y);
    cell_event((int16_t)(x + 0x0C), y);
    cell_event((int16_t)(x + 3), (int16_t)(y + 0x0F));
    cell_event((int16_t)(x + 0x0C), (int16_t)(y + 0x0F));
    if (g_state.g_a106) cell_event((int16_t)(x + 0x0E), (int16_t)(y + 8));

    col = (x + 4) / 16;
    row = (y + 4) / 16;
    if (cell_word_cur(col, row, CELL_W0) == (int)0xFF9F) {
        hazard_cell((int16_t)(x + 3), (int16_t)(y + 2));
        hazard_cell((int16_t)(x + 0x0C), (int16_t)(y + 2));
    }

    col = (x + 5) / 16;
    row = (y - 5) / 16;
    if (cell_word_cur(col, row, CELL_W0) == (int)0xFF9D && g_state.jump_flag) {
        cell_word_put(col, row, CELL_W3, 3);
        cell_word_put(col, row, CELL_W5, 1);
        cell_put(col, row, CELL_FLAG_SOLID, 1);
        mark_redraw((int16_t)(x + 5), (int16_t)(y - 5));
        cell_word_put(col, row, CELL_W0, 0);
        goto after_hazards;
    }

    col = (x + 0x0A) / 16;
    row = (y - 5) / 16;
    if (cell_word_cur(col, row, CELL_W0) == (int)0xFF9D && g_state.jump_flag) {
        cell_word_put(col, row, CELL_W3, 3);
        cell_word_put(col, row, CELL_W5, 1);
        cell_put(col, row, CELL_FLAG_SOLID, 1);
        mark_redraw((int16_t)(x + 0x0A), (int16_t)(y - 5));
        cell_word_put(col, row, CELL_W0, 0);
    }

after_hazards:
    if (!((g_state.airborne && g_state.g_a0b8 == 0) || g_state.g_a0fc)) return;

    col = (x + 8) / 16;
    row = (y - 1) / 16;
    w0 = cell_word_cur(col, row, CELL_W0);
    w3 = cell_word_cur(col, row, CELL_W3);
    w5 = cell_word_cur(col, row, CELL_W5);
    if (w3 < 4 || w3 > 0x0C) return;
    if (w5 != 0x14) return;

    if (g_state.obj_cursor < OBJ_MAX && g_state.obj[g_state.obj_cursor].kind == 0x2D)
        g_state.obj[g_state.obj_cursor].state = 2;

    g_state.g_a0fc = 0;
    g_state.g_a0b8 = 1;
    g_state.suppress_base_stamp = 1;

    if (w0 == (int)0xFF9E) {
        int shape = w3;
        play_sound(SND_BLOCK_POP);
        cell_word_put(col, row, CELL_W0, 0);
        g_state.obj_cursor++;
        if (g_state.obj_cursor < OBJ_MAX) g_state.obj[g_state.obj_cursor].state = 0x63;
        {
            SpawnArgs a;
            a.handler = HDL_CRYSTAL_BOB; a.handler_seg = 0x1F9A;
            a.phase = 0; a.speed = 2; a.anim_last = 1;
            a.anim_shape_b = 0x25; a.anim_shape_a = 0x25;
            a.vy = 0xFFFF; a.vx = 0;
            a.ycell = (uint16_t)(row - 1);
            a.xcell = (uint16_t)col;
            a.block = 0x0D; a.kind = 0x2E;
            a.slot = (int)g_state.obj_cursor;
            spawn_object_47d(&a);
        }
        (void)shape;
    }
    g_state.obj_cursor++;
    {
        SpawnArgs a;
        int shape = w3;
        a.handler = HDL_PICKUP_RISE; a.handler_seg = 0x1F9A;
        a.phase = 0; a.speed = 1; a.anim_last = 1;
        a.anim_shape_b = (uint16_t)shape; a.anim_shape_a = (uint16_t)shape;
        a.vy = 0xFFFF; a.vx = 0;
        a.ycell = (uint16_t)row;
        a.xcell = (uint16_t)col;
        a.block = 0x14; a.kind = 0x2D;
        a.slot = (int)g_state.obj_cursor;
        spawn_object_47d(&a);
    }
    g_state.suppress_base_stamp = 0;
    cell_word_put(col, row, CELL_W3, 0x32);
    g_state.anim_timer = 1;
    g_state.shape_player = (g_state.move_dir == 1) ? 0x19 : 0x1A;
}

