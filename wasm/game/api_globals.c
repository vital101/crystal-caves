/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"

/*
 * Generic DS-offset access for the captured gameplay globals.
 *
 * Each row is one observable scalar: DS word offset (as the JS harnesses
 * address it), the cc_state_t field that mirrors it, and the read/write
 * widths the original stores use (byte fields are masked to 8 bits, words
 * keep 16; a few fields read back signed).  Order and duplicates mirror the
 * original switch arms; cave_flags (DS 0x50A8..0x50B8) are the only
 * range-mapped group and stay handled separately.
 */
typedef struct GlobalField {
    uint16_t off;
    unsigned long field;
    unsigned char sgn;  /* read back sign-extended (int16_t fields) */
    unsigned char wide; /* word store (matches a >= 2-byte state field) */
} GlobalField;

#define GF(off, field, sgn)                                            \
    { off, CC_OFFSETOF(cc_state_t, field), sgn,                        \
      sizeof(((cc_state_t *)0)->field) >= 2 }

/* Rows mirror the original switch arms in order.  The byte-vs-word store
 * width comes from the cc_state_t field itself, so it cannot drift. */
static const GlobalField g_global_fields[] = {
    GF(0xA0F1, blocked_l, 0),
    GF(0xA0F2, blocked_r, 0),
    GF(0xA0F6, interaction_lock, 0),
    GF(0xA0F8, a0f8, 0),
    GF(0xA0FC, g_a0fc, 0),
    GF(0xA0FD, airborne, 0),
    GF(0xA0EA, invuln_timer, 0),
    GF(0xA0B4, walk_phase, 0),
    GF(0xA0B8, g_a0b8, 0),
    GF(0x02BA, walk_speed, 0),
    GF(0x02CC, g_2cc, 0),
    GF(0x02CE, g_2ce, 0),
    GF(0x02D4, g_2d4, 0),
    GF(0x02D6, g_2d6, 0),
    GF(0x02D8, g_2d8, 0),
    GF(0x02FA, g_2fa, 0),
    GF(0x02FC, g_2fc, 0),
    GF(0x02FE, g_2fe, 0),
    GF(0x02CA, impulse, 0),
    GF(0x02C8, c8, 0),
    GF(0x02C6, c6, 0),
    GF(0x02E0, cooldown, 0),
    GF(0x0260, g_260, 0),
    GF(0x50DC, g_50dc, 0),
    GF(0x50DE, g_50de, 0),
    GF(0x50E0, g_50e0, 0),
    GF(0xA0F0, g_a0f0, 0),
    GF(0xA0FA, g_a0fa, 0),
    GF(0x0306, jump_flag, 0),
    GF(0x030A, anim_suppress, 0),
    GF(0x030B, status_30b, 0),
    GF(0x030C, paused_30c, 0),
    GF(0x5118, carry_x_delta, 0),
    GF(0x5126, scroll_x_accum, 0),
    GF(0x5128, carry_y_delta, 0),
    GF(0x5124, death_state, 0),
    GF(0x512E, cam_x, 1),
    GF(0x5130, cam_y, 1),
    GF(0x5132, gravity_scale, 1),
    GF(0x5134, jump_phase, 0),
    GF(0x513C, interaction_enabled, 0),
    GF(0x513D, riding_horizontal, 0),
    GF(0x5140, interaction_suppressed, 0),
    GF(0x5142, riding_vertical, 0),
    GF(0x509C, ammo, 0),
    GF(0x509E, health, 0),
    GF(0x02A6, rng_lo, 0),
    GF(0x02A8, rng_hi, 0),
    GF(0x02DA, fire_impulse, 0),
    GF(0x02F2, g_2f2, 0),
    GF(0x02B8, g_2b8, 0),
    GF(0x2B5E, restart_timer, 0),
    GF(0x2B5C, text_scroll, 1),
    GF(0x511E, g_511e, 0),
    GF(0x5122, g_5122, 0),
    GF(0x50A5, score_lo, 0),
    GF(0x50A7, score_hi, 0),
    GF(0x50A0, checkpoint_50a0, 0),
    GF(0x50BE, checkpoint_50be, 0),
    GF(0x50C4, checkpoint_50c4, 0),
    GF(0x50C7, checkpoint_50c7, 0),
    GF(0x50C9, checkpoint_50c9, 0),
    GF(0xA0FF, power_shot_active, 0),
    GF(0xA0C4, power_timer, 0),
    GF(0xA0CC, crystals_remaining, 0),
    GF(0x50A2, crystals_collected, 0),
    GF(0xA0F5, exit_unlocked, 0),
    GF(0xA0F7, hub_door_active, 0),
    GF(0xA0F9, switch_latch, 0),
    GF(0xA0F3, lever_state, 0),
    GF(0xA0FB, all_caves_complete, 0),
    GF(0xA126, story_transition, 0),
    GF(0xA10B, collectible_spawn_active, 0),
    GF(0xA10A, suppress_base_stamp, 0),
    GF(0xA0E8, switch_repeat, 0),
    GF(0xA0D0, spawn_cell_x, 0),
    GF(0xA0D2, spawn_cell_y, 0),
    GF(0xA0D4, switch_a_col, 0),
    GF(0xA0D6, switch_a_row, 0),
    GF(0xA0D8, switch_b_col, 0),
    GF(0xA0DA, switch_b_row, 0),
    GF(0xA0DC, switch_c_col, 0),
    GF(0xA0DE, switch_c_row, 0),
    GF(0xA0CE, collectible_spawn_timer, 0),
    GF(0xA0C2, power_shot_timer_preset, 0),
    GF(0xA0C8, gravity_timer_preset, 0),
    GF(0xA0CA, red_mushroom_timer_preset, 0),
    GF(0xA0E4, stun_timer_preset, 0),
    GF(0xA0C6, power_subtimer, 0),
    GF(0xA0E0, exit_col, 0),
    GF(0xA0E2, exit_row, 0),
    GF(0xA0E6, exit_step, 0),
    GF(0xA0BE, next_level, 0),
    GF(0xA100, g_a100, 0),
    GF(0xA101, g_a101, 0),
    GF(0xA102, g_a102, 0),
    GF(0xA104, g_a104, 0),
    GF(0xA105, g_a105, 0),
    GF(0xA106, g_a106, 0),
    GF(0xA107, g_a107, 0),
    GF(0xA108, g_a108, 0),
    GF(0xA109, g_a109, 0),
    GF(0xA10D, g_a10d, 0),
    GF(0xA0F4, g_a0f4, 0),
    GF(0x3392, g_3392, 0),
    GF(0x3395, g_3395, 0),
    GF(0x3396, g_3396, 0),
};

