/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"

/* 0x6681 (entry 0x26021): cell-erase effect; only y changes. */
void h_cell_erase(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    mark_redraw((int16_t)(o->x_prev), (int16_t)(o->y_prev + 0x11));
    mark_redraw((int16_t)(o->x_prev + 0x0F), (int16_t)(o->y_prev + 0x11));
    mark_redraw((int16_t)(o->x_prev + 0x0F), (int16_t)(o->y_prev));
}

/* 0x672B (entry 0x260CB): crystal collection score/bob effect. */
void h_crystal_bob(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    o->phase = (uint16_t)(o->phase + 1u);
    if (o->subtype == 0) {
        if (o->phase == 1) o->y = (int16_t)(o->y + 0x0C);
        else o->y = (int16_t)(o->y - 0x0C);
        if ((int16_t)o->phase > 1) {
            o->subtype = 1;
            o->phase = 0;
        }
        return;
    }
    if (o->subtype == 1) {
        int i = (int)o->phase + 3;
        int16_t d = (int8_t)g_vert_speed[i];
        o->y = (int16_t)(o->y - d);
        if (i > 9) {
            o->subtype = 2;
            o->phase = 0;
        }
        return;
    }
    if (o->subtype == 2) {
        int i = (int)o->phase + 0x0A;
        int16_t d = (int8_t)g_vert_speed[i];
        o->y = (int16_t)(o->y + d);
        if (i > 0x10) {
            o->subtype = 3;
            o->phase = 0;
        }
        return;
    }
    if (o->subtype == 3) {
        o->y = (int16_t)(o->y - (int16_t)o->speed);
        if ((int16_t)o->phase > 1) {
            o->subtype = 4;
            o->phase = 0;
        }
        return;
    }
    if (o->subtype == 4) {
        o->y = (int16_t)(o->y + (int16_t)o->speed);
        if ((int16_t)o->phase > 1) {
            o->subtype = 5;
            o->phase = 0;
        }
        return;
    }
    if (o->subtype == 5) {
        if ((int16_t)o->phase > 1) {
            o->subtype = 6;
            o->phase = 0;
        }
        return;
    }
    if (o->subtype == 6) {
        o->subtype = (uint16_t)(o->subtype + 1u);
        o->ready = 1;
        spawn_effect(0x1C, (int16_t)(o->x / 16), (int16_t)(o->y / 16));
        score_add(0x1388, 0);
    }
}

/* 0x69D3 (entry 0x26373): pickup sprite rise/fall and cell restore. */
void h_pickup_rise(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)x; (void)y;
    if ((int16_t)o->phase >= 0 && (int16_t)o->phase <= 3) {
        o->y = (int16_t)(o->y - (int16_t)o->speed);
    } else if ((int16_t)o->phase >= 4 && (int16_t)o->phase <= 7) {
        o->y = (int16_t)(o->y + (int16_t)o->speed);
    } else if ((int16_t)o->phase == 8) {
        o->ready = 1;
        cell_word_put((int)o->x / 16, (int)o->y / 16, CELL_W3, o->anim_shape_b);
        mark_redraw(o->x, o->y);
        if (idx + 1 < OBJ_MAX) g_state.obj[idx + 1].state = 0;
    }
    o->phase = (uint16_t)(o->phase + 1u);
}

uint32_t g_unknown_handler_hits = 0;

