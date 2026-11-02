/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"


/* ---------------------------------------------------------------- update loop
 * Function 0xD72A.  One call advances slots 2..59 then 0 (never slot 1,
 * which is the player).  Handlers are dispatched exactly as in the binary:
 * some kinds run their handler directly, the rest run it only when visible
 * (0xF38 with margin 0x20) and not yet marked ready.
 */
void step_objects(void) {
    int i;

    for (i = 0; i < OBJ_MAX; i++) {
        int s = (2 + i) % OBJ_MAX;          /* 2..59, 0 */
        ObjectEntry *o = &g_state.obj[s];
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
        if (!g_state.a0f8 && visible_obj(0x20, o->x, o->y)) {
            if (!o->ready) h(o, s, (int16_t)x_new, (int16_t)y_new);
            continue;
        }

        if (o->kind == 2 || o->kind == 0x2B) {
            if (g_state.a0f8 && !o->ready) h_251d(o, s, (int16_t)x_new, (int16_t)y_new);
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
                g_state.obj[(s + OBJ_MAX - 1) % OBJ_MAX].phase = 0;
            }
            continue;
        }

        if (o->kind == 4) {
            if (!o->ready) {
                o->ready = 1;
                o->phase = 0;
            }
            continue;
        }

        if (o->kind == 7) {
            if (o->state == 0) {
                int32_t ax;
                o->phase = 0;
                o->state = 3;
                o->anim_shape_a = (uint16_t)(o->anim_shape_a - 4);
                o->anim_shape_b = (uint16_t)(o->anim_shape_b - 4);
                o->anim_last = (uint16_t)(o->anim_last + 4);
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
            g_state.g_5126 = 0;
        }
        if (o->kind == 0x16) {
            int k;
            for (k = 1; k <= 3; k++) {
                int t = (s + k) % OBJ_MAX;
                g_state.obj[t].state = 0;
            }
        }
    }
}

/* Level argument mapping: 0=hub, 1..16=caves, 0xffff=intro, 0xfffe=finale. */
int count_crystal_cells(void) {
    int n = 0;
    for (int r = 0; r < CELL_ROWS; r++) {
        for (int c = 0; c < CELL_COLS; c++) {
            int w6 = cell_word_cur(c, r, CELL_W6);
            int w7 = cell_word_cur(c, r, CELL_W7);
            if (w6 == 0x0D && w7 >= 1 && w7 <= 4) n++;
        }
    }
    return n;
}

void load_arg(int arg) {
    int level;
    uint16_t u = (uint16_t)arg;
    if (u == 0xFFFF) level = 0;
    else if (u == 0xFFFE) level = 1;
    else if (u == 0) level = 2;
    else if (u <= 16) level = (int)u + 2;
    else level = 2;
    g_state.next_level = (int16_t)arg;
    load_level(level, 0, 0, 1);
}

/* 0xD4E5: hub door opening/entry; caller has set a0f7. */
void door_entry_frame(void) {
    int c1, r1, w3;
    g_state.exit_step = (uint16_t)(g_state.exit_step + 1u);
    if ((int16_t)g_state.exit_step < 3) {
        w3 = (int)g_state.exit_step + 0x0D;
        cell_word_put(g_state.exit_col, g_state.exit_row, CELL_W3, w3);
        mark_redraw((int16_t)(g_state.exit_col * 16), (int16_t)(g_state.exit_row * 16));
        return;
    }
    if ((int16_t)g_state.exit_step != 3) return;

    g_state.g_a0f7 = 0;
    mark_redraw((int16_t)(g_state.exit_col * 16), (int16_t)(g_state.exit_row * 16));
    g_state.spawn_col_override = (uint16_t)(g_state.exit_col - (int16_t)g_state.move_dir);
    g_state.spawn_row_override = (uint16_t)g_state.exit_row;
    play_sound(0x14);
    g_state.jump_flag = 0;
    g_state.g_a0f6 = 0;
    c1 = g_state.exit_col + 1;
    r1 = g_state.exit_row + 1;
    {
        int arg = cell_word_cur(g_state.exit_col, g_state.exit_row, CELL_W0);
        g_state.next_level = (int16_t)arg;
        load_arg(arg);
    }
    (void)c1; (void)r1;
}

