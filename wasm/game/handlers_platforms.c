/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"

/* ---------------------------------------------------------------- handlers
 * The original calls a handler with (y_new, x_new, object_index) on the
 * stack (retf 6).  C handlers receive the same values explicitly so they can
 * reproduce the original platform/player interaction.
 */

int cell_solid_at(int32_t x, int32_t y) {
    return cell_solid_cur((int)(x / 16), (int)(y / 16));
}

/* 0x1F9A:0x1FB9 (kind 1): pure noop (retf 6). */
void h_player_noop(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)o; (void)idx; (void)x; (void)y;
}

void h_slash_update(ObjectEntry *o, int idx, int16_t x, int16_t y);
void load_level(int level, int new_game, int fresh_image,
                       int sound_start);
void cc_warp_level(int level);
void load_arg(int arg);

/* 0x1F9A:0x2850 (kind 2): ready gate around the slash update 0x251D. */
void h_attack_gate(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    if (o->ready == 0) h_slash_update(o, idx, x, y);
}

/* 0x1F9A:0x2877 (kind 3): transformed-object dissolve animation. */
void h_dissolve(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    o->phase = (uint16_t)(o->phase + 1u);
    if (o->block == 0x0D) {
        if (o->phase > 0x10) o->shape = 0x19;
        if (o->phase > 0x14) {
            o->phase = 0;
            o->ready = 1;
            o->shape = 0x11;
        }
    } else {
        if (o->phase > 8) {
            o->ready = 1;
            o->phase = 0;
        }
    }
    if (o->x > 2 && o->x < 0x26C)
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    anim_next(o);
}

/* 0x1F9A:0x6FF6 (kind 32): moving hazard/platform.
 * Flips p1e at the x bounds; advances x on even phase frames. */
void h_flip_mover(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    g_state.g_512a = (uint16_t)idx;

    if (!(o->x > 0x10 && o->x < 0x270)) {
        o->vx = (int16_t)(-o->vx);
        o->x = (int16_t)(o->x + (int32_t)o->vx * 2);
    }

    if (o->phase < 0x7d00u) o->phase = (uint16_t)(o->phase + 1);
    else o->phase = 0;

    if ((o->phase & 1u) == 0u) {
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    }
}

/* 0x1F9A:0x70E4 (kind 33): partner mover.  It swaps its record with the
 * object last touched by 0x6FF6 at phase==3 and adjusts the partner shape. */
void h_flip_partner(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    int16_t dx;
    int skip_flip;

    g_state.g_513a = (uint16_t)idx;
    anim_next(o);

    dx = (int16_t)(o->x - g_state.obj[g_state.g_512a].x);
    if (dx < 0) dx = (int16_t)-dx;

    if (dx > 0x23) {
        skip_flip = (o->phase != 0 && o->x > 0x10 && o->x < 0x270);
    } else {
        skip_flip = (o->x > 0x10 && o->x < 0x270);
    }
    if (!skip_flip) {
        o->phase = 1;
        o->vx = (int16_t)(-o->vx);
    }

    if (o->phase != 0) {
        o->phase = (uint16_t)(o->phase + 1);
        if (o->phase > 5) o->phase = 0;
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);

        if (o->phase == 3) {
            int other = (int)g_state.g_512a;
            ObjectEntry tmp = *o;
            *o = g_state.obj[other];
            g_state.obj[other] = tmp;

            {
                uint16_t old = g_state.g_513a;
                g_state.g_513a = g_state.g_512a;
                g_state.g_512a = old;
            }
            if ((int16_t)g_state.g_512a > (int16_t)g_state.g_513a) {
                g_state.obj[g_state.g_513a].shape++;
            } else {
                g_state.obj[g_state.g_513a].shape--;
            }
        }
    } else {
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    }
}

/* 0x1F9A:0x3F13 (kind 60): door/entrance animation. */
void h_door(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    o->phase = (uint16_t)(o->phase + 1);
    if (o->phase > 0x7d00) o->phase = 0;
    if (o->phase % 2 == 0) anim_next(o);
}

/* 0x1F9A:0x3D23 (kind 34): two-record door/lever animation. */
void h_door_lever(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    ObjectEntry *n = (idx + 1 < OBJ_MAX) ? &g_state.obj[idx + 1] : o;

    o->phase = (uint16_t)(o->phase + 1);
    if (o->phase > 0x12c) {
        o->shape = 0x19;
        n->shape = 0x15;
        o->anim_frame = 0;
        n->anim_frame = 0;
        o->phase = 0;
    }

    if (o->phase % 3 == 0 && o->phase < 0x51) {
        anim_next(o);
        anim_next(n);
    }

    if (o->phase > 0x50) {
        o->shape = 0x32;
        n->shape = 0x32;
        o->anim_frame = 1;
        n->anim_frame = 1;
        mark_redraw(o->x, o->y);
        mark_redraw(n->x, n->y);
    }
}

