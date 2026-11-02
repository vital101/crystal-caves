/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"


/* ---------------------------------------------------------------- helpers */

/* 0x1FCB0 anim_next(index): anim_frame++; if (anim_frame > anim_last) anim_frame = 1; */
void anim_next(ObjectEntry *o) {
    o->anim_frame = (uint16_t)(o->anim_frame + 1);
    if (o->anim_frame > o->anim_last) o->anim_frame = 1;
}

/* 0x1FCF7 anim_select_shape(index): p1e == 1 ? anim_shape_b : anim_shape_a -> shape. */
void anim_select_shape(ObjectEntry *o) {
    o->shape = (o->vx == 1) ? o->anim_shape_b : o->anim_shape_a;
}

/* 0x1FC73 mark_redraw(x, y): the original queues a cell redraw and calls the
 * video blitters.  Runtime rendering only needs the cell coordinates; keep
 * the call traceable and let the renderer redraw the affected cell. */
void mark_redraw(int16_t x, int16_t y) {
    (void)x;
    (void)y;
}

/* 0x0F38 cull(margin, index, x, y): visible iff object pixel (x,y) lies inside
 * [cam_x - margin, cam_x + 0x140 + margin] x [cam_y - margin, cam_y + 0xC8 + margin].
 * The update loop always passes margin 0x20. */
int visible_obj(int margin, int32_t x, int32_t y) {
    int32_t left   = g_state.cam_x - margin;
    int32_t right  = g_state.cam_x + 0x140 + margin;
    int32_t top    = g_state.cam_y - margin;
    int32_t bottom = g_state.cam_y + 0x00C8 + margin;
    return (x >= left && x <= right && y >= top && y <= bottom) ? 1 : 0;
}

/* 0x1F9A:0x177B proximity helper.
 * mode=1: same x as player, |y - player_y| < radius -> returns direction
 * mode=2: |x - player_x| < 8 and |y - player_y| < radius -> returns direction
 * Returns: -1 (above/left), 0 (none), 1 (below/right). */
int16_t proximity_test(int mode, int16_t radius,
                              int16_t obj_x, int16_t obj_y) {
    int16_t dx, dy, adx, ady;

    if (mode == 1) {
        /* 0x2177B mode 1: same y, x within radius; direction by x. */
        dy = (int16_t)(obj_y - g_state.player_y);
        if (dy != 0) return 0;
        dx = (int16_t)(obj_x - g_state.player_x);
        adx = dx < 0 ? (int16_t)-dx : dx;
        if (adx >= radius) return 0;
        return (dx > 0) ? -1 : 1;
    } else if (mode == 2) {
        dx = (int16_t)(obj_x - g_state.player_x);
        adx = dx < 0 ? (int16_t)-dx : dx;
        if (adx >= 8) return 0;
        dy = (int16_t)(obj_y - g_state.player_y);
        ady = dy < 0 ? (int16_t)-dy : dy;
        if (ady >= radius) return 0;
        return (dy > 0) ? -1 : 1;
    }
    return 0;
}

/* 0x1F9A:0x1DDB proximity helper mode 2 (used by 1FC2):
 * |x-player_x| < 8 and |y-player_y| < radius; returns 1 when the object is
 * at or above the player, -1 when below, 0 when out of range. */
int16_t proximity_mode2(int16_t x, int16_t y, int16_t radius) {
    int16_t d;
    d = (int16_t)(x - g_state.player_x);
    if (d < 0) d = (int16_t)-d;
    if (d >= 8) return 0;
    d = (int16_t)(y - g_state.player_y);
    if (d < 0) d = (int16_t)-d;
    if (d >= radius) return 0;
    return ((int16_t)(y - g_state.player_y) > 0) ? -1 : 1;
}

/* The raw attempt log + host callback (cc_sound_note) moved to wasm/sound.c
 * with the rest of the sound state. */

