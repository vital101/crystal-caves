/* Crystal Caves sound effect IDs: the 36 records of the three CC1-*.SND
 * banks, as captured by tools/sound_fixtures.py (bank/priority/tick tables
 * live in notes/sound.md).  A name describes the *call site* that plays the
 * effect in the original routines, not an interpretation of the waveform;
 * the comment names the routine.  IDs whose meaning is not evidenced keep
 * a neutral name. */
#ifndef CC_SOUND_IDS_H
#define CC_SOUND_IDS_H

enum {
    SND_JUMP            = 0x01, /* jump take-off (0xE2E1 jump block clears) */
    /* ID 0x02: bank entry with no original call site (see notes/sound.md) */
    SND_EXIT_FOUND      = 0x03, /* exit cell armed, checkpoint copied */
    SND_EXIT_LOCKED     = 0x04, /* exit still locked: "more crystals" gate */
    SND_HURT            = 0x05, /* gas/hazard damage (0xBB26 hazard, 0xE51D) */
    SND_HIT             = 0x06, /* attack connects / victim dissolves (0xCF6C path) */
    SND_PLAYER_DRAW     = 0x07, /* animated draw effect while hit/transform state */
    SND_CRYSTAL         = 0x08, /* crystal collected; also the 0x2C defeat bonus */
    SND_PICKUP          = 0x09, /* generic collectible collected (0xBB26) */
    SND_BLOCK_POP       = 0x0A, /* ceiling block punched out (0xE5D3) */
    SND_PRIZE_CELL      = 0x0B, /* key-gated prize cell pays a random award */
    SND_NEW_ITEM        = 0x0C, /* a fresh collectible appears elsewhere */
    SND_HIT_2C          = 0x0D, /* slash connects with a kind-0x2C actor */
    SND_SHOT            = 0x0E, /* normal shot fired (0xCF6C) */
    SND_POWER_SHOT      = 0x0F, /* power shot fired (0xCF6C) */
    SND_COMPANION       = 0x10, /* companion retargets to follow its leader */
    SND_DIG             = 0x11, /* dig_cell(): solid cell carved away */
    SND_STUN            = 0x12, /* player stunned by the 0x19/0x0E cell */
    SND_GRAVITY         = 0x13, /* gravity-flip cell collected */
    SND_DOOR            = 0x14, /* passage switches; walking in/out of exits */
    SND_SWITCH_TOGGLE   = 0x15, /* ON/OFF switch cells 0x11/0x13/0x15 engaged */
    SND_MUSHROOM        = 0x16, /* mushroom cells 0x31/0x2D collected */
    SND_MULTIPART_MOVE  = 0x17, /* multi-part movers start/reverse/catch */
    SND_MOVER_STOP      = 0x18, /* mover reaches a solid cell, arms (ready) */
    SND_MOVER_WAKE      = 0x19, /* parked proximity mover starts moving */
    SND_FINALE_FANFARE  = 26,   /* finale: shell holds g_wait_sound while it plays */
    SND_BIG_GEM         = 0x1B, /* 10000-point gem collected */
    SND_FALLING_BLOCK   = 0x1C, /* dug-block object starts to fall */
    SND_POISON          = 0x1D, /* poison mushroom cell collected */
    SND_SHOT_DRY        = 0x1E, /* fire pressed with no ammo and no power */
    SND_SWITCH_ACTIVATE = 0x1F, /* 0x3904 switch cell mid-animation */
    SND_FANFARE         = 0x20, /* all crystals collected; shell ending screen */
    /* shell-side effects (recovered statically, notes/sound.md): */
    SND_MENU_ACCEPT     = 33,   /* main menu N / Space / Enter (new game) */
    SND_HISCORE_NAME    = 34,   /* high-score name entry */
    SND_TEXT_PAGE       = 35,   /* text window page rendered */
    SND_LEVEL_START     = 36,   /* level loaded (0xB1A9 return) */
};

#endif /* CC_SOUND_IDS_H */
