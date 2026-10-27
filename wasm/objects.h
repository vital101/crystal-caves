#ifndef OBJECTS_H
#define OBJECTS_H

#include <stdint.h>

#include "objects_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load the oracle-captured 60-slot object image for `level`. */
void cc_objects_init(int level);

/* Advance every object slot once (original update loop, 0xD72A). */
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

#ifdef __cplusplus
}
#endif

#endif
