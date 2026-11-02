#ifndef GAME_COMBAT_H
#define GAME_COMBAT_H

#include <stdint.h>

#include "object.h"

void damage_life(void);
int rect_overlap(int32_t ax0, int32_t ax1, int32_t bx0, int32_t bx1);
void transform_object(ObjectEntry *o);
void slash_gate(ObjectEntry *o, int16_t x, int16_t y);
void slash_state1(ObjectEntry *o, int idx, int16_t x, int16_t y);
void slash_state2(ObjectEntry *o, int idx, int16_t x, int16_t y);
void player_interact(ObjectEntry *o, int16_t x, int16_t y);
void cc_fire(void);
void cc_fire_timers(void);
void cc_status_timers(void);

#endif /* GAME_COMBAT_H */
