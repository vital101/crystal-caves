#ifndef GAME_CELL_EVENT_H
#define GAME_CELL_EVENT_H

#include <stdint.h>

void helper_a110_clear(void);
void helper_a59d(void);
void helper_toggle_gravity(void);
void helper_toggle_kind(uint16_t kind);
void switch_pair(uint16_t col1, uint16_t row1, int shape1, int shape2);
void cell_event(int16_t x, int16_t y);
void hazard_cell(int16_t x, int16_t y);
void contact_frame(void);

#endif /* GAME_CELL_EVENT_H */