/* 0x1F9A:0x21F play_sound_or_effect(id).  All logging and driver selection
 * live in cc_sound_play()/cc_sound_note() so shell-triggered sounds use the
 * same path. */
void play_sound(uint16_t id) {
    cc_sound_play(id);
}

/* ---------------------------------------------------------------- input
 * cc_input_set(flag, pressed) - set a directional input flag.
 * Callers map physical keys to [0x338E]=left, [0x338F]=right,
 * [0x338B]=jump, [0x3390]=fire.  [0x338A] is joystick fire. */
void cc_input_set(uint16_t flag, int pressed) {
    uint8_t v = (uint8_t)(pressed ? 1 : 0);
    switch (flag) {
        case 0x338E: g_state.input_left = v; break;
        case 0x338F: g_state.input_right = v; break;
        case 0x338B: g_state.input_jump = v; break;
        case 0x338C: g_state.input_down = v; break;
        case 0x3390: g_state.input_fire = v; break;      /* keyboard fire */
        case 0x338A: g_state.input_joystick_fire = v; break; /* joystick fire */
        default: break;
    }
}

/* Apply the keyboard-ISR side effects once, after a whole input frame has
 * been latched.  Doing this in cc_input_set() resets the walk phase on the
 * intermediate left=0/right=0 state when the player switches direction. */
void input_commit(void) {
    if (!g_state.input_left && !g_state.input_right) g_state.walk_phase = 0;
    if (g_state.input_left || g_state.input_right) g_state.anim_suppress = 0;
}

/* ---------------------------------------------------------------- cell collision
 * 0x1F9A:0x43 collision helper (load 0x1F9E3).
 * Tests 9 player-hitbox corners against cell byte 0x11 (solid flag):
 *   (x+3/8/12 + xoff) x (y+0/8/15 + yoff)
 * Returns 1 if ANY corner is solid (blocked), 0 if all clear.
 * Cell layout is column-major: cell = (col+1)*500 + (row+1)*20, cell size 16px.
 */
int cc_cell_collide(int level, int32_t px, int32_t py,
                           int16_t xoff, int16_t yoff) {
    static const int8_t dx[3] = { 3, 8, 12 };
    (void)level;
    static const int8_t dy[3] = { 0, 8, 15 };
    int ci, cj;
    for (ci = 0; ci < 3; ci++) {
        int32_t cx = (px + dx[ci] + xoff) / 16;
        for (cj = 0; cj < 3; cj++) {
            int32_t cy = (py + dy[cj] + yoff) / 16;
            if (cell_solid_cur((int)cx, (int)cy)) return 1;
        }
    }
    return 0;
}

/* 0x1F9E3: returns 1 when the 9-point player box is clear at (xoff,yoff),
 * 0 when blocked.  A blocked move also clears [0xa0b4]. */
int player_clear(int16_t xoff, int16_t yoff) {
    if (cc_cell_collide(g_state.cur_level, g_state.player_x, g_state.player_y, xoff, yoff)) {
        g_state.walk_phase = 0;
        return 0;
    }
    return 1;
}

/* Pixel -> cell helpers used by the vertical routine.  The original uses
 * `idiv 16` for signed values in the reversed-gravity/special paths and
 * logical shifts for the normal down path; all in-range values agree. */
int cell_solid_px(int16_t x, int16_t y) {
    int col = (int)x / 16;
    int row = (int)y / 16;
    return cell_solid_cur(col, row);
}

int cell_special_px(int16_t x, int16_t y) {
    int col = (int)x / 16;
    int row = (int)y / 16;
    if (col < 0 || col >= CELL_COLS || row < 0 || row >= CELL_ROWS) return 0;
    if (g_cells_asset) return g_state.cells[row][col][18] != 0;
    return 0;
}

/* 0x1F9A:0x310 anim_next(1): advance the player object animation frame. */
void player_anim_next(void) {
    g_state.anim_timer = (uint16_t)(g_state.anim_timer + 1u);
    if (g_state.anim_timer > g_state.obj[1].anim_last) g_state.anim_timer = 1;
}

