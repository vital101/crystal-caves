/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"

/* Apply the exact 0xC3E new-game defaults captured in startup_data.h. */
void apply_startup_state(void) {
    g_state.blocked_l = g_startup.f_a0f1;
    g_state.blocked_r = g_startup.f_a0f2;
    g_state.g_a0f6 = g_startup.f_a0f6;
    g_state.a0f8 = g_startup.f_a0f8;
    g_state.g_a0fc = g_startup.f_a0fc;
    g_state.airborne = g_startup.f_a0fd;
    g_state.g_a0ea = g_startup.f_a0ea;
    g_state.walk_phase = g_startup.f_a0b4;
    g_state.g_a0b8 = g_startup.f_a0b8;
    g_state.walk_speed = g_startup.f_2ba;
    g_state.impulse = g_startup.f_2ca;
    g_state.c8 = g_startup.f_2c8;
    g_state.c6 = g_startup.f_2c6;
    g_state.jump_flag = g_startup.f_306;
    g_state.anim_suppress = g_startup.f_30a;
    g_state.status_30b = g_startup.f_30b;
    g_state.paused_30c = g_startup.f_30c;
    g_state.anim_timer = g_startup.f_461b;
    g_state.shape_b = g_startup.f_461d;
    g_state.shape_a = g_startup.f_461f;
    g_state.player_block = g_startup.f_4619;
    g_state.shape_player = g_startup.f_4627;
    g_state.move_dir = g_startup.f_4629;
    g_state.g_5118 = g_startup.f_5118;
    g_state.g_5126 = g_startup.f_5126;
    g_state.g_5128 = g_startup.f_5128;
    g_state.g_5124 = g_startup.f_5124;
    g_state.cam_x = (int16_t)g_startup.f_512e;
    g_state.cam_y = (int16_t)g_startup.f_5130;
    g_state.gravity_scale = (int16_t)g_startup.f_5132;
    g_state.jump_phase = g_startup.f_5134;
    g_state.g_513c = g_startup.f_513c;
    g_state.g_513d = g_startup.f_513d;
    g_state.g_5140 = g_startup.f_5140;
    g_state.g_5142 = g_startup.f_5142;
    g_state.g_509c = g_startup.f_509c;
    g_state.g_509e = g_startup.f_509e;

    g_state.score_lo = g_startup.f_50a5;
    g_state.score_hi = g_startup.f_50a7;
    g_state.rng_lo = g_startup.f_2a6;
    g_state.rng_hi = g_startup.f_2a8;
    g_state.cooldown = g_startup.f_2e0;
    g_state.g_260 = g_startup.f_260;
    g_state.g_50dc = g_startup.f_50dc;
    g_state.g_50de = g_startup.f_50de;
    g_state.g_50e0 = g_startup.f_50e0;
    g_state.g_a0f0 = g_startup.f_a0f0;
    g_state.g_a0fa = g_startup.f_a0fa;
    g_state.g_2cc = g_startup.f_2cc; g_state.g_2ce = g_startup.f_2ce;
    g_state.g_2d4 = g_startup.f_2d4; g_state.g_2d6 = g_startup.f_2d6;
    g_state.g_2d8 = g_startup.f_2d8; g_state.g_2fa = g_startup.f_2fa;
    g_state.g_2fc = g_startup.f_2fc; g_state.g_2fe = g_startup.f_2fe;
    g_state.fire_impulse = g_startup.f_2da;
    g_state.g_a100 = 0;
    g_state.power_shot = g_startup.f_a0ff;
    g_state.power_timer = g_startup.f_a0c4;
    g_state.checkpoint_50a0 = g_startup.f_50a0;
    g_state.checkpoint_50be = g_startup.f_50be;
    g_state.checkpoint_50c4 = g_startup.f_50c4;
    g_state.checkpoint_50c7 = g_startup.f_50c7;
    g_state.checkpoint_50c9 = g_startup.f_50c9;
    g_state.g_2b5e = g_startup.f_2b5e;
    g_state.g_2b5c = 0;
    g_state.g_50a2 = g_startup.f_50a2;
    g_state.g_a0cc = g_startup.f_a0cc;
    g_state.next_level = (int16_t)g_startup.f_a0be;
    g_state.g_a0f5 = g_startup.f_a0f5;
    g_state.g_a0f6 = g_startup.f_a0f6;
    g_state.g_a0f7 = g_startup.f_a0f7;
    g_state.g_a0f9 = g_startup.f_a0f9;
    g_state.g_a0f3 = g_startup.f_a0f3;
    g_state.g_a0fb = g_startup.f_a0fb;
    g_state.g_a126 = g_startup.f_a126;
    g_state.g_a0e8 = g_startup.f_a0e8;
    g_state.g_a0d0 = g_startup.f_a0d0;
    g_state.g_a0d2 = g_startup.f_a0d2;
    g_state.g_a0d4 = g_startup.f_a0d4;
    g_state.g_a0d6 = g_startup.f_a0d6;
    g_state.g_a0d8 = g_startup.f_a0d8;
    g_state.g_a0da = g_startup.f_a0da;
    g_state.g_a0dc = g_startup.f_a0dc;
    g_state.g_a0de = g_startup.f_a0de;
    g_state.g_a0ce = g_startup.f_a0ce;
    g_state.g_a10b = g_startup.f_a10b;
    g_state.g_a0c2 = g_startup.f_a0c2;
    g_state.g_a0c8 = g_startup.f_a0c8;
    g_state.g_a0ca = g_startup.f_a0ca;
    g_state.g_a0e4 = g_startup.f_a0e4;
    g_state.g_a0c6 = 0;
    g_state.g_a10a = 0;
    g_state.g_a100 = 0;
    g_state.g_a101 = 1;
    g_state.g_a102 = 1;
    g_state.g_a104 = 1;
    g_state.g_a105 = 1;
    g_state.g_a106 = 1;
    g_state.g_a107 = 1;
    g_state.g_a108 = 1;
    g_state.g_a109 = 1;
    g_state.g_a10d = 0;
    g_state.g_a0f4 = 0;
    g_state.g_3395 = 0;
    g_state.g_3396 = 0;
    g_state.cave_flags[0] = 0;
    for (int i = 1; i <= 16; i++) g_state.cave_flags[i] = 1;
    g_state.g_3394 = 0;
    g_state.g_a110 = 0;
    g_state.g_512c = 0;
    cc_sound_log_clear();

    g_state.g_512a = 0;
    g_state.g_513a = 0;
    g_state.g_511a = 0;
    g_state.g_511c = 0;
    g_state.exit_col = 0;
    g_state.exit_row = 0;
    g_state.exit_step = 0;
    g_state.input_left = 0;
    g_state.input_right = 0;
    g_state.input_jump = 0;
    g_state.input_down = 0;
    g_state.input_fire = 0;
    g_state.input_joystick_fire = 0;
}