/* 0x1F9A:0x298A (kind 0x05): two-phase walker. */
void h_walk_two_phase(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    anim_next(o);
    if (cell_test_200fe(x, y))
        o->vx = (int16_t)(-o->vx);
    if ((int16_t)o->subtype < (int16_t)(o->phase + 0xa)) {
        anim_select_shape(o);
        o->anim_last = 0x10;
        o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    } else if ((int16_t)o->subtype < (int16_t)(o->phase + 0x19)) {
        o->shape = 0x11;
        o->anim_last = 6;
    } else {
        o->subtype = 0;
        o->anim_frame = 1;
    }
    o->subtype = (uint16_t)(o->subtype + 1u);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x403A (kind 0x1E): proximity player shover. */
void h_player_shover(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int16_t dir;
    dir = proximity_test(1, (int16_t)(g_state.g_50dc << 4), x, y);
    g_state.g_5126 = 0;
    if (dir != 0) {
        push_player(5, dir);
        g_state.g_5118 = 5;
        goto common;
    }
    dir = proximity_test(1, (int16_t)(g_state.g_50de << 4), x, y);
    if (dir != 0) {
        if ((int16_t)o->vx == dir) {
            push_player(3, dir);
            g_state.g_5118 = 3;
        }
        goto common;
    }
    dir = proximity_test(1, (int16_t)(g_state.g_50e0 << 4), x, y);
    if (dir != 0 && (int16_t)o->vx == dir) {
        push_player(1, dir);
        g_state.g_5118 = 1;
    }
common:
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
    anim_next(o);
    if (idx + 1 < OBJ_MAX) anim_next(&g_state.obj[idx + 1]);
}

/* 0x1F9A:0x55EC (kind 0x36): paired horizontal mover. */
void h_move_h_paired(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *child;
    int16_t dir_p, dir_c;
    int spd;
    if (idx + 1 >= OBJ_MAX) return;
    child = &g_state.obj[idx + 1];
    dir_p = proximity_obj_dir(1, 0x20, 0x130, idx);
    dir_c = proximity_obj_dir(1, 0x20, 0x130, idx + 1);
    if ((int16_t)o->vx == dir_p || (int16_t)child->vx == dir_c) {
        o->speed = 4;
        child->speed = 4;
        spd = 1;
    } else {
        o->speed = 2;
        child->speed = 2;
        spd = 2;
    }
    o->phase = (uint16_t)(o->phase + 1u);
    if ((int16_t)o->phase % spd == 0) {
        anim_next(o);
        anim_next(child);
    }
    if (cell_test_200fe(x, y)) {
        o->vx = (int16_t)(-o->vx);
        anim_select_shape(o);
        child->vx = (int16_t)(-child->vx);
        anim_select_shape(child);
    }
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    child->x = (int16_t)(child->x + (int32_t)child->vx * child->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
    player_interact(child, (int16_t)child->x, (int16_t)child->y);
}




/* 0x1F9A:0x3A2E (kind 0x1C): proximity lever with animation phases. */
void h_lever_anim(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    int16_t dir;
    if (o->state == 1) o->state = 3;
    if ((int16_t)o->vx == 1)
        dir = proximity_test(1, 0x30, (int16_t)(o->x - 0x10), (int16_t)o->y);
    else
        dir = proximity_test(1, 0x30, (int16_t)(o->x + 0x10), (int16_t)o->y);
    if (((int16_t)o->vx == 1 && dir == 1) ||
        ((int16_t)o->vx == -1 && dir == -1) || o->phase != 0) {
        o->phase = (uint16_t)(o->phase + 1u);
        o->state = 0;
        if ((int16_t)o->phase >= 0x1e) {
            o->phase = 0;
        } else if ((int16_t)o->anim_frame < (int16_t)o->anim_last) {
            o->anim_frame = (uint16_t)(o->anim_frame + 1u);
        }
        player_interact(o, (int16_t)o->x, (int16_t)o->y);
        return;
    }
    if ((int16_t)o->anim_frame > 1) {
        o->anim_frame = (uint16_t)(o->anim_frame - 1u);
    } else {
        o->state = 1;
    }
}

/* 0x1F9A:0x3F6C (dynamic): proximity player shover (variant). */
void h_player_shover_alt(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    int16_t dir;
    g_state.g_5126 = 0;
    g_state.g_5118 = 0;
    dir = proximity_test(1, (int16_t)(g_state.g_50dc << 4), x, y);
    if (dir != 0) {
        push_player(5, dir);
        goto common;
    }
    dir = proximity_test(1, (int16_t)(g_state.g_50de << 4), x, y);
    if (dir != 0) {
        push_player(3, dir);
        goto common;
    }
    dir = proximity_test(1, (int16_t)(g_state.g_50e0 << 4), x, y);
    if (dir != 0) push_player(1, dir);
common:
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
    anim_next(o);
}

/* 0x1F9A:0x2E25 (kind 0x16): camera-relative spawner/mover. */
void h_spawn_cam_rel(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx; (void)x; (void)y;
    if (o->phase == 0) {
        int32_t v = (int32_t)g_state.cam_x + (int32_t)cc_rng_next(0x140);
        v = tp_long_div(v, 16);
        v = tp_long_mul(v, 16);
        o->x = (int16_t)v;
        o->y = g_state.cam_y;
        o->x_prev = o->x;
        o->y_prev = o->y;
    }
    o->phase = (uint16_t)(o->phase + 1u);
    if ((int16_t)o->y > 0x174) {
        o->ready = 1;
        o->phase = 0;
    }
    if ((int16_t)g_state.player_y < 0x64 && (int16_t)o->phase <= 1) {
        o->ready = 1;
        o->phase = 0;
        return;
    }
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x2F83 (kind 0x1A): paired vertical mover/trigger. */
void h_move_v_paired(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *child;
    if (idx + 1 >= OBJ_MAX) return;
    child = &g_state.obj[idx + 1];
    if (o->state == 1) o->state = 4;
    if (o->state == 3)
        o->vy = (uint16_t)proximity_test(2, 0x50, (int16_t)(x + 0xf), y);
    if ((int16_t)o->vy != 1) return;
    child->vy = 1;
    child->state = 0;
    o->state = 0;
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    child->y = (int16_t)(child->y + (int32_t)child->vy * child->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
    player_interact(child, (int16_t)child->x, (int16_t)child->y);
    if (cell_byte_cur((o->x + 2) / 16, (o->y + 0x11) / 16, CELL_FLAG_SOLID) != 0 ||
        cell_byte_cur((o->x + 2) / 16, (o->y + 0x11) / 16, CELL_FLAG_EXIT_LO) != 0) {
        cell_put((o->x + 2) / 16, (o->y + 8) / 16, CELL_FLAG_EXIT_LO, 1);
        cell_put((child->x + 2) / 16, (child->y + 8) / 16, CELL_FLAG_EXIT_LO, 1);
        play_sound(SND_MULTIPART_MOVE);
        o->state = 1;
        child->state = 1;
        o->vy = 0;
        o->vy = 0;
    }
}

/* 0x1F9A:0x4AD3 (kind 0x35): left/right animated walker. */
void h_walk_anim_lr(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    if (cell_test_200fe(x, y)) o->vx = (int16_t)(-o->vx);
    o->subtype = (uint16_t)(o->subtype + 1u);
    if ((int16_t)o->subtype > 0x46) {
        o->subtype = 0;
        if (cc_rng_next(100) > 0x46) {
            o->vx = (int16_t)(-o->vx);
            anim_select_shape(o);
        }
    }
    if ((int16_t)o->vx == 1) {
        anim_next(o);
    } else {
        o->anim_frame = (uint16_t)(o->anim_frame - 1u);
        if ((int16_t)o->anim_frame < 1) o->anim_frame = o->anim_last;
    }
    o->speed = 4;
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x36B0 (kind 0x16): four-record vertical formation mover. */
void h_move_v_formation(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    int k;
    if (idx + 3 >= OBJ_MAX) return;
    g_state.obj[idx + 1].state = 0xFFFF;
    g_state.obj[idx + 2].state = 0xFFFF;
    g_state.obj[idx + 3].state = 0xFFFF;
    if (cell_byte_cur((x + 0x10) / 16, y / 16, CELL_FLAG_SOLID) ||
        cell_byte_cur((x + 0x10) / 16, (y + 0x1b) / 16, CELL_FLAG_SOLID)) {
        o->vy = (int16_t)(-o->vy);
        if ((int16_t)o->vy == -1) {
            o->speed = 2;
            o->phase = 1;
            o->subtype = 0;
            play_sound(SND_MULTIPART_MOVE);
        }
    }
    if (o->phase == 0) {
        for (k = 0; k <= 3; k++)
            g_state.obj[idx + k].y = (int16_t)(g_state.obj[idx + k].y +
                                          (int32_t)o->vy * o->speed);
    } else {
        o->phase = (uint16_t)(o->phase + 1u);
        o->subtype = (uint16_t)(o->subtype + 1u);
        if ((int16_t)o->phase > 0x14) o->phase = 0;
        if ((int16_t)o->subtype == 1) {
            for (k = 0; k <= 3; k++)
                g_state.obj[idx + k].y = (int16_t)(g_state.obj[idx + k].y - 3);
        } else if ((int16_t)o->subtype == 3) {
            for (k = 0; k <= 3; k++)
                g_state.obj[idx + k].y = (int16_t)(g_state.obj[idx + k].y + 3);
            play_sound(SND_MULTIPART_MOVE);
        }
    }
    for (k = 0; k <= 3; k++)
        player_interact(o, (int16_t)g_state.obj[idx + k].x,
                        (int16_t)g_state.obj[idx + k].y);
}

/* 0x1F9A:0x4EEB (dynamic): simple companion walker. */
void h_walk_companion(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    anim_next(o);
    if (cell_byte_cur((x + 3) / 16, (y + 8) / 16, CELL_FLAG_SOLID) ||
        cell_byte_cur((x + 0xc) / 16, (y + 8) / 16, CELL_FLAG_SOLID))
        o->vx = (int16_t)(-o->vx);
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x52E7 (kind 0x37): projectile/phase state machine. */
void h_projectile(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    int16_t dir;
    if (o->phase == 1) {
        o->subtype = (uint16_t)(o->subtype + 1u);
        if ((int16_t)o->subtype > 0x2d) {
            o->ready = 1;
            o->phase = 0;
            o->subtype = 0;
        }
    }
    if (o->phase == 0 &&
        (cell_byte_cur((x + 8) / 16, (y + 3) / 16, CELL_FLAG_SOLID) ||
         cell_byte_cur((x + 8) / 16, (y + 0xc) / 16, CELL_FLAG_SOLID))) {
        play_sound(SND_MULTIPART_MOVE);
        if (cc_rng_next(1000) < 0x64) {
            o->shape = 0x2e;
            dir = proximity_obj_dir(1, 0x20, 0x130, idx);
            o->vx = (dir == 0) ? (uint16_t)-1 : (uint16_t)dir;
            o->vy = 0;
            o->y = (int16_t)(((int16_t)(o->y + 6) / 16) << 4);
            o->kind = 0x38;
            o->anim_last = 5;
            o->speed = 2;
            o->hit_points = 1;
            o->phase = 1;
            o->handler = HDL_WALK_COMPANION;
            o->handler_seg = 0x1F9A;
            mark_redraw((int16_t)o->x, (int16_t)(o->y + 0x10));
        } else {
            o->phase = 1;
            o->y = (int16_t)(((int16_t)(o->y + 6) / 16) << 4);
            o->shape = 1;
        }
    }
    if (o->phase == 1) {
        o->subtype = (uint16_t)(o->subtype + 1u);
        player_interact(o, (int16_t)o->x, (int16_t)(o->y + 0xc));
        return;
    }
    o->y = (int16_t)(o->y + (int32_t)o->vy * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x6232 (kind 0x33): two-frame walker. */
void h_walk_two_frame(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    (void)idx;
    o->phase = (uint16_t)(o->phase + 1u);
    if ((int16_t)o->phase % 2 == 0) anim_next(o);
    if (cell_test_200fe(x, y)) {
        o->vx = (int16_t)(-o->vx);
        anim_select_shape(o);
    }
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}

/* 0x1F9A:0x4C92 (kind 0x37): spawner that converts a companion to 0x52E7. */
void h_spawn_projectile(ObjectEntry *o, int idx, int16_t x, int16_t y) {
    ObjectEntry *child;
    anim_next(o);
    if (proximity_test(2, 0x70, x, y) == 1 && idx > 0) {
        child = &g_state.obj[idx - 1];
        if (child->ready != 0) {
            play_sound(SND_COMPANION);
            child->vx = 0;
            child->vy = 1;
            child->kind = 0x37;
            child->anim_last = 1;
            child->anim_frame = 1;
            child->speed = 6;
            child->hit_points = 0x7D00;
            child->handler = HDL_PROJECTILE;
            child->handler_seg = 0x1F9A;
            child->ready = 0;
            child->state = 0;
            child->x = (int16_t)(((int16_t)(o->x + 8) / 16) << 4);
            child->y = o->y;
            child->phase = 0;
            child->subtype = 0;
            child->shape = 0x27;
        }
    }
    if (cell_byte_cur((x + 3) / 16, (y + 8) / 16, CELL_FLAG_SOLID) ||
        cell_byte_cur((x + 0xc) / 16, (y + 8) / 16, CELL_FLAG_SOLID))
        o->vx = (int16_t)(-o->vx);
    o->x = (int16_t)(o->x + (int32_t)o->vx * o->speed);
    player_interact(o, (int16_t)o->x, (int16_t)o->y);
}