/* 0x1F9A:0x357 anim_select_shape(1): p1e==1 ? anim_shape_b : anim_shape_a -> shape. */
void player_select_shape(void) {
    g_state.shape_player = (g_state.move_dir == 1) ? g_state.shape_a : g_state.shape_b;
}

/* Mirror aliased player globals into object slot 1 after a full frame. */
void sync_player_object(void) {
    ObjectEntry *p = &g_state.obj[1];
    p->x = g_state.player_x;
    p->y = g_state.player_y;
    p->shape = g_state.shape_player;
    p->vx = (int16_t)g_state.move_dir;
    p->block = g_state.player_block;
    p->anim_frame = g_state.anim_timer;
    p->anim_shape_a = g_state.shape_b;
    p->anim_shape_b = g_state.shape_a;
}

/* 0x6BE: walk acceleration phase -> speed 2 for phases 1-2, 4 afterwards. */
void update_walk_speed(void) {
    g_state.walk_phase = (uint16_t)(g_state.walk_phase + 1u);
    if (g_state.walk_phase == 1 || g_state.walk_phase == 2) g_state.walk_speed = 2;
    else if (g_state.walk_phase >= 3 && g_state.walk_phase <= 0x7D00) g_state.walk_speed = 4;
}

/* 0xDCB9: falling/gravity half of the vertical routine. */
void cc_vertical(void) {
    int16_t saved_y = g_state.player_y;
    int16_t delta;
    int move;

    g_state.jump_phase = (uint16_t)(g_state.jump_phase + 1u);
    if ((int16_t)g_state.jump_phase > 0x13) g_state.jump_phase = 0x13;
    g_state.c8 = (uint16_t)(g_state.c8 + 1u);
    g_state.airborne = 1;

    move = 0;
    if (g_state.riding_vertical && (int16_t)g_state.jump_phase < 0xB) move = 1;
    if (g_state.riding_horizontal && (int16_t)g_state.jump_phase < 0xB) move = 1;
    if (!g_state.riding_horizontal && !g_state.riding_vertical && (int16_t)g_state.jump_phase > 0) move = 1;
    if (move) {
        delta = wmul(g_vert_speed[g_state.jump_phase], g_state.gravity_scale);
        g_state.player_y = wadd(g_state.player_y, delta);
    }

    if (g_state.riding_vertical || g_state.riding_horizontal) g_state.airborne = 0;

    if (g_state.gravity_scale == 1) {
        int blocked = cell_solid_px((int16_t)(g_state.player_x + 3),
                                    (int16_t)(g_state.player_y + 0x10));
        if (!blocked)
            blocked = cell_solid_px((int16_t)(g_state.player_x + 0xC),
                                    (int16_t)(g_state.player_y + 0x10));
        if (blocked) {
            if (!g_state.riding_vertical && !g_state.riding_horizontal)
                g_state.player_y = (int16_t)(((int)g_state.player_y / 16) * 16);
            g_state.airborne = 0;
        }

        {
            int special = cell_special_px((int16_t)(g_state.player_x + 3),
                                          (int16_t)(g_state.player_y + 0x10));
            if (!special)
                special = cell_special_px((int16_t)(g_state.player_x + 0xC),
                                          (int16_t)(g_state.player_y + 0x10));
            if (special) {
                int16_t dx = (int16_t)((((int)g_state.player_y + 0x10) / 16) * 16 + 2);
                if (wadd(saved_y, 0x10) <= dx) {
                    g_state.player_y = (int16_t)(((int)g_state.player_y / 16) * 16);
                    g_state.airborne = 0;
                }
            }
        }
    } else if (g_state.gravity_scale == -1) {
        int blocked = cell_solid_px((int16_t)(g_state.player_x + 3),
                                    (int16_t)(g_state.player_y - 1));
        if (!blocked)
            blocked = cell_solid_px((int16_t)(g_state.player_x + 0xC),
                                    (int16_t)(g_state.player_y - 1));
        if (blocked) {
            g_state.player_y = (int16_t)((((int)g_state.player_y + 0xC) / 16) * 16);
            g_state.airborne = 0;
        }
    }

    /* 0xDED4: idle animation shape. */
    if ((int16_t)g_state.impulse <= 0 && !g_state.anim_suppress) {
        g_state.shape_player = (g_state.move_dir == 1) ? 0x19 : 0x1A;
    }

    /* 0xDEF7: camera-y follow using the same displacement profile. */
    if (g_state.gravity_scale == 1) {
        if (wadd(g_state.cam_y, 0x78) < g_state.player_y) {
            int16_t nd = wadd(g_state.cam_y, wmul(g_vert_speed[g_state.jump_phase],
                                             g_state.gravity_scale));
            if (nd >= 0xC8) g_state.cam_y = 0xC8;
            else g_state.cam_y = nd;
        }
    } else if (g_state.gravity_scale == -1) {
        if (wsub(g_state.player_y, 0x50) < g_state.cam_y) {
            int16_t t = wsub(g_state.cam_y, g_vert_speed[g_state.jump_phase]);
            if (t <= 0x10) g_state.cam_y = 0x10;
            else g_state.cam_y = wadd(g_state.cam_y, wmul(
                g_vert_speed[g_state.jump_phase], g_state.gravity_scale));
        }
    }

    if (!g_state.airborne) {
        g_state.c8 = 0;
        if ((int16_t)g_state.impulse <= 0 && !g_state.anim_suppress)
            player_select_shape();
    }

    /* 0xDFA2: first airborne frame aligns y to the 8px animation grid. */
    if (g_state.gravity_scale != -1 && g_state.c8 == 1) {
        int16_t rem = (int16_t)(g_state.player_y % 8);
        if (rem < 7) g_state.player_y = wsub(g_state.player_y, rem);
        else g_state.player_y = wadd(g_state.player_y, rem);
    }

    if (!g_state.airborne) {
        g_state.c8 = 0;
        if ((int16_t)g_state.impulse <= 0 && !g_state.anim_suppress)
            player_select_shape();
    }

    /* 0xDFF6: airborne disables platform attachment latches. */
    if (g_state.airborne) {
        g_state.anim_timer = 1;
        g_state.riding_horizontal = 0;
        g_state.riding_vertical = 0;
    }
}

