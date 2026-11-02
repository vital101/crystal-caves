#ifndef GAME_PHYSICS_H
#define GAME_PHYSICS_H

#include <stdint.h>

void input_commit(void);
int cc_cell_collide(int level, int32_t px, int32_t py, int16_t xoff, int16_t yoff);
int player_clear(int16_t xoff, int16_t yoff);
void player_anim_next(void);
void player_select_shape(void);
void sync_player_object(void);
void update_walk_speed(void);
void cc_vertical(void);
void cc_jump_vertical(void);
void cc_horizontal(void);
void push_player(int16_t amount, int16_t dir);

#endif /* GAME_PHYSICS_H */
