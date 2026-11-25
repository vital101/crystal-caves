#ifndef CC_SYNTH_H
#define CC_SYNTH_H

#include <stdint.h>

/* Native port of wasm/sound_synth.js + wasm/audio.js: the C sound driver
 * emits accepted IDs; this renders the captured PIT trace with the same
 * integer arithmetic as CCSynth and plays one sound at a time (the original
 * single PC-speaker channel) through the SDL audio callback. */

typedef struct cc_synth cc_synth;

cc_synth *cc_synth_new(int sample_rate);
void      cc_synth_free(cc_synth *s);
void      cc_synth_set_enabled(cc_synth *s, int on);

/* Start rendering+playing trace `id` (preempts the current sound, like
 * audio.js play()).  Returns 1 if a sound was started. */
int  cc_synth_play(cc_synth *s, int id);
/* Stop the current sound immediately (audio.js stop()). */
void cc_synth_stop(cc_synth *s);

/* Raw rendered PCM (before the playback gain); *len receives the frame
 * count.  Valid after cc_synth_play(); used by --dump-sound. */
const int16_t *cc_synth_pcm(const cc_synth *s, int *len);

/* Audio callback body: write up to `frames` int16 mono samples into out.
 * When the current sound finishes, *ended_id is set to its ID (the host then
 * calls cc_sound_end(id), mirroring the WebAudio onended handler). */
void cc_synth_fill(cc_synth *s, int16_t *out, int frames, int *ended_id);

#endif
