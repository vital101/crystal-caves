#ifndef OBJECTS_H
#define OBJECTS_H

#include <stdint.h>

#include "objects_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional full mutable 20-byte cell image (19 x 25 x 40 x 20; cells.bin). */
void cc_set_cells(const unsigned char *cells);
int cc_cell_byte(int col, int row, int off);

/* Load the oracle-captured 60-slot object image for `level`. */
void cc_objects_init(int level);

/* Advance every object slot once (original update loop, 0xD72A).
 * C4: also processes input and updates the camera. */
void cc_objects_step(void);

/* Advance only the object update loop (no player physics/camera).  Used by
 * deterministic oracle fixtures. */
void cc_objects_step_objects(void);

/* Advance only exact player physics (0xE011 + 0xE36B); no object loop. */
void cc_objects_step_physics(void);

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
uint16_t cc_obj_state(int i);
int32_t  cc_obj_x(int i);
int32_t  cc_obj_y(int i);
uint16_t cc_obj_kind(int i);
uint16_t cc_obj_shape(int i);
uint16_t cc_obj_tile(int i);
uint16_t cc_obj_p10(int i);
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
 * play a sound via 0x1F9A:0x21F. */
void cc_sound_set_callback(void (*cb)(uint16_t id));

/* C5: score / collectibles / lives / level completion. */
uint32_t  cc_score(void);
uint16_t  cc_collectibles_total(void);
uint16_t  cc_collectibles_left(void);
uint16_t  cc_lives(void);
int       cc_level_complete(void);

#ifdef __cplusplus
}
#endif

#endif
