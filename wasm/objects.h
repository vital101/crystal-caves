#ifndef OBJECTS_H
#define OBJECTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional full mutable 20-byte cell image (19 x 25 x 40 x 20; cells.bin). */
void cc_set_cells(const unsigned char *cells);
void cc_set_level_overlay(const unsigned char *data);
int cc_cell_byte(int col, int row, int off);
void cc_cell_set(int col, int row, int off, int value);

/* Load the oracle-captured 60-slot object image for `level`. */
void cc_objects_init(int level);
/* Shell story backgrounds: load the level without the gameplay level-start
 * sound (0x24), which would otherwise preempt the finale (0x1A) effect. */
void cc_objects_init_silent(int level);
void cc_warp_level(int level);
void cc_level_exit(int arg);
void cc_load_arg(int arg);
int32_t cc_cur_level(void);

/* Advance every object slot once (original update loop, 0xD72A).
 * C4: also processes input and updates the camera. */
void cc_objects_step(void);

/* Advance only the object update loop (no player physics/camera).  Used by
 * deterministic oracle fixtures. */
void cc_objects_step_objects(void);

/* Loader test hook: poison all source object/cell bytes. */
void cc_test_fill_source(int obj_byte, int cell_byte);

/* Handler test hook: call a handler directly with original (y,x,slot) args. */
void cc_test_call_handler(int handler, int slot, int x, int y);
uint32_t cc_unknown_handler_hits(void);

/* Combat test hook: invoke the 0x2002D slot-3 effect spawn. */
void cc_test_cell_event(int x, int y);
void cc_test_contact(void);
void cc_test_spawn_2002d(int mode);
void cc_test_spawn_47d(int handler, int handler_seg, int phase, int speed,
                       int anim_last, int anim_shape_b, int anim_shape_a,
                       int vy, int vx,
                       int ycell, int xcell, int block, int kind, int slot);

/* Advance only exact player physics (0xE011 + 0xE36B); no object loop. */
void cc_objects_step_physics(void);

/* Full normal frame (physics, fire/timers, object loop).  Phase 2.2 fills in
 * 0xEBF2/0xEAEA; used by the game shell and combat fixtures. */
void cc_objects_step_full(void);
void cc_player_draw_sound(void);

/* Exact physics-state debug/test access (field order mirrors
 * tools/oracle_physics.FIELDS). */
enum cc_phys_field {
    CC_PHYS_X = 0, CC_PHYS_Y, CC_PHYS_CAM_X, CC_PHYS_CAM_Y,
    CC_PHYS_MOVE_DIR, CC_PHYS_WALK_SPEED, CC_PHYS_IMPULSE, CC_PHYS_WALK_PHASE,
    CC_PHYS_JUMP_PHASE, CC_PHYS_JUMP_FLAG, CC_PHYS_AIRBORNE,
    CC_PHYS_ANIM_SUPPRESS, CC_PHYS_SHAPE, CC_PHYS_SHAPE_A, CC_PHYS_SHAPE_B,
    CC_PHYS_PLAYER_BLOCK, CC_PHYS_ANIM_TIMER, CC_PHYS_CARRY_X,
    CC_PHYS_SCROLL_X, CC_PHYS_CARRY_Y, CC_PHYS_RIDE_H, CC_PHYS_RIDE_V,
    CC_PHYS_BLOCKED_L, CC_PHYS_BLOCKED_R, CC_PHYS_GRAVITY, CC_PHYS_C8,
    CC_PHYS_C6, CC_PHYS_LIVES, CC_PHYS_AMMO, CC_PHYS_INVULN,
    CC_PHYS_HIT_LOCK, CC_PHYS_EXIT_STATE, CC_PHYS_COUNT
};
int32_t cc_phys_get(int field);
void    cc_phys_set(int field, int32_t value);

