/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"


int cc_obj_count(void) { return OBJ_MAX; }
uint16_t cc_obj_state(int i)  { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].state  : 0; }
int32_t  cc_obj_x(int i)      { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].x      : 0; }
int32_t  cc_obj_y(int i)      { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].y      : 0; }
uint16_t cc_obj_kind(int i)   { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].kind   : 0; }
uint16_t cc_obj_shape(int i)  { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].shape  : 0; }
uint16_t cc_obj_tile(int i)   { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].block   : 0; }
uint16_t cc_obj_p10(int i)    { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].anim_frame    : 0; }
/* Shape actually selected by the original draw loop: base + anim_frame - 1. */
uint16_t cc_obj_draw_shape(int i) {
    if (i < 0 || i >= OBJ_MAX) return 0;
    uint16_t base = g_state.obj[i].shape;
    if (!base) return 0;
    int sh = (int)base - 1 + (int)g_state.obj[i].anim_frame;
    return sh < 1 ? 0 : (uint16_t)sh;
}
uint16_t cc_obj_p12(int i)    { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].anim_shape_a    : 0; }
uint16_t cc_obj_p14(int i)    { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].anim_shape_b    : 0; }
uint16_t cc_obj_p16(int i)    { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].anim_last    : 0; }
uint16_t cc_obj_p18(int i)    { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].phase    : 0; }
uint16_t cc_obj_p1a(int i)    { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].subtype    : 0; }
uint16_t cc_obj_ready(int i)  { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].ready  : 0; }
int32_t  cc_obj_vx(int i)     { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].vx     : 0; }
int32_t  cc_obj_vy(int i)     { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].vy     : 0; }
uint16_t cc_obj_speed(int i)  { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].speed  : 0; }
uint16_t cc_obj_p24(int i)    { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].unused24    : 0; }
uint16_t cc_obj_cursor(void)  { return g_state.obj_cursor; }
void     cc_obj_set_cursor(int value) { g_state.obj_cursor = (uint16_t)value; }
/* Current level index (updated by cc_level_exit / cc_objects_init). */
int32_t  cc_cur_level(void)    { return g_state.cur_level; }
int cc_cell_byte(int col, int row, int off) {
    if (col < 0 || col >= CELL_COLS || row < 0 || row >= CELL_ROWS ||
        off < 0 || off >= 20) return -1;
    if (g_cells_asset) return g_state.cells[row][col][off];
    if (off == CELL_FLAG_SOLID) return cc_cell_solid(g_state.cur_level, col, row);
    if (off == CELL_FLAG_EXIT) return cc_cell_exit(g_state.cur_level, col, row);
    return 0;
}
void cc_cell_set(int col, int row, int off, int value) {
    if (col < 0 || col >= CELL_COLS || row < 0 || row >= CELL_ROWS ||
        off < 0 || off >= 20) return;
    g_state.cells[row][col][off] = (unsigned char)value;
}

/* ---------------------------------------------------------------- test seeding
 * Generic field access mirrors tools/emulate_load.OBJ_FIELDS offsets.
 * Returns signed 16-bit values for x/y/x_prev/y_prev/vx/vy and unsigned for
 * the word fields; callers compare with & 0xFFFF. */
int32_t cc_obj_get(int i, int off) {
    if (i < 0 || i >= OBJ_MAX) return 0;
    const ObjectEntry *o = &g_state.obj[i];
    switch (off) {
        case 0x00: return o->state;
        case 0x02: return o->hit_flag;
        case 0x04: return o->hit_points;
        case 0x06: return (int16_t)o->x;
        case 0x08: return (int16_t)o->y;
        case 0x0a: return (int16_t)o->x_prev;
        case 0x0c: return (int16_t)o->y_prev;
        case 0x0e: return o->block;
        case 0x10: return o->anim_frame;
        case 0x12: return o->anim_shape_a;
        case 0x14: return o->anim_shape_b;
        case 0x16: return o->anim_last;
        case 0x18: return o->phase;
        case 0x1a: return o->subtype;
        case 0x1c: return o->shape;
        case 0x1e: return (int16_t)o->vx;
        case 0x20: return (int16_t)o->vy;
        case 0x22: return o->speed;
        case 0x24: return o->unused24;
        case 0x26: return o->kind;
        case 0x28: return o->ready;
        case 0x29: return o->handler;
        case 0x2b: return o->handler_seg;
        default: return 0;
    }
}
void cc_obj_set(int i, int off, int32_t v) {
    if (i < 0 || i >= OBJ_MAX) return;
    ObjectEntry *o = &g_state.obj[i];
    switch (off) {
        case 0x00: o->state = (uint16_t)v; break;
        case 0x02: o->hit_flag = (uint16_t)v; break;
        case 0x04: o->hit_points = (uint16_t)v; break;
        case 0x06: o->x = (int16_t)v; break;
        case 0x08: o->y = (int16_t)v; break;
        case 0x0a: o->x_prev = (int16_t)v; break;
        case 0x0c: o->y_prev = (int16_t)v; break;
        case 0x0e: o->block = (uint16_t)v; break;
        case 0x10: o->anim_frame = (uint16_t)v; break;
        case 0x12: o->anim_shape_a = (uint16_t)v; break;
        case 0x14: o->anim_shape_b = (uint16_t)v; break;
        case 0x16: o->anim_last = (uint16_t)v; break;
        case 0x18: o->phase = (uint16_t)v; break;
        case 0x1a: o->subtype = (uint16_t)v; break;
        case 0x1c: o->shape = (uint16_t)v; break;
        case 0x1e: o->vx = (int16_t)v; break;
        case 0x20: o->vy = (int16_t)v; break;
        case 0x22: o->speed = (uint16_t)v; break;
        case 0x24: o->unused24 = (uint16_t)v; break;
        case 0x26: o->kind = (uint16_t)v; break;
        case 0x28: o->ready = (uint8_t)v; break;
        case 0x29: o->handler = (uint16_t)v; break;
        case 0x2b: o->handler_seg = (uint16_t)v; break;
        default: break;
    }
}


