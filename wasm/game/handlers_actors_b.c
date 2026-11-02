/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"


/* 0x1F9A:0x5C8F (kind 0x3A): three-object boss formation. */
void h_boss_formation(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *prev, *next;
    int16_t prev_x, prev_y, next_x, next_y, dir;
    int flip = 0;
    (void)x; (void)y;
    if (idx < 2 || idx + 1 >= OBJ_MAX) return;
    prev = &g_state.obj[idx - 1];
    next = &g_state.obj[idx + 1];
    prev_x = (int16_t)(prev->x + (int32_t)prev->vx * prev->speed);
    prev_y = (int16_t)(prev->y + (int32_t)prev->vy * prev->speed);
    next_x = (int16_t)(next->x + (int32_t)next->vx * next->speed);
    next_y = (int16_t)(next->y + (int32_t)next->vy * next->speed);

    if (g_state.g_a0f0 != 0) {
        if (cc_rng_next(1000) < 0x14 && (int16_t)prev->shape < 0x15) {
            prev->shape = 5;
            prev->anim_frame = (uint16_t)(prev->anim_last + 1u);
            g_state.g_a0f0 = 0;
        }
    }
    if (g_state.g_a0fa != 0) {
        if (cc_rng_next(1000) < 0x14 && (int16_t)next->shape < 0x15) {
            next->shape = 0x0D;
            next->anim_frame = (uint16_t)(next->anim_last + 1u);
            g_state.g_a0fa = 0;
        }
    }
    if (g_state.g_a0f0 == 0 && cc_rng_next(1000) < 0x14) g_state.g_a0f0 = 1;
    if (g_state.g_a0fa == 0 && cc_rng_next(1000) < 0x14) g_state.g_a0fa = 1;

    o->phase = (uint16_t)(o->phase + 1u);
    if ((int16_t)o->phase % 2 == 0) {
        if (g_state.g_a0f0 == 0) {
            prev->anim_frame = (uint16_t)(prev->anim_frame - 1u);
            if ((int16_t)prev->anim_frame < 1) prev->anim_frame = 1;
        } else {
            if ((int16_t)prev->shape == 5 &&
                (int16_t)prev->anim_frame == (int16_t)prev->anim_last)
                prev->shape = 9;
            anim_next(prev);
        }
        anim_next(o);
        if (g_state.g_a0fa == 0) {
            next->anim_frame = (uint16_t)(next->anim_frame - 1u);
            if ((int16_t)next->anim_frame < 1) next->anim_frame = 1;
        } else {
            if ((int16_t)next->shape == 0x0D &&
                (int16_t)next->anim_frame == (int16_t)next->anim_last)
                next->shape = 0x11;
            anim_next(next);
        }
    }

    dir = proximity_obj_dir(1, 6, 0x130, idx);
    if ((int16_t)o->vx == dir && idx >= 2 &&
        g_state.obj[idx - 2].ready != 0 && cc_rng_next(100) < 5) {
        play_sound(0x10);
        g_state.obj[idx - 2].vx = (uint16_t)dir;
        g_state.obj[idx - 2].ready = 0;
        g_state.obj[idx - 2].state = 0;
        g_state.obj[idx - 2].x = o->x;
        g_state.obj[idx - 2].y = o->y;
        anim_select_shape(&g_state.obj[idx - 2]);
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
void h_walk_respawn(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int16_t dir;
    anim_next(o);
    if (cell_test_200fe(x, y)) {
        o->vx = (int16_t)(-o->vx);
        anim_select_shape(o);
    }
    dir = proximity_obj_dir(1, 6, 0x130, idx);
    if ((int16_t)o->vx == dir && idx > 0 &&
        g_state.obj[idx - 1].ready != 0 && cc_rng_next(100) < 5) {
        play_sound(0x10);
        g_state.obj[idx - 1].vx = (uint16_t)dir;
        g_state.obj[idx - 1].ready = 0;
        g_state.obj[idx - 1].state = 0;
        g_state.obj[idx - 1].x = o->x;
        g_state.obj[idx - 1].y = o->y;
    }
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x3416 (kind 0x0C): proximity walker with companion spawn. */
void h_walk_prox_spawn(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *child;
    o->phase = (uint16_t)proximity_test(1, 0x15, x, y);
    if ((int16_t)o->x > (int16_t)g_state.player_x &&
        (int16_t)o->phase == -1 && (int16_t)o->vx == 1)
        o->phase = 0;
    if (idx + 1 >= OBJ_MAX) return;
    child = &g_state.obj[idx + 1];
    if (o->phase != 0 && (int16_t)o->phase == (int16_t)o->vx) {
        child->state = 0;
        child->ready = 0;
        child->y = o->y;
        player_interact(child, (int16_t)child->x, (int16_t)child->y);
        anim_next(child);
        if (o->phase == 1) {
            o->anim_frame = 9;
            child->x = (int16_t)(o->x + 0x11);
        } else {
            o->anim_frame = 6;
            child->x = (int16_t)(o->x - 0x10);
        }
        return;
    }
    child->ready = 1;
    anim_next(o);
    o->subtype = (uint16_t)(o->subtype + 1u);
    if ((int16_t)o->subtype > 0x1e) {
        o->subtype = 0;
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
void h_move_v_prox(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
void h_walk_flip_contact(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    if (cell_test_200fe(x, y) && o->phase == 0) {
        o->vx = (int16_t)(-o->vx);
        o->phase = 1;
    }
    if ((int16_t)o->phase > 0) {
        o->phase = (uint16_t)(o->phase + 1u);
        o->anim_frame = 1;
        if ((int16_t)o->phase > 0x39) o->phase = 0;
        player_interact(o, (int16_t)o->x, (int16_t)o->y);
        return;
    }
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    anim_next(o);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x2BD4 (kind 0x07): proximity-triggered horizontal walker. */
void h_walk_h_prox(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    if (o->state == 3)
        o->vx = (uint16_t)proximity_test(1, 0x80, x, y);
    if ((int16_t)o->vx == 0) return;
    o->state = 0;
    if ((int16_t)o->anim_frame > 4 && o->phase != 1) {
        o->phase = 1;
        o->anim_shape_a = (uint16_t)(o->anim_shape_a + 4u);
        o->anim_shape_b = (uint16_t)(o->anim_shape_b + 4u);
        o->anim_last = (uint16_t)(o->anim_last - 4u);
    }
    anim_next(o);
    if (cell_test_200fe(x, y))
        o->vx = (int16_t)(-o->vx);
    anim_select_shape(o);
    if (o->phase == 1)
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x3904 (kind 0x0F): proximity-triggered lever/switch. */
void h_lever_switch(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    int16_t dir;
    if (o->state == 1) o->state = 3;
    dir = proximity_test(2, 0x40, (int16_t)o->x, (int16_t)o->y);
    if (dir != 0 && (int16_t)(g_state.player_y - 1) < (int16_t)o->y) {
        o->state = 0;
        if ((int16_t)o->anim_frame < (int16_t)o->anim_last)
            o->anim_frame = (uint16_t)(o->anim_frame + 1u);
        if ((int16_t)o->anim_frame > 1 && (int16_t)o->anim_frame < 4)
            play_sound(0x1f);
        player_interact(o, (int16_t)o->x, (int16_t)o->y);
        return;
    }
    o->state = 1;
    o->anim_frame = 1;
}

/* 0x1F9A:0x47B9 (kind 0x0B): walker with two animation phases. */
void h_walk_two_anim(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    anim_next(o);
    if (cell_test_200fe(x, y)) {
        o->vx = (int16_t)(-o->vx);
        anim_select_shape(o);
    }
    o->subtype = (uint16_t)(o->subtype + 1u);
    if ((int16_t)o->subtype < (int16_t)(o->phase + 0xa)) {
        o->anim_last = 9;
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    } else if ((int16_t)o->subtype < (int16_t)(o->phase + 0x19)) {
        o->shape = 0x0e;
        o->anim_last = 7;
    } else {
        o->subtype = 0;
        o->anim_frame = 1;
    }
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x32F6 (kind 0x19): companion-driven vertical mover. */
void h_move_v_companion(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    if (o->state == 2 && idx > 0) anim_next(&g_state.obj[idx - 1]);
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    anim_next(o);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
    if (cell_byte_cur((o->x + 2) / 16, (o->y + 8) / 16, CELL_FLAG_SOLID) != 0 ||
        cell_byte_cur((o->x + 2) / 16, (o->y + 8) / 16, CELL_FLAG_EXIT_LO) != 0) {
        play_sound(0x18);
        o->ready = 1;
    }
}

/* 0x1F9A:0x4145 (kind 0x23): bouncing adversary. */
int cell_blocked_4145(int col, int row) {
    return cell_byte_cur(col, row, CELL_FLAG_SOLID) != 0 ||
           cell_word_cur(col, row, CELL_W0) == 0xFFA0;
}
void h_bounce_adversary(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
                    o->shape = 0x25; o->anim_frame = 1; o->vx = -1; o->vy = 0;
                }
                break;
            case 1:
                if (cell_blocked_4145((o->x + 0x14) / 16, (o->y + 8) / 16)) {
                    o->vx = 0; o->vy = 0;
                } else {
                    o->shape = 0x21; o->anim_frame = 1; o->vx = 1; o->vy = 0;
                }
                break;
            case 2:
                if (cell_blocked_4145((o->x + 8) / 16, (o->y - 4) / 16)) {
                    o->vx = 0; o->vy = 0;
                } else {
                    o->shape = 0x29; o->anim_frame = 1; o->vx = 0; o->vy = -1;
                }
                break;
            default:
                if (cell_blocked_4145((o->x + 8) / 16, (o->y + 0x14) / 16)) {
                    o->vx = 0; o->vy = 0;
                } else {
                    o->shape = 0x2d; o->anim_frame = 1; o->vx = 0; o->vy = 1;
                }
                break;
        }
    }
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x48FE (kind 0x0A): random walker; flips at walls/proximity. */
void h_walk_random(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    anim_next(o);
    o->subtype = (uint16_t)(o->subtype + 1u);
    if ((int16_t)o->subtype > 0x1e) {
        o->subtype = 0;
        if (cc_rng_next(100) > 0x32) {
            o->vx = (int16_t)(-o->vx);
            anim_select_shape(o);
        }
    }
    if (cell_byte_cur((x + 3) / 16, (y + 8) / 16, CELL_FLAG_SOLID) ||
        cell_byte_cur((x + 0xc) / 16, (y + 8) / 16, CELL_FLAG_SOLID)) {
        o->vx = (int16_t)(-o->vx);
        anim_select_shape(o);
    }
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x6458 (kind 0x38): companion spawn/move pair. */
void h_companion_pair(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *n;
    int16_t dir;
    int16_t rem;
    if (o->state == 3) return;
    if (idx + 1 >= OBJ_MAX) return;
    n = &g_state.obj[idx + 1];
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
        o->phase = 1;
    }
    anim_next(o);
    if (cell_byte_cur((x + 8) / 16, (y + 5) / 16, CELL_FLAG_SOLID) ||
        cell_byte_cur((x + 8) / 16, (y + 0xa) / 16, CELL_FLAG_SOLID) ||
        cell_byte_cur((x + 8) / 16, (y + 5) / 16, CELL_FLAG_EXIT_LO) != 0)
        o->vy = (int16_t)(-o->vy);
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
}

/* 0x1F9A:0x4FD4 (kind 0x39): multi-state boss/companion retargeter. */
void h_boss_retarget(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *cm1 = (idx >= 1) ? &g_state.obj[idx - 1] : o;
    ObjectEntry *cm2 = (idx >= 2) ? &g_state.obj[idx - 2] : o;
    int16_t dir;

    if (o->subtype == 0) {
        if ((int16_t)o->phase <= (int16_t)g_state.g_260) goto p1a_handling;
        if ((int16_t)(o->x % 16) != 0) goto p1a_handling;
        o->phase = 0;
        o->subtype = 1;
        o->anim_frame = 1;
        o->shape = 0x2d;
        cm2->state = 0;
        cm2->ready = 0;
        cm2->x = o->x;
        cm2->y = (int16_t)(o->y - 0x10);
        g_state.g_260 = (uint16_t)(cc_rng_next(0x32) + 0x32);
    }

p1a_handling:
    if ((int16_t)o->subtype > 0x24) {
        o->subtype = 0;
        cm2->ready = 1;
        anim_select_shape(o);
        goto p1a_check;
    }
    if ((int16_t)o->subtype <= 0) goto p1a_check;
    o->subtype = (uint16_t)(o->subtype + 1u);
    if ((int16_t)o->subtype % 3 != 0) goto p1a_check;
    anim_next(cm2);

p1a_check:
    if (o->subtype != 0) goto interact_only;

    o->phase = (uint16_t)(o->phase + 1u);
    if ((int16_t)o->phase % 2 == 0) anim_next(o);

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