/* 0xD360 + 0xD412: cave exit animation, completion flag and return to hub. */
void exit_to_hub_frame(void) {
    int all;
    int i;
    g_state.exit_step = (uint16_t)(g_state.exit_step + 1u);
    if ((int16_t)g_state.exit_step < 4) {
        cell_word_put(g_state.exit_col, g_state.exit_row, CELL_W3,
                      (uint16_t)(g_state.exit_step + 0x0D));
        mark_redraw((int16_t)(g_state.exit_col * 16), (int16_t)(g_state.exit_row * 16));
        cell_word_put(g_state.exit_col, (int16_t)(g_state.exit_row + 1), CELL_W3,
                      (uint16_t)(g_state.exit_step + 0x11));
        mark_redraw((int16_t)(g_state.exit_col * 16),
                    (int16_t)((g_state.exit_row + 1) * 16));
        return;
    }
    if ((int16_t)g_state.exit_step == 4) {
        mark_redraw((int16_t)(g_state.exit_col * 16), (int16_t)(g_state.exit_row * 16));
        mark_redraw((int16_t)(g_state.exit_col * 16), (int16_t)((g_state.exit_row + 1) * 16));
        return;
    }

    g_state.player_block = 6;
    g_state.player_y = (int16_t)((g_state.exit_row + 1) * 16);
    g_state.shape_player = 1;
    if ((g_state.exit_step & 1) == 0) player_anim_next();
    g_state.player_x = (int16_t)(g_state.player_x + 1);
    if (g_state.player_x != (int16_t)((g_state.exit_col + 1) * 16)) return;

    play_sound(0x14);
    g_state.jump_flag = 0;
    g_state.g_a0f6 = 0;
    cave_flag_set(g_state.next_level, 0);
    all = 1;
    for (i = 1; i <= 16; i++) if (g_state.cave_flags[i] != 0) { all = 0; break; }
    g_state.g_a0fb = (uint8_t)all;
    if (g_state.g_a0fb) {
        g_state.g_a110 = 1;
        g_state.g_3396 = 0;
        g_state.g_3394 = 1;
    }
    g_state.next_level = 0;
    /* 0xD4CC loads the hub without touching [0x2B72]/[0x2B74]: they still
     * hold the hub-door entry cell, so the player returns beside that door. */
    play_sound(0x14);
    load_arg(0);
}

/* 0xCF6C: player fire.  The attack is object slot 2 (DS:0x4638). */
void cc_fire(void) {
    ObjectEntry *a = &g_state.obj[2];

    if (g_state.g_509c <= 0 && g_state.power_shot == 0) {
        play_sound(0x1E);
        if (!g_state.input_left && !g_state.input_right) {
            g_state.shape_player = (g_state.move_dir == 1) ? 0x1B : 0x1C;
            g_state.anim_timer = 1;
        }
    }

    if (g_state.g_509c <= 0 && g_state.power_shot == 0 && g_state.g_5140 == 0) return;
    if (g_state.obj[2].ready == 0) return;
    if ((int16_t)g_state.cooldown <= 6) return;

    a->vx = (int16_t)g_state.move_dir;
    if (g_state.power_shot) {
        play_sound(0x0F);
        a->speed = 8;
        a->anim_last = 4;
        a->subtype = 2;
        if (a->vx == 1) {
            a->x = (int16_t)(g_state.player_x + 8);
            a->shape = 0x1D;
            g_state.shape_player = 0x1B;
        } else {
            a->x = g_state.player_x;
            a->shape = 0x21;
            g_state.shape_player = 0x1C;
        }
    } else {
        play_sound(0x0E);
        a->speed = 4;
        a->anim_last = 6;
        a->subtype = 1;
        if (a->vx == 1) {
            a->x = (int16_t)(g_state.player_x + 8);
            a->shape = 0x25;
            g_state.shape_player = 0x1B;
        } else {
            a->x = g_state.player_x;
            a->shape = 0x2B;
            g_state.shape_player = 0x1C;
        }
    }
    g_state.anim_timer = 1;
    if (g_state.power_shot == 0 && g_state.g_5140 == 0) g_state.g_509c--;

    a->y = g_state.player_y;
    a->x_prev = a->x;
    a->y_prev = a->y;
    a->kind = 2;
    g_state.cooldown = 0;
    g_state.anim_suppress = 1;
    g_state.obj[2].ready = 0;
    a->state = 0;
    a->phase = 0;
    a->handler = 0x2850;
    a->handler_seg = 0x1F9A;
    a->block = 6;
    g_state.impulse = g_state.fire_impulse;
}

