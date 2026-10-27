#ifndef OBJECTS_H
#define OBJECTS_H

#include <stdint.h>

#include "objects_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load the oracle-captured 60-slot object image for `level`. */
void cc_objects_init(int level);

/* Advance every object slot once (original update loop, 0xD72A).
 * C4: also processes input and updates the camera. */
void cc_objects_step(void);

int  cc_obj_count(void);
uint16_t cc_obj_state(int i);
int32_t  cc_obj_x(int i);
int32_t  cc_obj_y(int i);
uint16_t cc_obj_kind(int i);
uint16_t cc_obj_shape(int i);
uint16_t cc_obj_tile(int i);
uint16_t cc_obj_p10(int i);
uint16_t cc_obj_p18(int i);
uint16_t cc_obj_ready(int i);
int32_t  cc_obj_vx(int i);
uint16_t cc_obj_cursor(void);

/* C4: input.  Set a directional input flag (pressed=1 / released=0).
 * Flags: 0x338E=left, 0x338F=right, 0x338D=up, 0x338C=down, 0x338A=fire. */
void cc_input_set(uint16_t flag, int pressed);

/* C4: player / camera. */
int32_t  cc_player_x(void);
int32_t  cc_player_y(void);
int32_t  cc_cam_x(void);
int32_t  cc_cam_y(void);
int16_t  cc_move_dir(void);

/* C4: collision.  Proximity test against the player (helper 0x1F9A:0x177B).
 * Returns object index or -1. */
int  cc_collide_player(int mode, int16_t radius);

#ifdef __cplusplus
}
#endif

#endif
