#ifndef GAME_SAVE_H
#define GAME_SAVE_H

#include <stdint.h>

#include "state.h"

uint16_t rd16_(const unsigned char *p);
uint32_t rd32_(const unsigned char *p);
void wr16_(unsigned char *p, uint16_t v);
void wr32_(unsigned char *p, uint32_t v);
uint32_t crc32_(const unsigned char *p, int n);
int state_valid_(const cc_state_t *st);

#endif /* GAME_SAVE_H */