/* 0xEBF2: cooldown, collectible spawn/expiry, exit unlock and fire. */
void cc_fire_timers(void) {
    g_state.cooldown = (uint16_t)(g_state.cooldown + 1u);
    if ((int16_t)g_state.cooldown > 0x7D00) g_state.cooldown = 0x19;

    g_state.g_a0ce = (uint16_t)(g_state.g_a0ce + 1u);
    if ((int16_t)g_state.g_a0ce > 0x10E && g_state.g_a10b == 0) {
        if (cc_rng_next(100) < 0x23) g_state.g_a10b = 1;
        else g_state.g_a0ce = 0;
    }
    if (g_state.g_a10b != 0 && g_state.g_a126 == 0) {
        if ((int16_t)g_state.g_a0d0 == -1) {
            int rx = (int)cc_rng_next(0x140);
            int ry = (int)cc_rng_next(0xC8);
            int c1 = (rx + g_state.cam_x) / 16 + 1;
            int r1 = (ry + g_state.cam_y) / 16 + 1;
            if (cell_word_raw(c1, r1, CELL_W7) == 0 &&
                cell_byte_cur(c1 - 1, r1 - 1, CELL_FLAG_SOLID) == 0) {
                int shape = (int)cc_rng_next(3) + 0x25;
                cell_word_raw_put(c1, r1, CELL_W7, shape);
                cell_word_raw_put(c1, r1, CELL_W6, 1);
                g_state.g_a0d0 = (uint16_t)(c1 * 16 - 1);
                g_state.g_a0d2 = (uint16_t)(r1 * 16 - 1);
                mark_redraw((int16_t)g_state.g_a0d0, (int16_t)g_state.g_a0d2);
                play_sound(0x0C);
            }
        } else if ((int16_t)g_state.g_a0ce > 0x19E) {
            int c1 = (int)g_state.g_a0d0 / 16 + 1;
            int r1 = (int)g_state.g_a0d2 / 16 + 1;
            g_state.g_a10b = 0;
            g_state.g_a0ce = 0;
            cell_word_raw_put(c1, r1, CELL_W7, 0);
            mark_redraw((int16_t)g_state.g_a0d0, (int16_t)g_state.g_a0d2);
            g_state.g_a0d0 = 0xFFFF;
            g_state.g_a0d2 = 0xFFFF;
        }
    }

    if ((int16_t)g_state.next_level > 0 && g_state.g_a0cc == 0 && g_state.g_a0f5 == 0) {
        play_sound(0x20);
        if (g_state.g_a107 && (int16_t)g_state.next_level > 0) {
            helper_a110_clear();
            g_state.g_a107 = 0;
        }
        g_state.g_a0f5 = 1;
        cc_shell_notify_last_crystal();
        cell_put(g_state.exit_col, g_state.exit_row, CELL_FLAG_SOLID, 0);
        cell_put(g_state.exit_col, (int16_t)(g_state.exit_row + 1), CELL_FLAG_SOLID, 0);
        cell_put(g_state.exit_col, g_state.exit_row, CELL_FLAG_SOLID, 0);
    }

    if (g_state.power_shot != 0 && (int16_t)g_state.power_timer <= -1 &&
        g_state.obj[2].ready != 0)
        g_state.power_shot = 0;

    if (g_state.input_fire) cc_fire();
}

/* 0xEAEA: status/invulnerability timers. */
void cc_status_timers(void) {
    if (g_state.a0f8 != 0 && (int16_t)g_state.power_timer <= -1) g_state.a0f8 = 0;
    if (g_state.g_a100 != 0 && (int16_t)g_state.power_timer <= -1) {
        /* 0xEB0E: lcall 0x0FC5:0xBAF4.  The original flips gravity back when
         * the shared power timer expires (0x1B744 is the same toggle the
         * gravity switch uses). */
        helper_toggle_gravity();
        g_state.g_a100 = 0;
    }

    if (g_state.status_30b != 0 && g_state.g_2f2 < OBJ_MAX) {
        ObjectEntry *o = &g_state.obj[g_state.g_2f2];
        if (o->state == 2) {
            o->ready = 0;
            o->state = 0;
            o->x = g_state.player_x;
            o->y = g_state.player_y;
        }
    }

    if ((int16_t)g_state.g_a0b8 > 0) {
        g_state.g_a0b8 = (uint16_t)(g_state.g_a0b8 + 1u);
        if ((int16_t)g_state.g_a0b8 > 9) g_state.g_a0b8 = 0;
    }

    if ((int16_t)g_state.g_a0ea > -1) {
        g_state.g_a0ea = (uint16_t)(g_state.g_a0ea + 1u);
        if ((int16_t)g_state.g_a0ea > 0x13) g_state.g_a0ea = 0xFFFF;
        if ((g_state.g_a0ea % 7) == 0) g_state.obj[1].hit_flag = 1;
    }
}

void cc_objects_step_objects(void) {
    input_commit();
    step_objects();
}

void cc_test_fill_source(int obj_byte, int cell_byte) {
    unsigned char *raw = (unsigned char *)&g_state.obj[0];
    unsigned i;
    for (i = 0; i < sizeof(g_state.obj); i++) raw[i] = (unsigned char)obj_byte;
    for (int r = 0; r < CELL_ROWS; r++)
        for (int c = 0; c < CELL_COLS; c++)
            for (i = 0; i < 20; i++)
                g_state.cells[r][c][i] = (unsigned char)cell_byte;
}
void cc_test_call_handler(int handler, int slot, int x, int y) {
    if (slot < 0 || slot >= OBJ_MAX) return;
    handler_for((uint16_t)handler)(&g_state.obj[slot], slot,
                                   (int16_t)x, (int16_t)y);
}
