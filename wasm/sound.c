/* Phase 6: Crystal Caves Sound driver state.
 *
 * Exact port of the observable state in 0x1F9A:0x21F (play_sound) and the
 * INT 1Ch ISR at 0x0A3F.  No audio is generated here; the host consumes
 * accepted IDs and renders the exported per-tick trace.
 */
#include "sound.h"
#include "sound_data.h"
#include "objects.h"

#define SOUND_EVENT_MAX 16

static int      s_enabled = 1;
static uint16_t s_current_id;
static uint16_t s_current_priority;
static uint16_t s_remaining_ticks;
static uint16_t s_current_bank;
static uint16_t s_current_slot;
static uint32_t s_unknown_hits;

static uint16_t s_events[SOUND_EVENT_MAX];
static int      s_event_count;

/* Host-only sound attempt log/callback.  Deliberately outside cc_state_t so
 * the deterministic save payload never contains pointers or host state. */
void (*g_sound_callback)(uint16_t id);
uint16_t g_sound_log[256];
uint16_t g_sound_log_count;
uint32_t g_sound_log_total;

/* Raw attempt log + host callback.  This is deliberately separate from the
 * Phase 6 driver state below: the existing combat/cell/handler fixtures
 * compare the exact sequence of IDs passed to 0x1F9A:0x21F, regardless of
 * the sound-enable/priority decision inside the routine. */
void cc_sound_note(uint16_t id) {
    if (g_sound_log_count < 256)
        g_sound_log[g_sound_log_count++] = id;
    g_sound_log_total++;
    if (g_sound_callback) g_sound_callback(id);
}

void cc_sound_set_callback(void (*cb)(uint16_t id)) {
    g_sound_callback = cb;
}
void cc_sound_log_clear(void) {
    g_sound_log_count = 0;
    g_sound_log_total = 0;
}
int cc_sound_log_count(void) { return g_sound_log_count; }
int cc_sound_log_total(void) { return (int)g_sound_log_total; }
uint16_t cc_sound_log_at(int i) {
    return (i >= 0 && i < g_sound_log_count) ? g_sound_log[i] : 0;
}

static int sound_valid(uint16_t id) {
    return id >= 1 && id <= CC_SOUND_COUNT;
}

void cc_sound_init(void) {
    s_enabled = 1;
    cc_sound_reset();
    s_unknown_hits = 0;
}

void cc_sound_reset(void) {
    s_current_id = 0;
    s_current_priority = 0;
    s_remaining_ticks = 0;
    /* Startup state: [0xA0BC] and [0x5136] are 1 (no sound selected yet). */
    s_current_bank = 1;
    s_current_slot = 1;
    s_event_count = 0;
}

void cc_sound_set_enabled(int on) {
    s_enabled = on ? 1 : 0;
}

int cc_sound_enabled(void) { return s_enabled; }

int cc_sound_play(uint16_t id) {
    if (!sound_valid(id)) {
        s_unknown_hits++;
        return 0;
    }

    /* Record the attempt even when disabled, exactly like the call sites in
     * the original: [0x513E] is checked inside 0x1FBBF after the call. */
    cc_sound_note(id);

    if (!s_enabled) return 0;

    /* [0x513F] == 0: always accept.  Otherwise compare priorities with the
     * original signed `cmp/jl`; equal priority preempts. */
    if (s_remaining_ticks != 0 &&
        cc_sound_priority[id] < s_current_priority)
        return 0;

    s_current_id = id;
    s_current_priority = cc_sound_priority[id];
    s_current_bank = cc_sound_bank[id];
    s_current_slot = cc_sound_slot[id];
    s_remaining_ticks = cc_sound_ticks[id];

    if (s_event_count < SOUND_EVENT_MAX)
        s_events[s_event_count++] = id;
    /* If the host has not drained events yet, dropping is preferable to
     * overwriting a just-queued sound; >16 accepted sounds in one frame is
     * unreachable (the original has a single channel). */
    return 1;
}

void cc_sound_tick(void) {
    if (s_remaining_ticks == 0) return;
    s_remaining_ticks--;
    /* The ISR clears [0x513F] and resets the note index at the terminator.
     * Keep the selected bank/slot for diagnostics but report not playing. */
}

void cc_sound_end(uint16_t id) {
    if (s_remaining_ticks != 0 && s_current_id == id)
        s_remaining_ticks = 0;
}

void cc_sound_stop(void) {
    s_remaining_ticks = 0;
    /* Do not let a sound queued earlier in the same frame start after a
     * level transition/quit has stopped playback. */
    s_event_count = 0;
}

int cc_sound_playing(void) { return s_remaining_ticks != 0; }
uint16_t cc_sound_current_id(void) { return s_current_id; }
uint16_t cc_sound_current_priority(void) { return s_current_priority; }
uint16_t cc_sound_current_bank(void) { return s_current_bank; }
uint16_t cc_sound_current_slot(void) { return s_current_slot; }
uint16_t cc_sound_current_ticks(void) { return s_remaining_ticks; }
uint32_t cc_sound_unknown_hits(void) { return s_unknown_hits; }

int cc_sound_event_count(void) { return s_event_count; }
uint16_t cc_sound_event_at(int i) {
    return (i >= 0 && i < s_event_count) ? s_events[i] : 0;
}
void cc_sound_event_clear(void) { s_event_count = 0; }