/* 0xE36B: jump start + ascending half; falls through to 0xDCB9. */
void cc_jump_vertical(void) {
    uint16_t saved_walk = g_state.walk_phase;

    if (!g_state.jump_flag && !g_state.airborne && g_state.input_jump) {
        int16_t up = wmul(g_state.gravity_scale, -4);
        if (player_clear(0, up)) {
            g_state.anim_suppress = 0;
            g_state.jump_flag = 1;
            g_state.c6 = 0;
            g_state.jump_phase = 0;
            g_state.riding_horizontal = 0;
            g_state.riding_vertical = 0;
            play_sound(SND_JUMP);
        } else {
            if (!g_state.g_a0b8) {
                g_state.g_a0fc = 1;
                g_state.g_a0b8 = 1;
            }
            g_state.walk_phase = saved_walk;
        }
    }

    if (!g_state.jump_flag) {
        cc_vertical();
        return;
    }

    g_state.anim_timer = 1;
    if (!g_state.anim_suppress) {
        g_state.shape_player = (g_state.move_dir == 1) ? 0x19 : 0x1A;
    }

    g_state.jump_phase = (uint16_t)(g_state.jump_phase + 1u);
    if (g_state.jump_phase == 0xA) {
        g_state.jump_flag = 0;
        g_state.jump_phase = 9;
    }
    if ((int16_t)g_state.jump_phase >= 0x14) {
        if (!g_state.jump_flag) cc_vertical();
        return;
    }

    {
        int16_t d = wmul(g_vert_speed[g_state.jump_phase], g_state.gravity_scale);
        g_state.player_y = wsub(g_state.player_y, d);

        if (g_state.gravity_scale == 1) {
            if (player_clear(0, wneg(d))) {
                if (g_state.cam_y > 0x10 && wadd(g_state.cam_y, 0x50) > g_state.player_y) {
                    int16_t ny = wsub(g_state.cam_y, wmul(
                        g_vert_speed[g_state.jump_phase], g_state.gravity_scale));
                    g_state.cam_y = (ny < 0x10) ? 0x10 : ny;
                }
            } else {
                g_state.jump_phase = 9;
                g_state.jump_flag = 0;
            }
        } else if (g_state.gravity_scale == -1) {
            if (player_clear(0, g_vert_speed[g_state.jump_phase])) {
                if (g_state.cam_y < 0xC8 && wadd(g_state.cam_y, 0x78) < g_state.player_y) {
                    int16_t ny = wadd(g_state.cam_y,
                                      g_vert_speed[g_state.jump_phase]);
                    g_state.cam_y = (ny > 0xC8) ? 0xC8 : ny;
                }
            } else {
                g_state.jump_phase = 9;
                g_state.jump_flag = 0;
            }
        }
    }

    if (!g_state.jump_flag) cc_vertical();
}

