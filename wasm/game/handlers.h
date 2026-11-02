#ifndef GAME_HANDLERS_H
#define GAME_HANDLERS_H

#include <stdint.h>

#include "object.h"

typedef void (*handler_fn)(ObjectEntry *, int, int16_t, int16_t);

/*
 * Handler load offsets (segment offsets into the 0x1F9A handler
 * block, see notes/handlers.md), named after the dispatch table
 * entry that implements each one.
 */
enum handler_offset {
    HDL_PLAYER_NOOP = 0x1fb9,
    HDL_DUG_BLOCK = 0x2241,
    HDL_ATTACK_GATE = 0x2850,
    HDL_DISSOLVE = 0x2877,
    HDL_PICKUP_EFFECT = 0x0831,
    HDL_COMPANION_SPAWNER = 0x1fc2,
    HDL_MAP_COMPANION = 0x2176,
    HDL_SPAWN_BELOW = 0x323c,
    HDL_EXPAND_ANIM = 0x3be8,
    HDL_DOOR_LEVER = 0x3d23,
    HDL_AMBIENT_ANIM_A = 0x3e70,
    HDL_DOOR = 0x3f13,
    HDL_WALK_TWO_PHASE = 0x298a,
    HDL_MOVE_V_PROX = 0x2d37,
    HDL_WALK_PROX_SPAWN = 0x3416,
    HDL_PLAYER_SHOVER = 0x403a,
    HDL_MOVE_H_PAIRED = 0x55ec,
    HDL_WALK_RESPAWN = 0x62f2,
    HDL_WALK_TWO_FRAME = 0x6232,
    HDL_SPAWN_PROJECTILE = 0x4c92,
    HDL_BOSS_FORMATION = 0x5c8f,
    HDL_WALK_COMPANION = 0x4eeb,
    HDL_PROJECTILE = 0x52e7,
    HDL_MOVE_V_PAIRED = 0x2f83,
    HDL_LEVER_ANIM = 0x3a2e,
    HDL_PLAYER_SHOVER_ALT = 0x3f6c,
    HDL_SPAWN_CAM_REL = 0x2e25,
    HDL_WALK_ANIM_LR = 0x4ad3,
    HDL_MOVE_V_FORMATION = 0x36b0,
    HDL_WALK_FLIP_CONTACT = 0x2acf,
    HDL_WALK_H_PROX = 0x2bd4,
    HDL_LEVER_SWITCH = 0x3904,
    HDL_BOUNCE_ADVERSARY = 0x4145,
    HDL_WALK_TWO_ANIM = 0x47b9,
    HDL_MOVE_V_COMPANION = 0x32f6,
    HDL_AMBIENT_ANIM_B = 0x4a4b,
    HDL_STATE_TOGGLE = 0x4a82,
    HDL_WALK_RANDOM = 0x48fe,
    HDL_BOSS_RETARGET = 0x4fd4,
    HDL_COMPANION_PAIR = 0x6458,
    HDL_GROUP_MOVE3 = 0x5855,
    HDL_ANIM_MOVER = 0x5bbe,
    HDL_CELL_ERASE = 0x6681,
    HDL_CRYSTAL_BOB = 0x672b,
    HDL_PICKUP_RISE = 0x69d3,
    HDL_PLATFORM_H = 0x6afa,
    HDL_PLATFORM_V = 0x6d31,
    HDL_FLIP_MOVER = 0x6ff6,
    HDL_FLIP_PARTNER = 0x70e4,
    HDL_SLASH_UPDATE = 0x251d,   /* shared slash-blade update helper */
};

void h_player_noop(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_attack_gate(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_dissolve(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_flip_mover(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_flip_partner(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_door(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_door_lever(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_platform_h(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_platform_v(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_pickup_effect(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_ambient_anim_a(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_ambient_anim_b(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_state_toggle(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_companion_spawner(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_spawn_below(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_map_companion(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_group_move3(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_anim_mover(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_expand_anim(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_slash_update(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_dug_block(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_cell_erase(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_crystal_bob(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_pickup_rise(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_walk_two_phase(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_player_shover(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_move_h_paired(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_lever_anim(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_player_shover_alt(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_spawn_cam_rel(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_move_v_paired(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_walk_anim_lr(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_move_v_formation(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_walk_companion(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_projectile(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_walk_two_frame(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_spawn_projectile(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_boss_formation(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_walk_respawn(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_walk_prox_spawn(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_move_v_prox(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_walk_flip_contact(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_walk_h_prox(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_lever_switch(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_walk_two_anim(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_move_v_companion(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_bounce_adversary(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_walk_random(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_companion_pair(ObjectEntry *o, int idx, int16_t x, int16_t y);
void h_boss_retarget(ObjectEntry *o, int idx, int16_t x, int16_t y);

handler_fn handler_for(uint16_t off);
int is_real_handler(uint16_t off);
const char *handler_name(uint16_t off);
int cc_handler_coverage_num(void);

#endif /* GAME_HANDLERS_H */
