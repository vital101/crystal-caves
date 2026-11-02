#ifndef GAME_MATH_H
#define GAME_MATH_H

#include <stdint.h>

void *memcpy_(void *d, const void *s, unsigned n);
int16_t wadd(int16_t a, int16_t b);
int16_t wsub(int16_t a, int16_t b);
int16_t wmul(int16_t a, int16_t b);
int16_t wneg(int16_t a);
int32_t tp_long_mul(int32_t a, int32_t b);
int32_t tp_long_div(int32_t a, int32_t b);
void score_add(uint16_t lo_add, uint16_t hi_add);

#endif /* GAME_MATH_H */
