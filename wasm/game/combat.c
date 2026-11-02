/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"

/* 0x1F9A:0xA36 (0x203D6): apply a hit; loses one life/crystal unit. */
void damage_life(void) {
    if ((int16_t)g_state.g_509e > 1) {
        g_state.g_a0ea = 0;
        g_state.g_509e--;
        g_state.obj[1].hit_flag = 1;
    } else if (g_state.g_5124 == 0) {
        g_state.g_5124 = 1;
        g_state.g_509e--;
    }
}

int rect_overlap(int32_t ax0, int32_t ax1, int32_t bx0, int32_t bx1) {
    return ax0 <= bx1 && ax1 >= bx0;
}

/* 0x20406 slash matrices (ported in P2.4/P2.5). */
void slash_state1(ObjectEntry *o, int idx, int16_t x, int16_t y);
void slash_state2(ObjectEntry *o, int idx, int16_t x, int16_t y);

/* 0x2059A: transform an object into the kind-3 dissolve state. */
void transform_object(ObjectEntry *o) {
    switch (o->kind) {
        case 9: case 7: case 0x0A: case 0x39: case 0x1A:
        case 5: case 0x35: case 0x30: case 0x17: case 0x23:
        case 0x1C: case 8: case 0x0B: case 0x0C:
            o->x = (int16_t)((o->x / 16) * 16);
            o->vx = 0;
            o->vy = 0;
            o->phase = 0;
            o->subtype = 0;
            o->shape = 0x19;
            o->anim_last = 9;
            o->block = 7;
            o->anim_frame = 1;
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
void slash_gate(ObjectEntry *o, int16_t x, int16_t y) {
    ObjectEntry *a = &g_state.obj[2];
    int kind;

    if (!rect_overlap(a->x + 3, a->x + 12, x + 3, x + 12)) return;
    if (!rect_overlap(a->y, a->y + 15, y, y + 15)) return;
    if (a->ready != 0) return;

    if (g_state.g_5140 == 0 && g_state.g_513c != 0 && o->kind == 0x24) {
        o->anim_frame = 1;
        o->anim_last = 1;
        g_state.g_5124 = 2;
    }

    o->hit_points = (uint16_t)(o->hit_points - 1u);
    a->ready = 1;
    kind = o->kind;
    if (kind == 0x15 || kind == 0x1D || kind == 0x12 ||
        kind == 0x37 || kind == 0x19)
        a->ready = 0;

    if (a->subtype == 1) slash_state1(o, (int)(o - g_state.obj), x, y);
    else slash_state2(o, (int)(o - g_state.obj), x, y);
}

int cell_border_byte(int col, int row, int off);
void cell_border_put(int col, int row, int off, int value);

ObjectEntry *obj_safe(int i) {
    return (i >= 0 && i < OBJ_MAX) ? &g_state.obj[i] : 0;
}

/* 0x20824..0x20F47: normal slash hit matrix. */
void slash_state1(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int kind = o->kind;
    int side;
    ObjectEntry *p;
    int col, row;

    if ((int16_t)o->hit_points >= 1) {
        int k = o->kind;
        if (k == 0x24 || k == 0x16 || k == 0x1D || k == 0x15 ||
            k == 0x0E || k == 0x10 || k == 0x19)
            return;
        if (k == 0x2F) {
            p = obj_safe(idx + 1);
            side = (p && p->kind == 0x2F) ? 1 : -1;
            p = obj_safe(idx + side);
            if (p) {
                p->hit_points = (uint16_t)(p->hit_points - 1u);
                p->hit_flag = 1;
            }
            o->hit_flag = 1;
            if (o->vx == g_state.obj[2].vx) {
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
            o->hit_flag = 1;
            if ((p = obj_safe(idx + 1))) p->hit_flag = 1;
            if ((p = obj_safe(idx + 2))) p->hit_flag = 1;
            if (o->vx == g_state.obj[2].vx) {
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
            if (o->shape == 0x2D) o->hit_flag = 1;
            else o->hit_points = (uint16_t)(o->hit_points + 1u);
            if (o->vx == g_state.obj[2].vx) o->vx = (int16_t)-o->vx;
            return;
        }
        if (k == 0x35) {
            if (g_state.obj[2].vx == 1) {
                p = obj_safe(idx - 1);
                if (p && p->shape == 9) {
                    p->hit_points = (uint16_t)(p->hit_points - 1u);
                    if ((int16_t)p->hit_points < 1) p->shape = 0x15;
                    else p->hit_flag = 1;
                }
            } else {
                p = obj_safe(idx + 1);
                if (p && p->shape == 0x11) {
                    p->hit_points = (uint16_t)(p->hit_points - 1u);
                    if ((int16_t)p->hit_points < 1) p->shape = 0x19;
                    else p->hit_flag = 1;
                }
            }
            if ((obj_safe(idx + 1) && (int16_t)obj_safe(idx + 1)->hit_points > 0) ||
                (obj_safe(idx - 1) && (int16_t)obj_safe(idx - 1)->hit_points > 0))
                o->hit_points = (uint16_t)(o->hit_points + 1u);
            else
                o->hit_flag = 1;
            if (o->vx == g_state.obj[2].vx) {
                o->vx = (int16_t)-o->vx;
                if ((p = obj_safe(idx + 1))) p->vx = (int16_t)-p->vx;
                if ((p = obj_safe(idx - 1))) p->vx = (int16_t)-p->vx;
            }
            return;
        }
        o->hit_flag = 1;
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
        if (cell_border_byte(col, row + 1, CELL_FLAG_SOLID) == 0 &&
            cell_border_byte(col, row + 1, CELL_FLAG_EXIT_LO) == 0)
            col--;
        cell_border_word_put(col, row, CELL_W0, 0xFF9F);
        cell_border_word_put(col, row, CELL_W0, 0xFF9F);
        if (cell_border_word(col, row, CELL_W5) == 0) {
            cell_border_word_put(col, row, CELL_W5, o->block);
            cell_border_word_put(col, row, CELL_W3, kind == 0x09 ? 0x31 : 0x18);
        } else {
            cell_border_word_put(col, row, CELL_W6, o->block);
            cell_border_word_put(col, row, CELL_W7, kind == 0x09 ? 0x31 : 0x18);
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
            o->hit_points = (uint16_t)(o->hit_points + 1u);
            o->ready = 0;
            if (o->vx == g_state.obj[2].vx) o->vx = (int16_t)-o->vx;
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
        if (o->anim_shape_b == 0x2F) {
            p = obj_safe(idx + 1);
            if (p && p->kind == 0x33) {
                p->anim_shape_b = 0x2F;
                p->anim_shape_a = 0x25;
            }
            if (p) anim_select_shape(p);
            spawn_effect(0x18, (int16_t)((o->x + 8) / 16),
                         (int16_t)((o->y + 8) / 16));
            score_add(0x3E8, 0);
            spawn_template_2002d(1);
        } else {
            o->ready = 0;
            o->hit_flag = 1;
        }
        return;
    }
    if (kind == 0x2C) {
        play_sound(0x0D);
        col = o->x / 16 + 1;
        row = o->y / 16 + 1;
        cell_border_word_put(col, row, CELL_W7, g_state.g_512c + 9);
        cell_border_word_put(col, row, CELL_W6, 9);
        g_state.g_512c = (uint16_t)(g_state.g_512c + 1u);
        if (g_state.g_512c > 4) g_state.g_512c = 0;
        return;
    }

    spawn_template_2002d(1);
    score_add(100, 0);
}

/* 0x21342..0x21775: power slash hit matrix. */
void slash_state2(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
        cell_border_word_put(col, row, CELL_W7, g_state.g_512c + 9);
        cell_border_word_put(col, row, CELL_W6, 9);
        g_state.g_512c = (uint16_t)(g_state.g_512c + 1u);
        if (g_state.g_512c > 4) g_state.g_512c = 0;
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
        if (o->anim_shape_b == 0x2F) {
            p = obj_safe(idx + 1);
            if (p && p->kind == 0x33) {
                p->anim_shape_b = 0x2F;
                p->anim_shape_a = 0x25;
            }
            if (p) anim_select_shape(p);
            o->ready = 1;
            spawn_template_2002d(2);
            spawn_effect(0x18, (int16_t)((o->x + 8) / 16),
                         (int16_t)((o->y + 8) / 16));
            score_add(0x3E8, 0);
        } else {
            o->ready = 0;
            o->hit_flag = 1;
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
void player_interact(ObjectEntry *o, int16_t x, int16_t y) {
    int kind;

    if (!rect_overlap(g_state.player_x + 3, g_state.player_x + 12, x + 3, x + 12) ||
        !rect_overlap(g_state.player_y, g_state.player_y + 15, y, y + 15))
        goto slash_gate;

    if (g_state.g_513c == 0) {
        transform_object(o);
        goto slash_gate;
    }
    if (g_state.g_5140 != 0) goto slash_gate;
    if (g_state.g_5124 != 0) goto slash_gate;
    if (g_state.g_a0ea != 0xFFFF) goto slash_gate;
    if (g_state.g_a0f6 != 0) goto slash_gate;

    damage_life();
    kind = o->kind;
    switch (kind) {
        case 0x10: case 0x0E:
            g_state.g_a0ea = 0xFFFF;
            g_state.g_5124 = 1;
            g_state.obj[1].hit_flag = 0;
            break;
        case 0x24:
            g_state.g_5124 = 0;
            g_state.g_a0ea = 0xFFFF;
            g_state.g_509e++;
            g_state.obj[1].hit_flag = 0;
            break;
        case 0x16:
            if (g_state.g_5124 == 1)
                g_state.g_5124 = (g_state.gravity_scale == 1) ? 3 : 1;
            break;
        case 0x2C:
            g_state.g_5124 = 0;
            g_state.g_a0ea = 0xFFFF;
            g_state.g_509e++;
            g_state.obj[1].hit_flag = 0;
            o->ready = 1;
            spawn_effect(0x18, (int16_t)(x / 16), (int16_t)(y / 16));
            play_sound(8);
            score_add(1000, 0);
            break;
        default:
            break;
    }
    if ((int16_t)g_state.g_5124 > 0) play_sound(5);

slash_gate:
    slash_gate(o, x, y);
}

