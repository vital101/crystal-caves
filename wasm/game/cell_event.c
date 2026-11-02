/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"


/* ---------------------------------------------------------------- cells
 * 0x0FC5:0xBB26 (linear 0x1B776): per-cell event handler.  The original is
 * called by 0xE5D3 for several player-relative points each frame.  All cell
 * fields are words except the solid/exit flags at +0x11/+0x13. */
void helper_a110_clear(void) { g_state.g_a110 = 0; }
void helper_a59d(void) { g_state.g_a110 = 0; g_state.g_3395 = 0; }
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
void cell_event(int16_t x, int16_t y) {
    int col = (int)x / 16;
    int row = (int)y / 16;
    int w0 = cell_word_cur(col, row, CELL_W0);
    int w3 = cell_word_cur(col, row, CELL_W3);
    int w5 = cell_word_cur(col, row, CELL_W5);
    int w6 = cell_word_cur(col, row, CELL_W6);
    int w7 = cell_word_cur(col, row, CELL_W7);

    if (w7 != 0) {
        if (w7 == 0x31 && w6 == 6) {
            play_sound(SND_PICKUP);
            g_state.g_509c = (uint16_t)(g_state.g_509c + 5);
            cell_word_put(col, row, CELL_W7, 0);
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 0x32 && w6 == 6) {
            play_sound(SND_PICKUP);
            if (g_state.g_a104) {
                g_state.g_a104 = 0;
                helper_a110_clear();
                cc_shell_notify_power_shot();
            }
            g_state.power_shot = 1;
            cell_word_put(col, row, CELL_W7, 0);
            mark_redraw(x, y);
            g_state.power_timer = g_state.g_a0c2;
            g_state.g_a0c6 = 0;
            goto phase2;
        }
        if (w7 == 0x26 && w6 == 0x0C) {
            score_add(0x32, 0);
            cell_word_put(col, row, CELL_W7, 0);
            mark_redraw(x, y);
            helper_toggle_gravity();
            play_sound(SND_GRAVITY);
            if (g_state.g_a102) {
                helper_a110_clear();
                cc_shell_notify_gravity();
                g_state.g_a102 = 0;
            }
            g_state.g_a100 = 1;
            if (!g_state.g_513c && !g_state.g_5140) g_state.g_513c = 1;
            g_state.power_timer = g_state.g_a0c8;
            g_state.g_a0c6 = 0;
            goto phase2;
        }
        if (w6 == 0x0D && (w7 == 1 || w7 == 2 || w7 == 3 || w7 == 4)) {
            g_state.g_a0cc = (uint16_t)(g_state.g_a0cc - 1);
            play_sound(g_state.g_a0cc == 0 ? SND_FANFARE : SND_CRYSTAL);
            score_add(0x32, 0);
            cell_word_put(col, row, CELL_W7, 0);
            mark_redraw(x, y);
            g_state.g_50a2 = (uint16_t)(g_state.g_50a2 + 1);
            goto phase2;
        }
        if (w7 == 0x0B && w6 == 1) {
            spawn_effect(0x17, (int16_t)col, (int16_t)row);
            play_sound(SND_PICKUP);
            score_add(0x320, 0);
            cell_word_put(col, row, CELL_W7, 0);
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 4 && w6 == 1) {
            spawn_effect(0x18, (int16_t)col, (int16_t)row);
            score_add(0x3E8, 0);
            cell_word_put(col, row, CELL_W7, 0);
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 9 && w6 == 1) {
            spawn_effect(0x1C, (int16_t)col, (int16_t)row);
            play_sound(SND_PICKUP);
            score_add(0x1388, 0);
            cell_word_put(col, row, CELL_W7, 0);
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 0x2C && w6 == 0x0D) {
            play_sound(SND_PICKUP);
            cell_word_put(col, row, CELL_W7, 0);
            mark_redraw(x, y);
            g_state.g_a0f9 = 1;
            goto phase2;
        }
        if (w7 >= 9 && w7 <= 0x0D && w6 == 9) {
            if (w7 == 0x0D) {
                spawn_effect(0x1F, (int16_t)col, (int16_t)row);
                score_add(0x2710, 0);
                play_sound(SND_BIG_GEM);
            } else {
                spawn_effect(0x10, (int16_t)col, (int16_t)row);
                play_sound(SND_PICKUP);
                score_add(0x64, 0);
            }
            cell_word_put(col, row, CELL_W7, 0);
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 0x31 && w6 == 0x0D) {
            spawn_effect(0x18, (int16_t)col, (int16_t)row);
            play_sound(SND_MUSHROOM);
            score_add(0x3E8, 0);
            cell_word_put(col, row, CELL_W7, 0);
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 0x2D && w6 == 0x0D) {
            if (g_state.g_513c) {
                g_state.g_a10d = 1;
                g_state.g_513c = 0;
                g_state.power_timer = g_state.g_a0ca;
                g_state.g_a0c6 = 0;
            }
            play_sound(SND_MUSHROOM);
            cell_word_put(col, row, CELL_W7, 0);
            mark_redraw(x, y);
            helper_a110_clear();
            cc_shell_notify_red_mushroom();
            goto phase2;
        }
        if (w7 == 0x2E && w6 == 0x0D) {
            play_sound(SND_POISON);
            cell_word_put(col, row, CELL_W7, 0);
            mark_redraw(x, y);
            if (g_state.g_a105) g_state.g_a105 = 0;
            if (g_state.g_513c && !g_state.g_5140) {
                g_state.g_5124 = 1;
                cc_shell_notify_poison();
            }
            goto phase2;
        }
        if (w7 >= 0x25 && w7 <= 0x27 && w6 == 1) {
            g_state.g_a10a = 1;
            spawn_effect(0x1C, (int16_t)col, (int16_t)row);
            g_state.g_a10a = 0;
            play_sound(SND_PICKUP);
            score_add(0x1388, 0);
            cell_word_put(col, row, CELL_W7, 0);
            g_state.g_a0d0 = 0xFFFF;
            g_state.g_a0d2 = 0xFFFF;
            g_state.g_a10b = 0;
            g_state.g_a0ce = 0;
            mark_redraw(x, y);
            goto phase2;
        }
        if (w7 == 0x19 && w6 == 0x0E) {
            play_sound(SND_STUN);
            g_state.a0f8 = 1;
            cell_word_put(col, row, CELL_W7, 0);
            g_state.power_timer = g_state.g_a0e4;
            g_state.g_a0c6 = 0;
            if (g_state.g_a101) {
                helper_a110_clear();
                cc_shell_notify_stunned();
            }
            goto phase2;
        }
    }

phase2:
    if (w3 == 0) goto hazard;
    if ((w3 == 0x11 || w3 == 0x0D) && w5 == 0x0C) {
        if (g_state.g_a0f5) {
            if (!g_state.g_a0f6) {
                play_sound(SND_EXIT_LOCKED);
                if (g_state.g_509e == 3) {
                    helper_a59d();
                    cc_shell_notify_perfect_health();
                    score_add(0xC350, 0);
                }
                g_state.g_a0f6 = 1;
                g_state.exit_step = 0;
            }
        } else if (g_state.g_a106) {
            helper_a110_clear();
            cc_shell_notify_more_crystals();
            g_state.g_a106 = 0;
        }
        goto hazard;
    }
    if (w3 == 0x32 && w5 == 0x0D) {
        if (g_state.g_a0f9) {
            int shape;
            cell_word_put(col, row, CELL_W3, (uint16_t)(w3 - 2));
            play_sound(SND_PRIZE_CELL);
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
        if (g_state.g_a109) {
            helper_a110_clear();
            cc_shell_notify_lever();
            g_state.g_a109 = 0;
        }
        if (g_state.input_joystick_fire && g_state.g_a0f4) {
            g_state.g_a0f4 = 0;
            g_state.g_a0e8 = (uint16_t)(g_state.g_a0e8 + 1);
            if (g_state.g_a0e8 % 3 == 0) {
                cell_word_put(col, row, CELL_W3, (uint16_t)(w3 + 1));
                g_state.g_a0e8 = 0;
                g_state.input_fire = 1;
            }
        }
        if (g_state.input_fire) {
            play_sound(SND_SWITCH_TOGGLE);
            g_state.input_fire = 0;
            cell_word_put(col, row, CELL_W3, (uint16_t)(w3 + 1));
            switch_pair(g_state.g_a0d4, g_state.g_a0d6, 0x2D, 0x2E);
        }
        goto hazard;
    }
    if (w3 == 0x15 && w5 == 9) {
        if (g_state.g_a109) {
            helper_a110_clear();
            cc_shell_notify_lever();
            g_state.g_a109 = 0;
        }
        if (g_state.input_joystick_fire && g_state.g_a0f4) {
            g_state.g_a0f4 = 0;
            g_state.g_a0e8 = (uint16_t)(g_state.g_a0e8 + 1);
            if (g_state.g_a0e8 % 3 == 0) {
                cell_word_put(col, row, CELL_W3, (uint16_t)(w3 + 1));
                g_state.g_a0e8 = 0;
                g_state.input_fire = 1;
            }
        }
        if (g_state.input_fire) {
            play_sound(SND_SWITCH_TOGGLE);
            g_state.input_fire = 0;
            cell_word_put(col, row, CELL_W3, (uint16_t)(w3 + 1));
            switch_pair(g_state.g_a0d8, g_state.g_a0da, 0x31, 0x32);
        }
        goto hazard;
    }
    if (w3 == 0x13 && w5 == 9) {
        if (g_state.g_a109) {
            helper_a110_clear();
            cc_shell_notify_lever();
            g_state.g_a109 = 0;
        }
        if (g_state.input_joystick_fire && g_state.g_a0f4) {
            g_state.g_a0f4 = 0;
            g_state.g_a0e8 = (uint16_t)(g_state.g_a0e8 + 1);
            if (g_state.g_a0e8 % 3 == 0) {
                cell_word_put(col, row, CELL_W3, (uint16_t)(w3 + 1));
                g_state.g_a0e8 = 0;
                g_state.input_fire = 1;
            }
        }
        if (g_state.input_fire) {
            play_sound(SND_SWITCH_TOGGLE);
            g_state.input_fire = 0;
            cell_word_put(col, row, CELL_W3, (uint16_t)(w3 + 1));
            switch_pair(g_state.g_a0dc, g_state.g_a0de, 0x2F, 0x30);
        }
        goto hazard;
    }
    if (w3 == 0x0F && w5 == 9) {
        if (g_state.g_a108) { helper_a110_clear(); g_state.g_a108 = 0; }
        if (g_state.input_joystick_fire && g_state.g_a0f4) {
            g_state.g_a0f4 = 0;
            g_state.g_a0e8 = (uint16_t)(g_state.g_a0e8 + 1);
            if (g_state.g_a0e8 % 3 == 0) { g_state.g_a0e8 = 0; g_state.input_fire = 1; }
        }
        if (g_state.input_fire) {
            play_sound(SND_DOOR);
            g_state.input_fire = 0;
            cell_word_put(col, row, CELL_W3, (uint16_t)(w3 - 1));
            g_state.g_a0f3 = 0;
        }
        goto hazard;
    }
    if (w3 == 0x0E && w5 == 9) {
        if (g_state.g_a108) { helper_a110_clear(); g_state.g_a108 = 0; }
        if (g_state.input_joystick_fire && g_state.g_a0f4) {
            g_state.g_a0f4 = 0;
            g_state.g_a0e8 = (uint16_t)(g_state.g_a0e8 + 1);
            if (g_state.g_a0e8 % 3 == 0) { g_state.g_a0e8 = 0; g_state.input_fire = 1; }
        }
        if (g_state.input_fire) {
            play_sound(SND_DOOR);
            g_state.input_fire = 0;
            cell_word_put(col, row, CELL_W3, (uint16_t)(w3 + 1));
            g_state.g_a0f3 = 1;
        }
        goto hazard;
    }
    if (w3 == 0x1F && w5 == 0x0C) {
        if (g_state.g_a108) { helper_a110_clear(); g_state.g_a108 = 0; }
        if (g_state.input_joystick_fire && g_state.g_a0f4) {
            g_state.g_a0f4 = 0;
            g_state.g_a0e8 = (uint16_t)(g_state.g_a0e8 + 1);
            if (g_state.g_a0e8 % 3 == 0) {
                cell_word_put(col, row, CELL_W3, (uint16_t)(w3 + 1));
                g_state.g_a0e8 = 0;
                g_state.input_fire = 1;
            }
        }
        if (g_state.input_fire) {
            play_sound(SND_DOOR);
            g_state.input_fire = 0;
            cell_word_put(col, row, CELL_W3, (uint16_t)(w3 + 1));
            helper_toggle_kind((uint16_t)w0);
        }
        goto hazard;
    }
    if (w3 == 0x20 && w5 == 0x0C) {
        if (g_state.g_a108) { helper_a110_clear(); g_state.g_a108 = 0; }
        if (g_state.input_joystick_fire && g_state.g_a0f4) {
            g_state.g_a0f4 = 0;
            g_state.g_a0e8 = (uint16_t)(g_state.g_a0e8 + 1);
            if (g_state.g_a0e8 % 3 == 0) {
                cell_word_put(col, row, CELL_W3, (uint16_t)(w3 - 1));
                g_state.g_a0e8 = 0;
                g_state.input_fire = 1;
            }
        }
        if (g_state.input_fire) {
            play_sound(SND_DOOR);
            g_state.input_fire = 0;
            cell_word_put(col, row, CELL_W3, (uint16_t)(w3 - 1));
            helper_toggle_kind((uint16_t)w0);
        }
        goto hazard;
    }
    if (w3 == 0x0D && w5 == 1) {
        if (cave_flag_get(w0) != 0) {
            g_state.exit_col = (int16_t)col;
            g_state.exit_row = (int16_t)row;
            g_state.exit_step = 0;
            g_state.g_a0f7 = 1;
            g_state.g_2f6 = 0xBEEF; /* DEBUG */
            play_sound(SND_EXIT_FOUND);
            memcpy_(g_state.checkpoint_block, g_state.level_block, 0x21);
        }
        goto hazard;
    }

hazard:
    if (cell_byte_cur(col, row, CELL_FLAG_EXIT) == 0) return;
    if (!g_state.g_513c) return;
    if (g_state.g_5140) return;
    if ((w7 == 0x31 || w7 == 0x32 || w7 == 0x1B || w7 == 0x1C) &&
        w6 == 0x0A) {
        g_state.g_5124 = 1;
        play_sound(SND_HURT);
        return;
    }
    if (g_state.g_5124 == 0) {
        if ((int16_t)g_state.g_a0ea == -1) damage_life();
        if ((int16_t)g_state.g_5124 > 0) play_sound(SND_HURT);
    }
}

/* 0xE51D: apply hazard-cell damage for the two lower player corners. */
void hazard_cell(int16_t x, int16_t y) {
    int col = (int)x / 16;
    int row = (int)y / 16;
    if (cell_word_cur(col, row, CELL_W0) != (int)0xFF9F) return;
    if (!g_state.g_513c) return;
    if (g_state.g_5140) return;
    if ((int16_t)g_state.g_a0ea != -1) return;
    damage_life();
    if ((int16_t)g_state.g_5124 > 0) play_sound(SND_HURT);
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
    g_state.g_a10a = 1;

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
    g_state.g_a10a = 0;
    cell_word_put(col, row, CELL_W3, 0x32);
    g_state.anim_timer = 1;
    g_state.shape_player = (g_state.move_dir == 1) ? 0x19 : 0x1A;
}