/* Apply the loader's per-level player/camera/spawn state. */
void apply_level_state(int level, int use_start, int fresh) {
    const cc_level_start_t *st = &g_level_start_state[level];

    if (use_start) {
        /* Initial spawn cell from the Y token / generated level-start data. */
        g_state.player_x = st->f_4611;
        g_state.player_y = st->f_4613;
        g_state.cam_x = st->f_512e;
        g_state.cam_y = st->f_5130;
    } else {
        /* Transition spawn override ([0x2B72]/[0x2B74]).  The loader uses the
         * cell and centres the camera on it (0xB1A9): cam_x = col*16 - 0xA0,
         * cam_y = row*16 - 0x64, clamped to the 320x200 view bounds. */
        g_state.player_x = (int16_t)(g_state.spawn_col_override * 16);
        g_state.player_y = (int16_t)(g_state.spawn_row_override * 16);
        g_state.cam_x = (int16_t)(g_state.player_x - 0xA0);
        g_state.cam_y = (int16_t)(g_state.player_y - 0x64);
        if (g_state.cam_x < 0) g_state.cam_x = 0;
        if (g_state.cam_x > 0x140) g_state.cam_x = 0x140;
        if (g_state.cam_y < 0) g_state.cam_y = 0;
        if (g_state.cam_y > 0xC8) g_state.cam_y = 0xC8;
        /* Do not clear [0x2B72]/[0x2B74]: the loader only reads them, and the
         * cave exit reuses the hub door entry value to place the player next
         * to the door on return. */
    }
    g_state.player_block = st->f_4619;
    g_state.anim_timer = st->f_461b;
    g_state.shape_b = st->f_461d;
    g_state.shape_a = st->f_461f;
    g_state.shape_player = st->f_4627;
    g_state.move_dir = st->f_4629;
    /* Prologue state is reloaded with every level.  RNG is only seeded on a
     * fresh game load; transitions preserve the live LCG state. */
    g_state.fire_impulse = st->f_2da;
    g_state.g_2f2 = st->f_2f2;
    g_state.g_2b8 = st->f_2b8;
    g_state.g_511e = st->f_511e;
    g_state.g_5122 = st->f_5122;
    g_state.g_50dc = st->f_50dc;
    g_state.g_50de = st->f_50de;
    g_state.g_50e0 = st->f_50e0;
    /* The hub transition preserves these two cave-companion flags; every
     * other destination reloads them from its level prologue. */
    if (fresh || level != 2) {
        g_state.g_a0f0 = st->f_a0f0;
        g_state.g_a0fa = st->f_a0fa;
    }
    g_state.g_30d = st->f_30d;
    g_state.status_30b = st->f_30b;
    if (fresh) {
        g_state.g_2cc = st->f_2cc; g_state.g_2ce = st->f_2ce;
        g_state.g_2d4 = st->f_2d4; g_state.g_2d6 = st->f_2d6;
        g_state.g_2d8 = st->f_2d8; g_state.g_2fa = st->f_2fa;
        g_state.g_2fc = st->f_2fc; g_state.g_2fe = st->f_2fe;
    }
    g_state.checkpoint_50a0 = st->f_50a0;
    g_state.g_a0cc = st->f_a0cc;
    g_state.g_50a2 = st->f_50a2;
    g_state.next_level = (int16_t)st->f_a0be;
    g_state.g_a0f5 = st->f_a0f5;
    g_state.g_a0f6 = st->f_a0f6;
    g_state.g_a0f7 = st->f_a0f7;
    g_state.g_a0f9 = st->f_a0f9;
    g_state.g_a0f3 = st->f_a0f3;
    if (fresh) {
        g_state.exit_col = (int16_t)st->f_a0e0;
        g_state.exit_row = (int16_t)st->f_a0e2;
        g_state.exit_step = st->f_a0e6;
    }
    g_state.g_a0e8 = st->f_a0e8;
    g_state.g_a0d0 = st->f_a0d0;
    g_state.g_a0d2 = st->f_a0d2;
    g_state.g_a0d4 = st->f_a0d4;
    g_state.g_a0d6 = st->f_a0d6;
    g_state.g_a0d8 = st->f_a0d8;
    g_state.g_a0da = st->f_a0da;
    g_state.g_a0dc = st->f_a0dc;
    g_state.g_a0de = st->f_a0de;
    g_state.g_a0ce = st->f_a0ce;
    g_state.g_a10b = st->f_a10b;
    g_state.g_a0c2 = st->f_a0c2;
    g_state.g_a0c8 = st->f_a0c8;
    g_state.g_a0ca = st->f_a0ca;
    g_state.g_a0e4 = st->f_a0e4;
    g_state.g_a0c6 = 0;
    g_state.g_a100 = 0;
    g_state.g_a101 = 1;
    g_state.g_a102 = 1;
    g_state.g_a104 = 1;
    g_state.g_a105 = 1;
    g_state.g_a106 = 1;
    g_state.g_a107 = 1;
    g_state.g_a108 = 1;
    g_state.g_a109 = 1;
    g_state.g_a10a = 0;
    g_state.g_a10d = 0;
    g_state.g_a0f4 = 0;
    g_state.g_a0fb = st->f_a0fb;
    g_state.g_a126 = st->f_a126;
    if (fresh) {
        g_state.rng_lo = st->f_2a6;
        g_state.rng_hi = st->f_2a8;
    }
}


