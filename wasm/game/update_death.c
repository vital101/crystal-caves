/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"

uint32_t cc_unknown_handler_hits(void) { return g_unknown_handler_hits; }
void cc_test_spawn_2002d(int mode) { spawn_template_2002d(mode); }
void cc_test_cell_event(int x, int y) { cell_event((int16_t)x, (int16_t)y); }
void cc_test_contact(void) { contact_frame(); }
void cc_test_spawn_47d(int handler, int handler_seg, int phase, int speed,
                       int anim_last, int anim_shape_b, int anim_shape_a, int vy, int vx,
                       int ycell, int xcell, int block, int kind, int slot) {
    SpawnArgs a;
    a.handler = (uint16_t)handler; a.handler_seg = (uint16_t)handler_seg;
    a.phase = (uint16_t)phase; a.speed = (uint16_t)speed;
    a.anim_last = (uint16_t)anim_last; a.anim_shape_b = (uint16_t)anim_shape_b; a.anim_shape_a = (uint16_t)anim_shape_a;
    a.vy = (uint16_t)vy; a.vx = (uint16_t)vx;
    a.ycell = (uint16_t)ycell; a.xcell = (uint16_t)xcell;
    a.block = (uint16_t)block; a.kind = (uint16_t)kind; a.slot = slot;
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
void restore_checkpoint(void) {
    g_state.g_509c = g_state.checkpoint_50be;
    g_state.score_lo = g_state.checkpoint_50c7;
    g_state.score_hi = g_state.checkpoint_50c9;
    g_state.g_50a2 = g_state.checkpoint_50c4;
    g_state.g_509e = 3;
}

/* 0xD15C: reload the current level through the loader-equivalent path. */
void reload_current_level(void) {
    g_state.spawn_col_override = 0xFFFF;
    g_state.spawn_row_override = 0;
    load_level(g_state.cur_level, 0, 0, 1);
}

/* 0xD140: decrement the fade/lives latch. */
void fade_latch(void) {
    g_state.checkpoint_50a0 = (uint16_t)(g_state.checkpoint_50a0 - 1u);
    if (g_state.checkpoint_50a0 == 0) {
        g_state.g_3394 = 1;
        g_state.g_a110 = 1;
    }
}

void restart_sequence(void) {
    g_state.g_5124 = 0;
    reload_current_level();
    fade_latch();
    restore_checkpoint();
}

/* 0x0FC5:0xB7B7: player death animation.  Uses [0x4623]/[0x4625]
 * (slot-1 phase/subtype) as phase/frame and returns 1 when the final phase ends. */
int death_animation(void) {
    uint16_t *phase = &g_state.obj[1].phase;
    uint16_t *frame = &g_state.obj[1].subtype;
    int done = 0;

    switch (*phase) {
        case 0:
            if (*frame == 0) {
                g_state.player_block = 0x11;
                g_state.anim_timer = 1;
                g_state.shape_player = 1;
                g_state.obj[1].anim_last = 6;
            }
            if (*frame > 0xC && g_state.anim_timer <= g_state.obj[1].anim_last)
                g_state.anim_timer = (uint16_t)(g_state.anim_timer + 1u);
            (*frame)++;
            if (*frame > 0x18) { *frame = 0; (*phase)++; }
            break;
        case 1:
            if (*frame == 0) {
                g_state.anim_timer = 1;
                g_state.shape_player = 8;
                g_state.obj[1].anim_last = 2;
            }
            if (*frame < 9) player_anim_next();
            (*frame)++;
            if (*frame > 0xE) { *frame = 0; (*phase)++; }
            break;
        case 2:
            if (*frame == 0) {
                g_state.anim_timer = 1;
                g_state.shape_player = 0xA;
            }
            (*frame)++;
            if (*frame > 0xE) { *frame = 0; (*phase)++; }
            break;
        case 3:
            if (*frame == 0) {
                g_state.anim_timer = 1;
                g_state.shape_player = 0xB;
                g_state.obj[1].anim_last = 5;
            }
            if (*frame < 0xF) player_anim_next();
            (*frame)++;
            if (*frame == 0xF) { *frame = 0; (*phase)++; }
            break;
        case 4:
            if (*frame == 0) {
                g_state.anim_timer = 1;
                g_state.shape_player = 0x10;
                g_state.obj[1].anim_last = 2;
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
void death_state1(void) {
    if (death_animation()) {
        restart_sequence();
        return;
    }
    if ((g_state.g_2b5e > 0 && g_state.g_2b5e < 0x80 && g_state.obj[1].phase > 0) ||
        g_state.input_fire || g_state.input_joystick_fire)
        restart_sequence();
}

/* 0xD1C6: death state 2 - spawn 0x6681 and play the fall/restart sequence. */
void death_state2(void) {
    uint16_t *phase = &g_state.obj[1].phase;
    uint16_t *frame = &g_state.obj[1].subtype;

    if (*phase == 0) {
        if (*frame == 0) {
            g_state.player_block = 0x11;
            g_state.anim_timer = 1;
            g_state.shape_player = 0x12;
            g_state.obj[1].anim_last = 3;
        }
        if (*frame > 3 && (*frame % 3) == 0 &&
            g_state.anim_timer < g_state.obj[1].anim_last)
            g_state.anim_timer = (uint16_t)(g_state.anim_timer + 1u);
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
        g_state.obj_cursor++;
        slot = g_state.obj_cursor;
        if (slot < OBJ_MAX) {
            SpawnArgs a;
            g_state.obj[slot].state = 0x63;
            a.handler = HDL_CELL_ERASE; a.handler_seg = 0x1F9A;
            a.phase = 0; a.speed = 4; a.anim_last = 1;
            a.anim_shape_b = 0x15; a.anim_shape_a = 0x15;
            a.vy = 0xFFFF; a.vx = 0;
            a.ycell = (uint16_t)(g_state.player_y / 16);
            a.xcell = (uint16_t)(g_state.player_x / 16 + 1);
            a.block = 0x11; a.kind = 0x29; a.slot = slot;
            spawn_object_47d(&a);
            g_state.obj[slot].state = 0;
            g_state.obj[slot].x = g_state.player_x;
            g_state.obj[slot].y = (int16_t)(g_state.player_y - 0x10);
            g_state.obj[slot].x_prev = g_state.obj[slot].x;
            g_state.obj[slot].y_prev = g_state.obj[slot].y;
        }
        g_state.player_block = 7;
        g_state.anim_timer = 0;
        g_state.shape_player = 0x19;
        g_state.obj[1].anim_last = 9;
        *frame = (uint16_t)(*frame + 1u);
    }
    if (g_state.anim_timer < g_state.obj[1].anim_last)
        g_state.anim_timer = (uint16_t)(g_state.anim_timer + 1u);
    else
        *frame = (uint16_t)(*frame + 1u);
    if (*frame >= 4) restart_sequence();
}

/* 0xD342: death state 3 - wait for the restart input. */
void death_state3(void) {
    if (g_state.input_fire || g_state.input_jump) restart_sequence();
}

/* Exact 0xF4E6 frame dispatch. */
void cc_objects_step_full(void) {
    if (g_state.g_a0f6) {
        exit_to_hub_frame();
    } else if (g_state.g_a0f7) {
        door_entry_frame();
    } else if (g_state.g_a0fb) {
        /* 0xD710: story/finale transition. */
        cc_shell_finale_done();
        g_state.g_a0fb = 0;
        g_state.g_a110 = 1;
    } else if (g_state.g_a126) {
        /* 0xD6D4: story screen input; outside Phase 3. */
    } else if (g_state.g_5124) {
        if (g_state.g_5124 == 1) death_state1();
        else if (g_state.g_5124 == 2) death_state2();
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
    ObjectEntry *p = &g_state.obj[1];
    if (p->state == 0 || p->state == 2) return;
    if (g_state.g_a0ea >= 4) return;
    play_sound(SND_PLAYER_DRAW);
}

/* ---------------------------------------------------------------- collision
 * Check if any object is in proximity to the player (using helper 0x1F9A:0x177B).
 * Returns the object index, or -1 if none.  `radius` is the proximity
 * threshold in pixels; `mode` selects the test (1 = same-x, 2 = box). */
int cc_collide_player(int mode, int16_t radius) {
    int i;
    for (i = 0; i < OBJ_MAX; i++) {
        ObjectEntry *o = &g_state.obj[i];
        if (i == 1) continue;          /* slot 1 is the player itself */
        if (o->kind == 0) continue;    /* empty slot */
        if (o->state == 2) continue;   /* inactive/free slot */
        int16_t dx = (int16_t)o->x - g_state.player_x;
        int16_t dy = (int16_t)o->y - g_state.player_y;
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
