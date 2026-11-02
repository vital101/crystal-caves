#ifndef GAME_UPDATE_H
#define GAME_UPDATE_H

#include <stdint.h>

void step_objects(void);
int count_crystal_cells(void);
void door_entry_frame(void);
void exit_to_hub_frame(void);
void restore_checkpoint(void);
void reload_current_level(void);
void fade_latch(void);
void restart_sequence(void);
int death_animation(void);
void death_state1(void);
void death_state2(void);
void death_state3(void);

#endif /* GAME_UPDATE_H */