/* 0xE011 impulse slide (knockback): move player/camera by the decaying
 * impulse, halving it when the way is blocked.  Returns 1 when consumed. */
static int impulse_move(void) {
    if ((int16_t)g_state.impulse <= 0)
        return 0;
    if (g_state.move_dir == 1) {
        int16_t off = wadd(wneg(g_state.impulse), g_state.scroll_x_accum);
        if (player_clear(off, 0)) {
            if (g_state.cam_x > 0 && wadd(g_state.cam_x, 0x96) > g_state.player_x) {
                int16_t nx = wsub(g_state.cam_x,
                                  wadd(g_state.impulse, g_state.scroll_x_accum));
                g_state.cam_x = (nx < 0) ? 0 : nx;
            }
            g_state.player_x = wsub(g_state.player_x, g_state.impulse);
        } else {
            g_state.impulse = ((int16_t)g_state.impulse > 1)
                         ? (uint16_t)((int16_t)g_state.impulse / 2) : 0;
        }
    } else {
        int16_t off = wsub(g_state.impulse, g_state.scroll_x_accum);
        if (player_clear(off, 0)) {
            if (g_state.cam_x < 0x140 && wadd(g_state.cam_x, 0xAA) < g_state.player_x) {
                int16_t nx = wadd(g_state.cam_x,
                                  wsub(g_state.impulse, g_state.scroll_x_accum));
                g_state.cam_x = (nx > 0x140) ? 0x140 : nx;
            }
            g_state.player_x = wadd(g_state.player_x, g_state.impulse);
        } else {
            g_state.impulse = ((int16_t)g_state.impulse > 1)
                         ? (uint16_t)((int16_t)g_state.impulse / 2) : 0;
        }
    }
    if ((int16_t)g_state.impulse > 0) g_state.impulse--;
    return 1;
}

