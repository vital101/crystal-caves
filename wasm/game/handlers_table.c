/* Crystal Caves gameplay module - split from wasm/objects.c.
 *
 * This module owns the single handler dispatch table for the original
 * object loop: one entry per handler load offset.  handler_for(),
 * is_real_handler() and handler_name() all resolve through it, so there is
 * exactly one list of the original handler offsets in the tree.
 */
#include "game_internal.h"

/* Handler offset (behaviour ID, segment 0x1F9A) for slot i. */
uint16_t cc_obj_handler(int i)  { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].handler : 0; }

/*
 * The single dispatch table for the original handler list (all 49
 * offsets the object loop dispatches to).  Each entry is the handler
 * load offset (segment offset into the 0x1F9A block), the C function
 * that implements it, and the automatic disassembler label it was
 * ported from.  The loader kind table (wasm/objects_data.h) is what
 * associates an object kind with one of these offsets, so kinds are
 * only named in the comments where that table confirms them.
 */
typedef struct {
    uint16_t offset;   /* handler load offset (0x1F9A segment) */
    handler_fn fn;
    const char *name;  /* original automatic label */
} HandlerEntry;

static const HandlerEntry g_handlers[] = {
    /* game/handlers_platforms.c */
    {0x1fb9, h_player_noop, "h_1fb9"},
    {0x2850, h_attack_gate, "h_2850"},
    {0x2877, h_dissolve, "h_2877"},
    {0x0831, h_pickup_effect, "h_831"},
    {0x3d23, h_door_lever, "h_3d23"},
    {0x3f13, h_door, "h_3f13"},
    {0x6afa, h_platform_h, "h_6afa"},
    {0x6d31, h_platform_v, "h_6d31"},
    {0x6ff6, h_flip_mover, "h_6ff6"},
    {0x70e4, h_flip_partner, "h_70e4"},

    /* game/handlers_interaction.c */
    {0x2241, h_dug_block, "h_2241"},
    {0x1fc2, h_companion_spawner, "h_1fc2"},
    {0x2176, h_map_companion, "h_2176"},
    {0x323c, h_spawn_below, "h_323c"},
    {0x3be8, h_expand_anim, "h_3be8"},
    {0x3e70, h_ambient_anim_a, "h_3e70"},
    {0x4a4b, h_ambient_anim_b, "h_4a4b"},
    {0x4a82, h_state_toggle, "h_4a82"},
    {0x5855, h_group_move3, "h_5855"},
    {0x5bbe, h_anim_mover, "h_5bbe"},

    /* game/handlers_actors_a.c */
    {0x298a, h_walk_two_phase, "h_298a"},
    {0x403a, h_player_shover, "h_403a"},
    {0x55ec, h_move_h_paired, "h_55ec"},
    {0x6232, h_walk_two_frame, "h_6232"},
    {0x4c92, h_spawn_projectile, "h_4c92"},
    {0x4eeb, h_walk_companion, "h_4eeb"},
    {0x52e7, h_projectile, "h_52e7"},
    {0x2f83, h_move_v_paired, "h_2f83"},
    {0x3a2e, h_lever_anim, "h_3a2e"},
    {0x3f6c, h_player_shover_alt, "h_3f6c"},
    {0x2e25, h_spawn_cam_rel, "h_2e25"},
    {0x4ad3, h_walk_anim_lr, "h_4ad3"},
    {0x36b0, h_move_v_formation, "h_36b0"},
    {0x6681, h_cell_erase, "h_6681"},
    {0x672b, h_crystal_bob, "h_672b"},
    {0x69d3, h_pickup_rise, "h_69d3"},

    /* game/handlers_actors_b.c */
    {0x2d37, h_move_v_prox, "h_2d37"},
    {0x3416, h_walk_prox_spawn, "h_3416"},
    {0x62f2, h_walk_respawn, "h_62f2"},
    {0x5c8f, h_boss_formation, "h_5c8f"},
    {0x2acf, h_walk_flip_contact, "h_2acf"},
    {0x2bd4, h_walk_h_prox, "h_2bd4"},
    {0x3904, h_lever_switch, "h_3904"},
    {0x4145, h_bounce_adversary, "h_4145"},
    {0x47b9, h_walk_two_anim, "h_47b9"},
    {0x32f6, h_move_v_companion, "h_32f6"},
    {0x48fe, h_walk_random, "h_48fe"},
    {0x4fd4, h_boss_retarget, "h_4fd4"},
    {0x6458, h_companion_pair, "h_6458"},
};

/* Function for one handler load offset, or the inert fallback. */
handler_fn handler_for(uint16_t off) {
    for (const HandlerEntry *e = g_handlers;
         e != g_handlers + sizeof(g_handlers) / sizeof(g_handlers[0]); ++e) {
        if (e->offset == off)
            return e->fn;
    }
    g_unknown_handler_hits++;
    return h_player_noop;   /* inert fallback */
}

/* True if the original game would have dispatched to this offset. */
int is_real_handler(uint16_t off) {
    for (const HandlerEntry *e = g_handlers;
         e != g_handlers + sizeof(g_handlers) / sizeof(g_handlers[0]); ++e) {
        if (e->offset == off)
            return 1;
    }
    return 0;
}

/* Original disassembler label for a handler load offset. */
const char *handler_name(uint16_t off) {
    for (const HandlerEntry *e = g_handlers;
         e != g_handlers + sizeof(g_handlers) / sizeof(g_handlers[0]); ++e) {
        if (e->offset == off)
            return e->name;
    }
    return 0;
}

/* Porting-progress coverage metric (see test_gameplay.js). */int cc_handler_coverage_num(void) {
    int n = 0, d = 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        if (g_state.obj[i].kind == 0) continue;
        if (g_state.obj[i].state == 2) continue;
        d++;
        if (is_real_handler(g_state.obj[i].handler)) n++;
    }
    return d ? (n * 100) / d : 100;
}
