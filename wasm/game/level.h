#ifndef GAME_LEVEL_H
#define GAME_LEVEL_H

#include <stdint.h>

void apply_startup_state(void);
void apply_level_state(int level, int use_start, int fresh);
uint16_t ov_rd16(const unsigned char *p);
uint32_t ov_rd32(const unsigned char *p);
void ov_obj_byte(int slot, int off, unsigned char value);
void apply_level_overlay(int level);
void load_level(int level, int new_game, int fresh_image, int sound_start);
void load_arg(int arg);

#endif /* GAME_LEVEL_H */