/* C4 exports. */
void     cc_set_player(int32_t x, int32_t y) { g_state.player_x = (int16_t)x; g_state.player_y = (int16_t)y; g_state.obj[1].x = (int16_t)x; g_state.obj[1].y = (int16_t)y; }
int32_t  cc_player_x(void) { return g_state.player_x; }
int32_t  cc_player_y(void) { return g_state.player_y; }
int32_t  cc_cam_x(void)    { return g_state.cam_x; }
int32_t  cc_cam_y(void)    { return g_state.cam_y; }
int16_t  cc_move_dir(void) { return g_state.move_dir; }
int      cc_collide_player(int mode, int16_t radius);

/* C5 exports: score / collectibles / lives / sound. */
uint32_t cc_score(void)       { return ((uint32_t)g_state.score_hi << 16) | g_state.score_lo; }
uint16_t cc_score_lo(void)    { return g_state.score_lo; }
uint16_t cc_score_hi(void)    { return g_state.score_hi; }
uint16_t cc_cooldown(void)    { return g_state.cooldown; }
uint16_t cc_g_2da(void)       { return g_state.fire_impulse; }
uint16_t cc_g_a0c4(void)      { return g_state.power_timer; }
uint8_t  cc_g_a0ff(void)      { return g_state.power_shot; }
uint16_t cc_collectibles_total(void)  {
    return (uint16_t)(g_state.g_50a2 + g_state.g_a0cc);
}
uint16_t cc_collectibles_left(void)   { return g_state.g_a0cc; }
uint16_t cc_lives(void)             { return g_state.g_509e; }
uint16_t cc_health(void)            { return g_state.g_509e; }
uint16_t cc_ammo(void)              { return g_state.g_509c; }
uint16_t cc_crystals_collected(void) { return g_state.g_50a2; }
uint16_t cc_crystals_remaining(void) { return g_state.g_a0cc; }
int16_t  cc_level_arg(void)          { return g_state.next_level; }
uint8_t  cc_exit_unlocked(void)      { return g_state.g_a0f5; }
uint8_t  cc_exit_active(void)        { return g_state.g_a0f6; }
uint8_t  cc_door_active(void)        { return g_state.g_a0f7; }
int      cc_cave_flag(int idx)       { return cave_flag_get(idx); }
int      cc_level_complete(void)     { return g_state.g_a0fb; }

/* ---------------------------------------------------------------- physics debug
 * Field order must stay in sync with enum cc_phys_field / tools fixtures. */
