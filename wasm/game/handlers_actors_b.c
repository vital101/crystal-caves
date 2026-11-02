/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"


/* 0x1F9A:0x5C8F (kind 0x3A): three-object boss formation. */
void h_5c8f(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
            prev->p10 = (uint16_t)(prev->p16 + 1u);
            g_state.g_a0f0 = 0;
        }
    }
    if (g_state.g_a0fa != 0) {
        if (cc_rng_next(1000) < 0x14 && (int16_t)next->shape < 0x15) {
            next->shape = 0x0D;
            next->p10 = (uint16_t)(next->p16 + 1u);
            g_state.g_a0fa = 0;
        }
    }
    if (g_state.g_a0f0 == 0 && cc_rng_next(1000) < 0x14) g_state.g_a0f0 = 1;
    if (g_state.g_a0fa == 0 && cc_rng_next(1000) < 0x14) g_state.g_a0fa = 1;

    o->p18 = (uint16_t)(o->p18 + 1u);
    if ((int16_t)o->p18 % 2 == 0) {
        if (g_state.g_a0f0 == 0) {
            prev->p10 = (uint16_t)(prev->p10 - 1u);
            if ((int16_t)prev->p10 < 1) prev->p10 = 1;
        } else {
            if ((int16_t)prev->shape == 5 &&
                (int16_t)prev->p10 == (int16_t)prev->p16)
                prev->shape = 9;
            anim_next(prev);
        }
        anim_next(o);
        if (g_state.g_a0fa == 0) {
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
void h_62f2(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
void h_3416(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *child;
    o->p18 = (uint16_t)proximity_test(1, 0x15, x, y);
    if ((int16_t)o->x > (int16_t)g_state.player_x &&
        (int16_t)o->p18 == -1 && (int16_t)o->vx == 1)
        o->p18 = 0;
    if (idx + 1 >= OBJ_MAX) return;
    child = &g_state.obj[idx + 1];
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
void h_2d37(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
void h_2acf(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
void h_2bd4(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
void h_3904(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    int16_t dir;
    if (o->state == 1) o->state = 3;
    dir = proximity_test(2, 0x40, (int16_t)o->x, (int16_t)o->y);
    if (dir != 0 && (int16_t)(g_state.player_y - 1) < (int16_t)o->y) {
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
void h_47b9(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
void h_32f6(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    if (o->state == 2 && idx > 0) anim_next(&g_state.obj[idx - 1]);
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
int cell_blocked_4145(int col, int row) {
    return cell_byte_cur(col, row, 0x11) != 0 ||
           cell_word_cur(col, row, 0x00) == 0xFFA0;
}
void h_4145(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
void h_48fe(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
void h_6458(ObjectEntry *o, int idx, int16_t x, int16_t y) {
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
void h_4fd4(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *cm1 = (idx >= 1) ? &g_state.obj[idx - 1] : o;
    ObjectEntry *cm2 = (idx >= 2) ? &g_state.obj[idx - 2] : o;
    int16_t dir;

    if (o->p1a == 0) {
        if ((int16_t)o->p18 <= (int16_t)g_state.g_260) goto p1a_handling;
        if ((int16_t)(o->x % 16) != 0) goto p1a_handling;
        o->p18 = 0;
        o->p1a = 1;
        o->p10 = 1;
        o->shape = 0x2d;
        cm2->state = 0;
        cm2->ready = 0;
        cm2->x = o->x;
        cm2->y = (int16_t)(o->y - 0x10);
        g_state.g_260 = (uint16_t)(cc_rng_next(0x32) + 0x32);
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

handler_fn handler_for(uint16_t off) {
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