/* 0x1F9A:0x6AFA (kind 19): horizontal moving platform. */
void h_platform_h(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    if (cell_solid_at(x + 3, y + 8) || cell_solid_at(x + 12, y + 8)) {
        o->vx = (int16_t)(-o->vx);
    }
    anim_next(o);

    if (g_state.player_y + 0x10 > o->y - 5) {
        if (g_state.player_y + 0x10 < o->y + 5 &&
            g_state.player_x + 8 > o->x - 4 &&
            g_state.player_x + 8 < o->x + 0x14 &&
            g_state.gravity_scale == 1 &&
            g_state.jump_phase > 0xA) {
            g_state.player_y = (int32_t)o->y - 0x10;
            g_state.g_513d = 1;
            o->subtype = 1;
            g_state.g_511a = (uint16_t)idx;
            goto move;
        }
    }

    if (g_state.g_513d) {
        if ((uint16_t)idx != g_state.g_511a) goto move;
    }
    g_state.g_513d = 0;
    if (o->subtype == 0) goto move;
    o->subtype = 0;
    g_state.g_5126 = 0;
    g_state.g_5118 = 0;

move:
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    if (o->subtype != 1) return;
    if (o->vx == 1) {
        g_state.g_5118 = o->speed;
        if (player_clear((int16_t)g_state.g_5118, 0)) {
            g_state.player_x += (int16_t)g_state.g_5118;
        }
    } else {
        g_state.g_5118 = (uint16_t)(-(int16_t)o->speed);
        if (player_clear((int16_t)g_state.g_5118, 0)) {
            g_state.player_x += (int16_t)g_state.g_5118;
        }
    }
}

/* 0x1F9A:0x6D31 (kind 20/0x27): vertical moving platform. */
void h_platform_v(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    if (cell_solid_at(x + 8, y - 0x14) || cell_solid_at(x + 8, y + 0x10)) {
        o->vy = (int16_t)(-o->vy);
    }
    anim_next(o);

    if (!g_state.g_5142) {
        if (g_state.player_y + 0x10 > o->y - 5 &&
            g_state.player_y + 0x10 < o->y + 5 &&
            g_state.player_x + 8 > o->x - 4 &&
            g_state.player_x + 8 < o->x + 0x14 &&
            g_state.gravity_scale == 1) {
            g_state.player_y = (int32_t)o->y - 0x10;
            g_state.g_5142 = 1;
            g_state.g_511c = (uint16_t)idx;
        }
    }

    if (g_state.player_y + 0x10 == o->y &&
        g_state.player_x + 8 > o->x - 4 &&
        g_state.player_x + 8 < o->x + 0x14 &&
        g_state.gravity_scale == 1) {
        g_state.g_5142 = 1;
        o->subtype = 1;
        g_state.g_511c = (uint16_t)idx;
        goto move;
    }

    if (!g_state.g_5142 || (uint16_t)idx == g_state.g_511c) {
        g_state.g_5142 = 0;
        o->subtype = 0;
        g_state.g_5128 = 0;
    }

move:
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    if (o->subtype != 1 || g_state.gravity_scale != 1) return;
    if (o->vy == 1) {
        g_state.g_5128 = o->speed;
        if (player_clear(0, (int16_t)g_state.g_5128)) {
            g_state.player_y += (int16_t)g_state.g_5128;
        }
    } else {
        g_state.g_5128 = (uint16_t)(-(int16_t)o->speed);
        if (player_clear(0, (int16_t)g_state.g_5128)) {
            g_state.player_y += (int16_t)g_state.g_5128;
        }
    }
}

/* 0x1F9A:0x831 (0x201D1): short-lived pickup/effect object. */
void h_pickup_effect(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    if (o->phase > 0x10) o->ready = 1;
    o->phase = (uint16_t)(o->phase + 1);
    if ((int16_t)o->y > 0x12)
        o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
}

/* 0x59 SetCell state side: stamp the base cell at (xcell,ycell). */
void base_cell_stamp(int xcell, int ycell) {
    int shape = g_state.g_5122;
    int cache = (shape == g_state.g_5122) ? 0 : 0xFFFF;
    if (shape == g_state.g_5122) {
        if (g_state.g_511e == 4) {
            if (ycell & 1) { shape += 4; cache += 2; }
            if (xcell & 1) { shape += 1; cache += 1; }
        } else if (g_state.g_511e == 6) {
            if (ycell & 1) { shape += 4; cache += 3; }
            if ((xcell % 3) == 0) { shape += 2; cache += 2; }
            else if ((xcell % 3) == 2) { shape += 1; cache += 1; }
        } else if (g_state.g_511e == 8) {
            if (ycell & 1) { shape += 4; cache += 4; }
            if ((xcell % 4) == 0) { shape += 3; cache += 3; }
            else if ((xcell % 4) == 3) { shape += 2; cache += 2; }
            else if ((xcell % 4) == 2) { shape += 1; cache += 1; }
        }
    }
    if (g_state.g_30d) cache = 0xFFFF;
    g_state.g_2f6 = (uint16_t)cache;
    cell_word_put(xcell, ycell, CELL_W0, 0);
    cell_word_put(xcell, ycell, CELL_W1, (uint16_t)cache);
    cell_word_put(xcell, ycell, CELL_W2, (uint16_t)shape);
    cell_word_put(xcell, ycell, CELL_W3, 0);
    cell_word_put(xcell, ycell, CELL_W4, g_state.g_2b8);
    cell_word_put(xcell, ycell, CELL_W5, 0);
    cell_word_put(xcell, ycell, CELL_W6, 0);
    cell_word_put(xcell, ycell, CELL_W7, 0);
    cell_word_put(xcell, ycell, CELL_W8, 0);
    cell_word_put(xcell, ycell, CELL_W9, 0);
}

