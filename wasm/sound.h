#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 6: exact Crystal Caves Sound driver state.
 *
 * The original 0x1F9A:0x21F play_sound routine selects a bank/slot by ID,
 * checks the [0x513E] enable flag and the [0x513F] playing flag, and applies
 * the record priority from +600.  The INT 1Ch ISR at 0x0A3F advances one note
 * per 140.06 Hz tick and clears [0x513F] at the 0xFFFF terminator.
 *
 * The C driver owns selection/priority/lifetime.  Audio rendering lives in
 * JS (wasm/sound_synth.js + wasm/audio.js); cc_sound_play() emits accepted
 * IDs to a small event queue consumed by game.html.
 */

void cc_sound_init(void);
void cc_sound_reset(void);              /* stop + clear pending events */
void cc_sound_set_enabled(int on);      /* [0x513E] */
int  cc_sound_enabled(void);

/* Raw attempt logging + callback, implemented in objects.c.  Every call to
 * the original play_sound (gameplay or shell) is recorded here. */
void cc_sound_note(uint16_t id);

/* Exact 0x1FBBF selection.  Returns 1 when the sound was accepted/started. */
int  cc_sound_play(uint16_t id);

/* Advance one 140.06 Hz sound-driver tick. */
void cc_sound_tick(void);

/* JS onended safety: clear only if this ID is still current. */
void cc_sound_end(uint16_t id);

/* Force [0x513F] = 0 (level exit/load/restore/quit). */
void cc_sound_stop(void);

int      cc_sound_playing(void);
uint16_t cc_sound_current_id(void);
uint16_t cc_sound_current_priority(void);
uint16_t cc_sound_current_bank(void);
uint16_t cc_sound_current_slot(void);
uint16_t cc_sound_current_ticks(void);
uint32_t cc_sound_unknown_hits(void);

/* Accepted-event queue for the host audio layer. */
int      cc_sound_event_count(void);
uint16_t cc_sound_event_at(int i);
void     cc_sound_event_clear(void);

#ifdef __cplusplus
}
#endif

#endif