/* 0xE2FD walking: facing, walk animation and camera-x for one frame. */
static void walk_move(int right, int left) {
    if (right) {
        g_state.move_dir = 1;
        if (!g_state.jump_flag && !g_state.airborne) g_state.shape_player = g_state.shape_a;
        update_walk_speed();
        {
            int16_t off = wsub(g_state.walk_speed, g_state.scroll_x_accum);
            if (player_clear(off, 0)) {
                if (g_state.cam_x < 0x140 && wadd(g_state.cam_x, 0xAA) < g_state.player_x) {
                    int16_t nx = wadd(g_state.cam_x, wsub(g_state.walk_speed,
                                                     g_state.scroll_x_accum));
                    g_state.cam_x = (nx > 0x140) ? 0x140 : nx;
                }
                g_state.player_x = wadd(g_state.player_x, g_state.walk_speed);
                player_anim_next();
            }
        }
    }

    if (left) {
        g_state.move_dir = (uint16_t)-1;
        if (!g_state.jump_flag && !g_state.airborne) g_state.shape_player = g_state.shape_b;
        update_walk_speed();
        {
            int16_t off = wadd(wneg(g_state.walk_speed), g_state.scroll_x_accum);
            if (player_clear(off, 0)) {
                if (g_state.cam_x > 0 && wadd(g_state.cam_x, 0x96) > g_state.player_x) {
                    int16_t nx = wsub(g_state.cam_x,
                                      wadd(g_state.walk_speed, g_state.scroll_x_accum));
                    g_state.cam_x = (nx < 0) ? 0 : nx;
                }
                g_state.player_x = wsub(g_state.player_x, g_state.walk_speed);
                player_anim_next();
            }
        }
    }

    if (!left && !g_state.blocked_l && !right && !g_state.blocked_r)
        g_state.anim_timer = 1;
}

/* Platform carry along x: shift the world under the player, or let the
 * camera follow so the delta can retire. */
static void platform_carry_x(void) {
    if (g_state.carry_x_delta == 0)
        return;
    if ((int16_t)g_state.carry_x_delta < 0) {
        if (player_clear((int16_t)g_state.carry_x_delta, 0)) {
            if (g_state.cam_x > 0 && wadd(g_state.cam_x, 0x96) > g_state.player_x) {
                int16_t nx = wadd(g_state.cam_x, (int16_t)g_state.carry_x_delta);
                g_state.cam_x = (nx < 0) ? 0 : nx;
                g_state.carry_x_delta = 0;
            }
        }
    } else {
        if (player_clear((int16_t)g_state.carry_x_delta, 0)) {
            if (g_state.cam_x < 0x140 && wadd(g_state.cam_x, 0xAA) < g_state.player_x) {
                int16_t nx = wadd(g_state.cam_x, (int16_t)g_state.carry_x_delta);
                g_state.cam_x = (nx > 0x140) ? 0x140 : nx;
                g_state.carry_x_delta = 0;
            }
        }
    }
}

/* Platform carry along y: camera-y follows the platform within its band. */
static void platform_carry_y(void) {
    if (g_state.carry_y_delta == 0)
        return;
    if ((int16_t)g_state.carry_y_delta < 0) {
        if (player_clear(0, (int16_t)g_state.carry_y_delta)) {
            if (g_state.cam_y > 0x10 && wadd(g_state.cam_y, 0x50) > g_state.player_y) {
                int16_t ny = wadd(g_state.cam_y, (int16_t)g_state.carry_y_delta);
                g_state.cam_y = (ny < 0x10) ? 0x10 : ny;
            }
        }
    } else {
        if (wadd(g_state.cam_y, 0x78) < g_state.player_y) {
            if (g_state.cam_y < 0xC8) g_state.cam_y = wadd(g_state.cam_y,
                                                 (int16_t)g_state.carry_y_delta);
            else g_state.cam_y = 0xC8;
        }
    }
}

/* 0xE011 + 0xE2FD: horizontal movement, camera-x, platform carry, camera-y. */
void cc_horizontal(void) {
    const int16_t right = (g_state.input_right || g_state.blocked_r) ? 1 : 0;
    const int16_t left = (g_state.input_left || g_state.blocked_l) ? 1 : 0;

    if (!impulse_move())
        walk_move(right, left);

    platform_carry_x();
    platform_carry_y();

    /* Mirror the physics position into the player object record so the
     * renderer draws the player at the correct place. */
    g_state.obj[1].x = g_state.player_x;
    g_state.obj[1].y = g_state.player_y;
}

