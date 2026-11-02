/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"

/* 0x1F9A:0x3E70 (kind 36): two-frame ambient animation. */
void h_ambient_anim_a(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    o->phase = (uint16_t)(o->phase + 1);
    if (o->phase > 0x7d00) o->phase = 0;
    if (o->phase % 2 == 0) anim_next(o);
    player_interact(o, x, y);
    player_interact(o, x, (int16_t)(y + 0x10));
}

/* 0x1F9A:0x4A4B (kind 31): two-frame ambient animation. */
void h_ambient_anim_b(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    o->phase = (uint16_t)(o->phase + 1);
    if (o->phase % 2 == 0) anim_next(o);
}

/* 0x1F9A:0x4A82 (kind 44): interaction helper with state toggles. */
void h_state_toggle(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    uint8_t old_513c = g_state.interaction_enabled;
    uint8_t old_5140 = (uint8_t)g_state.interaction_suppressed;
    g_state.interaction_enabled = 1;
    g_state.interaction_suppressed = 0;
    player_interact(o, x, y);
    g_state.interaction_enabled = old_513c;
    g_state.interaction_suppressed = old_5140;
}

/* 0x1F9A:0x1FC2 (kind 8): proximity-triggered companion spawner/mover. */
void h_companion_spawner(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int16_t r;
    anim_next(o);

    r = proximity_mode2(x, y, 0x70);
    if (r == 1 && idx > 0) {
        ObjectEntry *c = &g_state.obj[idx - 1];
        if (c->ready != 0) {
            play_sound(SND_COMPANION);
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
void h_spawn_below(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    if (o->y > g_state.player_y) return;
    if (idx + 1 < OBJ_MAX) {
        ObjectEntry *n = &g_state.obj[idx + 1];
        if (n->state == 2) anim_next(o);
        if (o->anim_frame == 6) {
            n->state = 0;
            n->ready = 0;
            n->x = o->x;
            n->y = (int16_t)(o->y + 8);
            o->anim_frame = 1;
        }
    }
}

/* 0x1F9A:0x2176 (kind 52): vertical mover / map companion. */
void h_map_companion(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int solid;
    (void)idx;
    solid = cell_solid_at(x + 8, y + 3) || cell_solid_at(x + 8, y + 0xc);
    if (solid) o->ready = 1;
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    player_interact(o, o->x, o->y);
}

/* 0x1FD4B: is this cell part of the level's collectible/base tile set? */
int cell_type_collectible(int col, int row) {
    int shape = cell_word_cur(col, row, CELL_W2);
    int base = g_state.g_5122;
    switch (g_state.g_511e) {
        case 4:
            if (cell_byte_cur(col, row, CELL_FLAG_SOLID) != 0) return 0;
            return shape == base || shape == base + 1 ||
                   shape == base + 4 || shape == base + 5;
        case 1:
            return shape == base;
        case 6:
            if (cell_byte_cur(col, row, CELL_FLAG_SOLID) != 0) return 0;
            return shape == base || shape == base + 1 || shape == base + 2 ||
                   shape == base + 4 || shape == base + 5 || shape == base + 6;
        case 8:
            if (cell_byte_cur(col, row, CELL_FLAG_SOLID) != 0) return 0;
            return shape >= base && shape <= base + 7;
        default:
            return 0;
    }
}

/* 0x200FE: terrain/cell test used by movers and slash paths. */
int cell_test_200fe(int x, int y) {
    if (cell_byte_cur((x + 3) / 16, (y + 8) / 16, CELL_FLAG_SOLID) != 0) return 1;
    if (cell_byte_cur((x + 0xC) / 16, (y + 8) / 16, CELL_FLAG_SOLID) != 0) return 1;
    if (cell_type_collectible((x + 4) / 16, (y + 0x12) / 16) &&
        cell_byte_cur((x + 8) / 16, (y + 0x12) / 16, CELL_FLAG_EXIT_LO) == 0)
        return 1;
    return 0;
}

/* 0x1F9A:0x1F58: shove the player by `amount` in `dir` (1=left, else
 * right) if the cell immediately beside them is clear. */
void push_player(int16_t amount, int16_t dir) {
    if (dir == 1) {
        if (!cell_test_200fe((int16_t)(g_state.player_x - 3), g_state.player_y)) {
            g_state.player_x = (int16_t)(g_state.player_x - amount);
            g_state.obj[1].x = g_state.player_x;
            g_state.scroll_x_accum = (uint16_t)amount;
            g_state.carry_x_delta = (uint16_t)amount;
        }
    } else {
        if (!cell_test_200fe((int16_t)(g_state.player_x + 3), g_state.player_y)) {
            g_state.scroll_x_accum = (uint16_t)(-(int16_t)amount);
            g_state.carry_x_delta = (uint16_t)(-(int16_t)amount);
            g_state.player_x = (int16_t)(g_state.player_x + amount);
            g_state.obj[1].x = g_state.player_x;
        }
    }
}

/* Turbo Pascal 32-bit multiply/divide helpers (0x2BB5:0x0279/0x0294). */
int32_t tp_long_mul(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a * (uint32_t)b);
}
int32_t tp_long_div(int32_t a, int32_t b) {
    if (b == 0) return 0;
    return (int32_t)(a / b);
}

/* 0x1F9A:0x1E74 helper: proximity test against the player using the object
 * record's current x/y.  mode 1: |dy| < r1 and |dx| < r2; mode 2:
 * |dx| < 8 and |dy| < r1.  Returns -1/1 for direction, 0 for no match. */
int16_t proximity_obj_dir(int mode, int16_t r1, int16_t r2, int slot) {
    ObjectEntry *p;
    int16_t d;
    if (slot < 0 || slot >= OBJ_MAX) return 0;
    p = &g_state.obj[slot];
    if (mode == 1) {
        d = (int16_t)(p->y - g_state.player_y);
        if (d < 0) d = (int16_t)-d;
        if (d >= r1) return 0;
        d = (int16_t)(p->x - g_state.player_x);
        if (d < 0) d = (int16_t)-d;
        if (d >= r2) return 0;
        return ((int16_t)(p->x - g_state.player_x) > 0) ? -1 : 1;
    }
    if (mode == 2) {
        d = (int16_t)(p->x - g_state.player_x);
        if (d < 0) d = (int16_t)-d;
        if (d >= 8) return 0;
        d = (int16_t)(p->y - g_state.player_y);
        if (d < 0) d = (int16_t)-d;
        if (d >= r1) return 0;
        return ((int16_t)(p->y - g_state.player_y) > 0) ? -1 : 1;
    }
    return 0;
}

/* 0x1F9A:0x5855 (kind 48): three-record moving group. */
void h_group_move3(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *n1 = (idx + 1 < OBJ_MAX) ? &g_state.obj[idx + 1] : o;
    ObjectEntry *n2 = (idx + 2 < OBJ_MAX) ? &g_state.obj[idx + 2] : o;
    int16_t x2 = (int16_t)(n2->x + (int32_t)n2->vx * n2->speed);
    int16_t y2 = (int16_t)(n2->y + (int32_t)n2->vy * n2->speed);
    int flip = 0;

    o->phase = (uint16_t)(o->phase + 1);
    if (o->phase % 2 == 0) {
        anim_next(o);
        anim_next(n1);
        anim_next(n2);
    }

    /* 0x1F9A:0x595C: if the player is close in the travel direction and the
     * previous slot is active, Random(100) < 5 respawns it at the middle
     * record's position. */
    if (idx > 0) {
        int16_t dir = proximity_obj_dir(1, 6, 0x130, idx);
        ObjectEntry *prev = &g_state.obj[idx - 1];
        if (dir == o->vx && prev->ready != 0) {
            if (cc_rng_next(100) < 5) {
                play_sound(SND_COMPANION);
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
void h_anim_mover(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
void h_expand_anim(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    o->phase = (uint16_t)(o->phase + 1);
    if (o->phase == 0x19) o->subtype = 1;
    if (o->subtype == 0) return;

    if (o->anim_frame > 6) {
        o->shape = (uint16_t)(o->shape + 3);
        o->anim_last = (uint16_t)(o->anim_last - 3);
        o->anim_frame = 1;
    }
    if (o->phase % 2 == 0) anim_next(o);
    o->subtype = (uint16_t)(o->subtype + 1);
    if (o->subtype > 0x3c) {
        o->phase = 0;
        o->subtype = 0;
        o->shape = (uint16_t)(o->shape - 3);
        o->anim_last = (uint16_t)(o->anim_last + 3);
        o->anim_frame = 1;
    }
    player_interact(o, o->x, o->y);
}

/* Clear a dug cell: +0x06/+0x10/+0x11 and w0=0xFFA1, then redraw/sound. */
void dig_cell(int col, int row) {
    if (cell_byte_cur(col, row, CELL_FLAG_SOLID_LO) == 0) return;
    cell_put(col, row, CELL_W3, 0);
    cell_put(col, row, CELL_FLAG_SOLID, 0);
    cell_put(col, row, CELL_FLAG_SOLID_LO, 0);
    cell_word_put(col, row, CELL_W0, 0xFFA1);
    mark_redraw((int16_t)(col * 16), (int16_t)(row * 16));
    play_sound(SND_DIG);
}

/* 0x1F9A:0x251D: slot-2 slash update (also the a0f8 direct path for kind 2). */
void h_slash_update(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    /* Handler args arrive as (y_new, x_new); the slash hitbox tests the
     * bottom edge cells (x, y+9) and (x+0xF, y+9). */
    int col = x / 16;
    int row = (y + 9) / 16;
    int col2 = (x + 0xF) / 16;
    int row2 = (y + 9) / 16;
    (void)idx;

    o->phase = (uint16_t)(o->phase + 1);
    anim_next(o);

    if (cell_byte_cur(col, row, CELL_FLAG_SOLID) != 0 ||
        cell_byte_cur(col2, row2, CELL_FLAG_SOLID) != 0) {
        o->kind = 0x2B;
        o->handler = HDL_DUG_BLOCK;
        o->handler_seg = 0x1F9A;
        o->phase = 0;
        dig_cell(col, row);
        dig_cell(col2, row2);
        return;
    }

    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    if (o->subtype == 1 && o->phase > 6 && o->speed < 0xE)
        o->speed = (uint16_t)(o->speed + 2);
}

/* Border-indexed cell helpers: the original's col/row already include the
 * +1 border, so logical = raw - 1. */
int cell_border_byte(int col, int row, int off) {
    return cell_byte_cur(col - 1, row - 1, off);
}
void cell_border_put(int col, int row, int off, int value) {
    cell_put(col - 1, row - 1, off, value);
}
int cell_border_word(int col, int row, int woff) {
    return cell_word_cur(col - 1, row - 1, woff);
}
void cell_border_word_put(int col, int row, int woff, int value) {
    cell_word_put(col - 1, row - 1, woff, value);
}
int cell_w0_is_dig(int col, int row) {
    return cell_border_word(col, row, CELL_W0) == (int)0xFFA1;
}

/* 0x1F9A:0x2241: dug-block placement and animation. */

void h_dug_block(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    if (o->phase == 0) {
        int col = o->x / 16 + 1;
        int row = (o->y + 8) / 16 + 1;
        int i = 0;

        play_sound(SND_FALLING_BLOCK);
        o->block = 1;
        o->shape = 0x1D;
        o->anim_shape_b = 0x1D;
        o->anim_shape_a = 0x1D;
        o->anim_frame = 0;
        o->anim_last = 4;
        o->phase = 1;

        if (o->vx == -1) {
            for (;;) {
                if (cell_border_byte(col, row, CELL_FLAG_SOLID) != 0) break;
                if (cell_w0_is_dig(col + 1, row)) break;
                if (i >= 2) break;
                col--;
                i++;
            }
            o->x = (int16_t)(col * 16);
            if (cell_w0_is_dig(col + 1, row)) {
                cell_border_word_put(col + 1, row, CELL_W0, 0);
            }
            o->vx = 0;
        } else if (o->vx == 1) {
            for (;;) {
                if (cell_border_byte(col, row, CELL_FLAG_SOLID) != 0) break;
                if (cell_w0_is_dig(col - 1, row)) break;
                if (i >= 3) break;
                col++;
                i++;
            }
            o->x = (int16_t)((col - 2) * 16);
            if (cell_w0_is_dig(col - 1, row)) {
                cell_border_word_put(col - 1, row, CELL_W0, 0);
            }
            o->vx = 0;
        }
    }

    if (o->phase == 1) {
        if (o->anim_frame < o->anim_last) o->anim_frame = (uint16_t)(o->anim_frame + 1u);
        else o->phase = (uint16_t)(o->phase + 1u);
    } else if (o->phase == 2) {
        if (o->anim_frame > 1) o->anim_frame = (uint16_t)(o->anim_frame - 1u);
        else o->ready = 1;
    }
}