static const GlobalField *global_field(uint16_t off) {
    for (unsigned i = 0; i < sizeof g_global_fields / sizeof g_global_fields[0]; ++i)
        if (g_global_fields[i].off == off)
            return &g_global_fields[i];
    return 0;
}

/* Row semantics:
 *  - read: `wide` rows fetch the DS word (16-bit, `sgn` fields sign-extended);
 *    the others fetch the single byte, matching the byte-typed state fields
 *    the original switch arms returned.
 *  - write: `wide` rows store the full word (word stores), the others are
 *    byte stores (mov byte [off], al) touching only the low byte.
 * Byte/word rows mirror the original store widths; when renaming a field
 * keep the width that the original memory had at that DS offset. */
static int global_read(const GlobalField *f) {
    const unsigned char *p = (const unsigned char *)&g_state + f->field;
    unsigned v = (unsigned)p[0];
    if (f->wide)
        v |= (unsigned)p[1] << 8;
    return f->sgn ? (int)(int16_t)v : (int)v;
}

static void global_write(const GlobalField *f, int32_t v) {
    unsigned char *p = (unsigned char *)&g_state + f->field;
    p[0] = (unsigned char)v;
    if (f->wide)
        p[1] = (unsigned char)(v >> 8);
}

/* Arms the table cannot express: DS aliases that mirror into the player
 * object image (0x4611..0x4629) and the attack slot (0x4660/0x4652), plus
 * the 0x460D object byte.  Everything else is a g_global_fields row. */
int32_t cc_global_get(int off) {
    const GlobalField *f = global_field((uint16_t)(off & 0xFFFF));
    if (f)
        return global_read(f);
    switch (off & 0xFFFF) {
        case 0x4611: return (int16_t)g_state.player_x;
        case 0x4613: return (int16_t)g_state.player_y;
        case 0x461B: return g_state.anim_timer;
        case 0x461D: return g_state.shape_b;
        case 0x461F: return g_state.shape_a;
        case 0x4619: return g_state.player_block;
        case 0x4627: return g_state.shape_player;
        case 0x4629: return g_state.move_dir;
        case 0x460D: return g_state.obj[1].hit_flag;
        case 0x4660: return g_state.obj[2].ready;
        case 0x4652: return g_state.obj[2].subtype;
        default:
            if ((off & 0xFFFF) >= 0x50A8 && (off & 0xFFFF) <= 0x50B8)
                return cave_flag_get((off & 0xFFFF) - 0x50A8);
            return 0;
    }
}
void cc_global_set(int off, int32_t v) {
    const GlobalField *f = global_field((uint16_t)(off & 0xFFFF));
    if (f) {
        global_write(f, v);
        return;
    }
    switch (off & 0xFFFF) {
        case 0x4611: g_state.player_x = (int16_t)v; g_state.obj[1].x = (int16_t)v; break;
        case 0x4613: g_state.player_y = (int16_t)v; g_state.obj[1].y = (int16_t)v; break;
        case 0x461B: g_state.anim_timer = (uint16_t)v; g_state.obj[1].anim_frame = (uint16_t)v; break;
        case 0x461D: g_state.shape_b = (uint16_t)v; g_state.obj[1].anim_shape_a = (uint16_t)v; break;
        case 0x461F: g_state.shape_a = (uint16_t)v; g_state.obj[1].anim_shape_b = (uint16_t)v; break;
        case 0x4619: g_state.player_block = (uint16_t)v; g_state.obj[1].block = (uint16_t)v; break;
        case 0x4627: g_state.shape_player = (uint16_t)v; g_state.obj[1].shape = (uint16_t)v; break;
        case 0x4629: g_state.move_dir = (uint16_t)v; g_state.obj[1].vx = (int16_t)v; break;
        case 0x460D: g_state.obj[1].hit_flag = (uint16_t)v; break;
        case 0x4660: g_state.obj[2].ready = (uint8_t)v; break;
        case 0x4652: g_state.obj[2].subtype = (uint16_t)v; break;
        default:
            if ((off & 0xFFFF) >= 0x50A8 && (off & 0xFFFF) <= 0x50B8)
                cave_flag_set((off & 0xFFFF) - 0x50A8, (int)v);
            break;
    }
}