int32_t cc_phys_get(int f) {
    switch (f) {
        case CC_PHYS_X: return g_state.player_x;
        case CC_PHYS_Y: return g_state.player_y;
        case CC_PHYS_CAM_X: return g_state.cam_x;
        case CC_PHYS_CAM_Y: return g_state.cam_y;
        case CC_PHYS_MOVE_DIR: return (int16_t)g_state.move_dir;
        case CC_PHYS_WALK_SPEED: return g_state.walk_speed;
        case CC_PHYS_IMPULSE: return g_state.impulse;
        case CC_PHYS_WALK_PHASE: return g_state.walk_phase;
        case CC_PHYS_JUMP_PHASE: return g_state.jump_phase;
        case CC_PHYS_JUMP_FLAG: return g_state.jump_flag;
        case CC_PHYS_AIRBORNE: return g_state.airborne;
        case CC_PHYS_ANIM_SUPPRESS: return g_state.anim_suppress;
        case CC_PHYS_SHAPE: return g_state.shape_player;
        case CC_PHYS_SHAPE_A: return g_state.shape_a;
        case CC_PHYS_SHAPE_B: return g_state.shape_b;
        case CC_PHYS_PLAYER_BLOCK: return g_state.player_block;
        case CC_PHYS_ANIM_TIMER: return g_state.anim_timer;
        case CC_PHYS_CARRY_X: return g_state.g_5118;
        case CC_PHYS_SCROLL_X: return g_state.g_5126;
        case CC_PHYS_CARRY_Y: return g_state.g_5128;
        case CC_PHYS_RIDE_H: return g_state.g_513d;
        case CC_PHYS_RIDE_V: return g_state.g_5142;
        case CC_PHYS_BLOCKED_L: return g_state.blocked_l;
        case CC_PHYS_BLOCKED_R: return g_state.blocked_r;
        case CC_PHYS_GRAVITY: return g_state.gravity_scale;
        case CC_PHYS_C8: return g_state.c8;
        case CC_PHYS_C6: return g_state.c6;
        case CC_PHYS_LIVES: return g_state.g_509e;
        case CC_PHYS_AMMO: return g_state.g_509c;
        case CC_PHYS_INVULN: return g_state.g_a0ea;
        case CC_PHYS_HIT_LOCK: return g_state.obj[2].ready;
        case CC_PHYS_EXIT_STATE: return g_state.g_5124;
        default: return 0;
    }
}

void cc_phys_set(int f, int32_t v) {
    switch (f) {
        case CC_PHYS_X: g_state.player_x = (int16_t)v; break;
        case CC_PHYS_Y: g_state.player_y = (int16_t)v; break;
        case CC_PHYS_CAM_X: g_state.cam_x = (int16_t)v; break;
        case CC_PHYS_CAM_Y: g_state.cam_y = (int16_t)v; break;
        case CC_PHYS_MOVE_DIR: g_state.move_dir = (uint16_t)v; break;
        case CC_PHYS_WALK_SPEED: g_state.walk_speed = (uint16_t)v; break;
        case CC_PHYS_IMPULSE: g_state.impulse = (uint16_t)v; break;
        case CC_PHYS_WALK_PHASE: g_state.walk_phase = (uint16_t)v; break;
        case CC_PHYS_JUMP_PHASE: g_state.jump_phase = (uint16_t)v; break;
        case CC_PHYS_JUMP_FLAG: g_state.jump_flag = (uint8_t)v; break;
        case CC_PHYS_AIRBORNE: g_state.airborne = (uint8_t)v; break;
        case CC_PHYS_ANIM_SUPPRESS: g_state.anim_suppress = (uint8_t)v; break;
        case CC_PHYS_SHAPE: g_state.shape_player = (uint16_t)v; break;
        case CC_PHYS_SHAPE_A: g_state.shape_a = (uint16_t)v; break;
        case CC_PHYS_SHAPE_B: g_state.shape_b = (uint16_t)v; break;
        case CC_PHYS_PLAYER_BLOCK: g_state.player_block = (uint16_t)v; break;
        case CC_PHYS_ANIM_TIMER: g_state.anim_timer = (uint16_t)v; break;
        case CC_PHYS_CARRY_X: g_state.g_5118 = (uint16_t)v; break;
        case CC_PHYS_SCROLL_X: g_state.g_5126 = (uint16_t)v; break;
        case CC_PHYS_CARRY_Y: g_state.g_5128 = (uint16_t)v; break;
        case CC_PHYS_RIDE_H: g_state.g_513d = (uint8_t)v; break;
        case CC_PHYS_RIDE_V: g_state.g_5142 = (uint8_t)v; break;
        case CC_PHYS_BLOCKED_L: g_state.blocked_l = (uint8_t)v; break;
        case CC_PHYS_BLOCKED_R: g_state.blocked_r = (uint8_t)v; break;
        case CC_PHYS_GRAVITY: g_state.gravity_scale = (int16_t)v; break;
        case CC_PHYS_C8: g_state.c8 = (uint16_t)v; break;
        case CC_PHYS_C6: g_state.c6 = (uint16_t)v; break;
        case CC_PHYS_LIVES: g_state.g_509e = (uint16_t)v; break;
        case CC_PHYS_AMMO: g_state.g_509c = (uint16_t)v; break;
        case CC_PHYS_INVULN: g_state.g_a0ea = (uint16_t)v; break;
        case CC_PHYS_HIT_LOCK: g_state.obj[2].ready = (uint8_t)v; break;
        case CC_PHYS_EXIT_STATE: g_state.g_5124 = (uint16_t)v; break;
        default: break;
    }
}

