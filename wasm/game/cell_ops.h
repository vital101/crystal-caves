#ifndef GAME_CELL_OPS_H
#define GAME_CELL_OPS_H

#include <stdint.h>

unsigned char *cell_rec(int col, int row);
int cell_byte_cur(int col, int row, int off);
void cell_put(int col, int row, int off, int value);
int cell_word_cur(int col, int row, int off);
void cell_word_put(int col, int row, int off, int value);
int cell_word_raw(int col1, int row1, int off);
void cell_word_raw_put(int col1, int row1, int off, int value);
int cave_flag_get(int idx);
void cave_flag_set(int idx, int value);
int cell_solid_cur(int col, int row);
int cell_solid_px(int16_t x, int16_t y);
int cell_special_px(int16_t x, int16_t y);
int cell_solid_at(int32_t x, int32_t y);
int cell_border_byte(int col, int row, int off);
void cell_border_put(int col, int row, int off, int value);
int cell_border_word(int col, int row, int woff);
void cell_border_word_put(int col, int row, int woff, int value);
int cell_w0_is_dig(int col, int row);
void dig_cell(int col, int row);
int cell_type_collectible(int col, int row);
int cell_test_200fe(int x, int y);
int cell_blocked_4145(int col, int row);

#endif /* GAME_CELL_OPS_H */