/* 0x1FFC8: walk the cursor back over reusable free slots. */
void reuse_free_slot(void) {
    while (g_state.obj_cursor > 0) {
        ObjectEntry *o = &g_state.obj[g_state.obj_cursor];
        if (o->state != 2) break;
        if (!(o->kind == 3 || o->kind == 0x2A ||
              o->kind == 0x2D || o->kind == 0x2E)) break;
        g_state.obj_cursor--;
    }
}

/* 0x2002D: update the slot-3 template and copy it to the next slot. */
void spawn_template_2002d(int mode) {
    ObjectEntry *t;
    reuse_free_slot();
    if (g_state.obj[3].state != 2 && g_state.obj_cursor + 1 < OBJ_MAX) {
        g_state.obj_cursor++;
        g_state.obj[g_state.obj_cursor] = g_state.obj[3];
    }
    t = &g_state.obj[3];
    t->x = g_state.obj[2].x;
    t->y = g_state.obj[2].y;
    t->x_prev = g_state.obj[2].x;
    t->y_prev = g_state.obj[2].y;
    t->vx = g_state.obj[2].vx;
    t->vy = -1;
    if (mode == 1) {
        t->shape = 0x11;
        t->anim_last = 8;
        t->block = 0x0D;
    } else {
        t->shape = 0x19;
        t->anim_last = 9;
        t->block = 7;
    }
    t->anim_frame = 1;
    t->phase = 0;
    t->kind = 3;
    t->ready = 0;
    t->state = 0;
}

/* 0x47D: general object spawn (state side; drawing is renderer-only). */
void spawn_object_47d(const SpawnArgs *a) {
    ObjectEntry *o;
    int16_t px = (int16_t)(a->xcell << 4);
    int16_t py = (int16_t)(a->ycell << 4);
    if (a->slot < 0 || a->slot >= OBJ_MAX) return;
    o = &g_state.obj[a->slot];
    o->shape = (a->vx == 1) ? a->anim_shape_b : a->anim_shape_a;
    if (!g_state.g_a10a) base_cell_stamp(a->xcell, a->ycell);
    o->block = a->block;
    o->anim_shape_b = a->anim_shape_b;
    o->anim_shape_a = a->anim_shape_a;
    o->x = px;
    o->y = py;
    o->x_prev = px;
    o->y_prev = py;
    o->vx = (int16_t)a->vx;
    o->vy = (int16_t)a->vy;
    o->anim_frame = 1;
    o->anim_last = a->anim_last;
    o->speed = a->speed;
    o->phase = a->phase;
    o->subtype = 0;
    o->hit_flag = 0;
    o->hit_points = 1;
    o->kind = a->kind;
    o->ready = 0;
    o->state = 0;
    o->handler = a->handler;
    o->handler_seg = a->handler_seg;
}

/* 0x20247: spawn the 0x831 effect object at cell (xcell,ycell). */
void spawn_effect(int16_t shape, int16_t xcell, int16_t ycell) {
    int s;
    ObjectEntry *o;
    if (g_state.obj_cursor + 1 >= OBJ_MAX) return;
    s = ++g_state.obj_cursor;
    o = &g_state.obj[s];
    *o = (ObjectEntry){0};
    o->hit_points = 0x7D00;
    o->x = (int16_t)(xcell * 16);
    o->y = (int16_t)(ycell * 16);
    o->x_prev = o->x;
    o->y_prev = o->y;
    o->block = 0x12;
    o->anim_frame = 1;
    o->anim_shape_a = (uint16_t)shape;
    o->anim_shape_b = (uint16_t)shape;
    o->anim_last = 1;
    o->phase = 0;
    o->subtype = 0;
    o->shape = (uint16_t)shape;
    o->vx = 0;
    o->vy = -1;
    o->speed = 2;
    o->kind = 0x2a;
    o->ready = 0;
    o->state = 0;
    o->handler = HDL_PICKUP_EFFECT;
    o->handler_seg = 0x1F9A;
}

