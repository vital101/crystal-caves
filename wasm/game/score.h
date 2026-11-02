/* Crystal Caves score awards.  Every name is tied to the cell-event or
 * combat branch that pays it (0x0FC5:0xBB26 and the combat state machine);
 * the comment gives the exact branch that was verified. */
#ifndef CC_SCORE_H
#define CC_SCORE_H

enum {
    SCORE_CRYSTAL     = 0x32,   /* 50:   crystal cell, gravity cell */
    SCORE_GEM_SMALL   = 0x64,   /* 100:  gem cells w7=9..0x0C, w6=9 */
    SCORE_ITEM_800    = 0x320,  /* 800:  w7=0x0B, w6=1 item cell */
    SCORE_MUSHROOM    = 0x3E8,  /* 1000: w7=4/0x31 cells; prize shape 1 */
    SCORE_PRIZE       = 0x7D0,  /* 2000: prize cell shape 0 (effect 0x1B) */
    SCORE_GEM_5000    = 0x1388, /* 5000: w7=9/0x1C..0x27 cells; prize shape 2 */
    SCORE_GEM_10000   = 0x2710, /* 10000: w7=0x0D, w6=9 gem cell */
    SCORE_FULL_HEALTH = 0xC350, /* 50000: first exit-open with health == 3 */
};

#endif /* CC_SCORE_H */