int  cc_obj_count(void);
/* Generic object field access using tools/emulate_load.OBJ_FIELDS offsets. */
int32_t cc_obj_get(int i, int off);
void    cc_obj_set(int i, int off, int32_t value);
uint16_t cc_obj_state(int i);
int32_t  cc_obj_x(int i);
int32_t  cc_obj_y(int i);
uint16_t cc_obj_kind(int i);
uint16_t cc_obj_shape(int i);
uint16_t cc_obj_tile(int i);
uint16_t cc_obj_p10(int i);
uint16_t cc_obj_draw_shape(int i);
uint16_t cc_obj_p12(int i);
uint16_t cc_obj_p14(int i);
uint16_t cc_obj_p16(int i);
uint16_t cc_obj_p18(int i);
uint16_t cc_obj_p1a(int i);
uint16_t cc_obj_ready(int i);
int32_t  cc_obj_vx(int i);
int32_t  cc_obj_vy(int i);
uint16_t cc_obj_speed(int i);
uint16_t cc_obj_p24(int i);
uint16_t cc_obj_cursor(void);
void     cc_obj_set_cursor(int value);

/* C4: input.  Set a directional input flag (pressed=1 / released=0).
 * Flags: 0x338E=left, 0x338F=right, 0x338B=jump, 0x3390=fire.
 * 0x338A is joystick fire and is accepted but unused. */
void cc_input_set(uint16_t flag, int pressed);

/* C4: player / camera. */
void     cc_set_player(int32_t x, int32_t y);
int32_t  cc_player_x(void);
int32_t  cc_player_y(void);
int32_t  cc_cam_x(void);
int32_t  cc_cam_y(void);
int16_t  cc_move_dir(void);

/* C4: collision.  Proximity test against the player (helper 0x1F9A:0x177B).
 * Returns object index or -1. */
int  cc_collide_player(int mode, int16_t radius);

/* C5: sound.  Set a callback (id -> void) invoked when the original would
 * play a sound via 0x1F9A:0x21F.  Every ID is also recorded in a log so the
 * wasm test harness can compare event order without JS callbacks. */
void cc_sound_set_callback(void (*cb)(uint16_t id));
void cc_sound_log_clear(void);
int  cc_sound_log_count(void);
int  cc_sound_log_total(void);
uint16_t cc_sound_log_at(int i);

/* Generic gameplay global access by original DS offset. */
int32_t cc_global_get(int off);
void    cc_global_set(int off, int32_t value);

/* Phase 2 RNG (0x2BB5:0x980). */
uint16_t cc_rng_next(uint16_t n);
void     cc_rng_seed(uint16_t lo, uint16_t hi);
uint16_t cc_rng_lo(void);
uint16_t cc_rng_hi(void);

/* C5: score / collectibles / lives / level completion. */
uint32_t  cc_score(void);
uint16_t  cc_score_lo(void);
uint16_t  cc_score_hi(void);
uint16_t  cc_cooldown(void);
uint16_t  cc_g_2da(void);
uint16_t  cc_g_a0c4(void);
uint8_t   cc_g_a0ff(void);
uint16_t  cc_collectibles_total(void);
uint16_t  cc_collectibles_left(void);
uint16_t cc_lives(void);
int       cc_level_complete(void);

/* Phase 5/7: complete deterministic state snapshot (gameplay + cells + flags).
 * The format is versioned and must remain stable.  cc_state_size() returns the
 * maximum buffer size, covering both the current v2 format and legacy v1
 * saves; cc_state_save() returns the actual number of bytes written. */
#define CC_STATE_LOAD_HUB_ONLY 1

enum cc_state_error {
    CC_STATE_OK = 0,
    CC_STATE_ERR_ARG = 1,
    CC_STATE_ERR_MAGIC = 2,
    CC_STATE_ERR_VERSION = 3,
    CC_STATE_ERR_SIZE = 4,
    CC_STATE_ERR_CRC = 5,
    CC_STATE_ERR_INVALID = 6,
    CC_STATE_ERR_NOT_HUB = 7
};

int cc_state_size(void);
int cc_state_save(unsigned char *out, int cap);
int cc_state_load(const unsigned char *in, int len);
int cc_state_load_ex(const unsigned char *in, int len, int flags);
int cc_state_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