/* ------------------------------------------------------- transition overlay
 * Generated by tools/export_level_overlay.py from the real loader.  The
 * destination loader writes only these cell/object bytes; everything else is
 * preserved from the source level.  A few entries are replayed at runtime:
 * RNG-selected spawn fields, map-command shapes that read carried globals,
 * the hub's two RNG decoration recipes, and flag-driven hub door cells.
 */
const unsigned char *g_level_overlay = 0;
void cc_set_level_overlay(const unsigned char *data) { g_level_overlay = data; }

uint16_t ov_rd16(const unsigned char *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
uint32_t ov_rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void ov_obj_byte(int slot, int off, unsigned char value) {
    unsigned char *raw = (unsigned char *)&g_state.obj[slot];
    raw[off] = value;
}

void apply_level_overlay(int level) {
    const unsigned char *base = g_level_overlay;
    const unsigned char *p, *bits, *values;
    const unsigned char *cell_tag_p, *obj_lit_p, *obj_tag_p, *glob_p, *draw_p;
    uint16_t cell_tag_n, obj_lit_n, obj_tag_n, glob_n, draw_n, draw_n_raw;
    uint16_t draws[1024];
    uint16_t vi = 0, i, version;
    struct { uint16_t bit; uint8_t tag; uint16_t payload; } ct[256];
    struct { uint8_t slot, off, tag; uint16_t payload; } ot[256];
    uint16_t ct_n = 0, ot_n = 0;

    if (!base || level < 0 || level >= OBJ_LEVELS) return;
    if (ov_rd32(base) != 0x4F4C4343u) return;  /* "CCLO" */
    version = ov_rd16(base + 4);
    p = base + ov_rd32(base + 8 + level * 4);
    bits = p;
    values = p + 2500;
    for (i = 0; i < 20000; i++) {
        if (bits[i >> 3] & (1u << (i & 7))) {
            unsigned cell = i / 20, boff = i % 20;
            g_state.cells[cell / 40][cell % 40][boff] = values[vi++];
        }
    }
    p = values + vi;
    cell_tag_n = ov_rd16(p); p += 2;
    cell_tag_p = p; p += (unsigned)cell_tag_n * 5;
    for (i = 0; i < cell_tag_n && ct_n < 256; i++) {
        ct[ct_n].bit = ov_rd16(cell_tag_p + i * 5);
        ct[ct_n].tag = cell_tag_p[i * 5 + 2];
        ct[ct_n].payload = ov_rd16(cell_tag_p + i * 5 + 3);
        ct_n++;
    }
    obj_lit_n = ov_rd16(p); p += 2;
    obj_lit_p = p; p += (unsigned)obj_lit_n * 3;
    for (i = 0; i < obj_lit_n; i++)
        ov_obj_byte(obj_lit_p[i * 3], obj_lit_p[i * 3 + 1],
                    obj_lit_p[i * 3 + 2]);
    obj_tag_n = ov_rd16(p); p += 2;
    obj_tag_p = p; p += (unsigned)obj_tag_n * 5;
    for (i = 0; i < obj_tag_n && ot_n < 256; i++) {
        ot[ot_n].slot = obj_tag_p[i * 5];
        ot[ot_n].off = obj_tag_p[i * 5 + 1];
        ot[ot_n].tag = obj_tag_p[i * 5 + 2];
        ot[ot_n].payload = ov_rd16(obj_tag_p + i * 5 + 3);
        ot_n++;
    }
    glob_n = ov_rd16(p); p += 2;
    glob_p = p; p += (unsigned)glob_n * 5;
    for (i = 0; i < glob_n; i++) {
        uint16_t off = ov_rd16(glob_p + i * 5);
        uint8_t size = glob_p[i * 5 + 2];
        uint16_t val = ov_rd16(glob_p + i * 5 + 3);
        (void)size;
        if (off == 0x02A6 || off == 0x02A8) continue;  /* live RNG */
        /* The camera depends on the actual spawn override ([0x2B72]/[0x2B74]);
         * apply_level_state owns it.  The fixture overlay was captured without
         * an override, so replaying its camera would undo a cave->hub spawn. */
        if (off == 0x512E || off == 0x5130) continue;
        if (off == 0xA0E0) g_state.g_2f6 = 0xDEAD; /* DEBUG */
        cc_global_set(off, val);
    }
    draw_n_raw = ov_rd16(p); p += 2;
    draw_p = p;
    draw_n = draw_n_raw;
    if (draw_n > 1024) draw_n = 1024;
    if (version >= 2) {
        const unsigned char *decor_p = draw_p + (unsigned)draw_n_raw * 4;
        uint16_t decor_n = ov_rd16(decor_p);
        decor_p += 2;
        for (i = 0; i < decor_n; i++, decor_p += 6) {
            uint16_t bit = ov_rd16(decor_p);
            uint8_t kind = decor_p[2];
            uint8_t baseval = decor_p[3];
            uint8_t range = decor_p[4];
            uint8_t special = decor_p[5];
            uint16_t r = (uint16_t)cc_rng_next(range);
            unsigned char value;
            if (kind == 0) {
                if (r == 2) r = (uint16_t)cc_rng_next(range);
                value = (unsigned char)(baseval + r);
            } else {
                value = (r == 4) ? special : (unsigned char)(baseval + r);
            }
            g_state.cells[(bit / 20) / 40][(bit / 20) % 40][bit % 20] = value;
        }
        {
            uint16_t door_n = ov_rd16(decor_p);
            decor_p += 2;
            for (i = 0; i < door_n; i++, decor_p += 3) {
                uint16_t bit = ov_rd16(decor_p);
                uint8_t index = decor_p[2];
                unsigned cell = bit / 20;
                unsigned open = cc_global_get(0x50A8 + index) != 0;
                /* Hub door cells store the door number in w0 and use shape
                 * 0x0D when the cave flag is set, 0x10 when it is clear. */
                g_state.cells[cell / 40][cell % 40][bit % 20] = open ? index : 0;
                g_state.cells[cell / 40][cell % 40][6] = open ? 0x0D : 0x10;
            }
        }
    }
    for (i = 0; i < draw_n; i++) {
        uint16_t n = ov_rd16(draw_p + i * 4);
        uint16_t add = ov_rd16(draw_p + i * 4 + 2);
        draws[i] = (uint16_t)(cc_rng_next(n) + add);
    }
    for (i = 0; i < ct_n; i++) {
        unsigned cell = ct[i].bit / 20, boff = ct[i].bit % 20;
        unsigned char value;
        if (ct[i].tag == 2) {
            value = (unsigned char)cc_global_get(ct[i].payload);
        } else if (ct[i].tag == 1) {
            uint16_t di = ct[i].payload;
            value = (unsigned char)(di < draw_n ? draws[di] : 0);
        } else {
            continue;
        }
        g_state.cells[cell / 40][cell % 40][boff] = value;
    }
    for (i = 0; i < ot_n; i++) {
        unsigned char value;
        if (ot[i].tag == 2) {
            value = (unsigned char)cc_global_get(ot[i].payload);
        } else if (ot[i].tag == 1) {
            uint16_t di = ot[i].payload;
            uint16_t v = di < draw_n ? draws[di] : 0;
            value = (ot[i].off & 1) ? (unsigned char)(v >> 8)
                                    : (unsigned char)v;
        } else {
            continue;
        }
        ov_obj_byte(ot[i].slot, ot[i].off, value);
    }
}

void load_level(int level, int new_game, int fresh_image, int sound_start) {
    if (level < 0 || level >= OBJ_LEVELS) level = 0;
    /* The original clears [0x513F] before reloading a level (0xEFCF). */
    cc_sound_reset();
    /* 0xCF18: the level-start effect (ID 0x24).  Shell story backgrounds
     * opt out so the finale effect (0x1A) can play instead. */
    if (sound_start) cc_sound_play(SND_LEVEL_START);
    if (new_game) apply_startup_state();
    /* Fresh loads use the baked image.  Real transitions preserve the source
     * memory and replay only the bytes the loader actually writes.  A debug
     * warp also uses the baked image but keeps the current progress. */
    if (new_game || fresh_image || !g_level_overlay) {
        memcpy_(g_state.obj, g_obj_levels[level], sizeof(g_state.obj));
        {
            unsigned i;
            unsigned char *dst = (unsigned char *)g_state.cells;
            for (i = 0; i < sizeof(g_state.cells); i++) dst[i] = 0;
            if (g_cells_asset) {
                memcpy_(g_state.cells,
                        g_cells_asset + (unsigned)level * CELL_ROWS * CELL_COLS * 20,
                        sizeof(g_state.cells));
            }
        }
    }
    g_state.obj_cursor = g_level_cursor[level];
    g_state.a0f8 = 0;
    g_state.cur_level = (int16_t)level;
    /* The destination entry cell decides whether the door override is used:
     * cave -> hub spawns at the matching hub door ([0x2B72]/[0x2B74]),
     * while hub -> cave always uses the cave's Y-token start (the loader's
     * comparison fails because a hub cell has no meaning in a cave). */
    int use_start = (g_state.spawn_col_override == 0xffff) || (level != 2);
    apply_level_state(level, use_start, new_game || fresh_image);
    if (!new_game && !fresh_image) apply_level_overlay(level);
    if (level >= 2) g_state.g_a0cc = (uint16_t)count_crystal_cells();
}

void cc_objects_init(int level) {
    g_state.spawn_col_override = 0xffff;   /* no override on a fresh load */
    g_state.spawn_row_override = 0;
    load_level(level, 1, 1, 1);
}

void cc_objects_init_silent(int level) {
    g_state.spawn_col_override = 0xffff;
    g_state.spawn_row_override = 0;
    load_level(level, 1, 1, 0);
}

/* Debug warp: use the destination's baked image instead of replaying a real
 * door/exit transition, which would preserve source cave cells. */
void cc_warp_level(int level) {
    g_state.spawn_col_override = 0xffff;
    g_state.spawn_row_override = 0;
    load_level(level, 0, 1, 1);
}

/* Transition using the original loader argument (0=hub, 1..16=caves). */
void cc_level_exit(int arg) { load_arg(arg); }
void cc_load_arg(int arg) { load_arg(arg); }
