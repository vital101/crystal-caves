/* Native port of wasm/sound_synth.js (integer PC-speaker trace renderer,
 * must match tools/sound_banks.py:synth_reference) plus the single-channel
 * play/stop semantics of wasm/audio.js. */
#include <stdlib.h>
#include <string.h>
/* <math.h> would resolve to wasm/game/math.h through the core's include
 * paths, so declare the two libm entry points the speaker filter needs. */
extern double sin(double);
extern double cos(double);
#include "synth.h"
#include "assets_embed.h"

#define PIT_HZ      1193181
#define TICK_DIVISOR 0x2147
#define AMPLITUDE   12000
#define VOLUME      0.7f   /* audio.js master gain */

/* Small-speaker playback model (matches sound_synth.js:speakerShape):
 * second-order Butterworth low-pass.  The raw trace renderer keeps the square
 * wave's harmonics to Nyquist; the original PC buzzer rolls them off. */
#define SPEAKER_LP_HZ 5500.0
#define SPEAKER_Q     0.7071067811865476

struct cc_synth {
    int sample_rate;
    int enabled;
    int16_t *pcm;      /* current sound, rendered */
    int pcm_len;
    int pcm_pos;
    int current_id;
};

/* CCSynth.renderInt16: identical integer phase accumulator. */
static int16_t *render_trace(const unsigned short *trace, int ticks,
                             int sample_rate, int *out_len) {
    long total = (long)((long)ticks * sample_rate * TICK_DIVISOR / PIT_HZ);
    if (total < 0) total = 0;
    int16_t *out = malloc((size_t)(total > 0 ? total : 1) * sizeof(int16_t));
    if (!out) { *out_len = 0; return 0; }
    uint32_t phase = 0;
    for (int k = 0; k < ticks; k++) {
        long start = (long)k * sample_rate * TICK_DIVISOR / PIT_HZ;
        long end   = (long)(k + 1) * sample_rate * TICK_DIVISOR / PIT_HZ;
        if (end > total) end = total;
        int divisor = trace[k * 2] | 0;
        int speaker = trace[k * 2 + 1] ? 1 : 0;
        /* JS: Math.floor(PIT_HZ * 2^32 / (divisor * sampleRate)); the
         * numerator fits in 64 bits (and in the JS double's 53-bit mantissa). */
        uint32_t inc = divisor
            ? (uint32_t)((uint64_t)PIT_HZ * 4294967296ULL / ((uint64_t)divisor * (unsigned)sample_rate))
            : 0;
        for (long i = start; i < end; i++) {
            if (divisor) phase += inc;
            if (speaker) out[i] = phase < 0x80000000u ? AMPLITUDE : -AMPLITUDE;
        }
    }
    *out_len = (int)total;
    return out;
}

/* Playback-only biquad (RBJ low-pass, Q = Butterworth).  Runs on the
 * rendered trace before the callback's gain, exactly like audio.js applies
 * CCSynth.speakerShape() before creating the buffer. */
static void speaker_shape(int16_t *pcm, int len, int rate) {
    if (len <= 0 || rate <= (int)(SPEAKER_LP_HZ * 2.0)) return;
    const double pi = 3.14159265358979323846;
    double w0 = 2.0 * pi * SPEAKER_LP_HZ / (double)rate;
    double cosw = cos(w0);
    double alpha = sin(w0) / (2.0 * SPEAKER_Q);
    double a0 = 1.0 + alpha;
    double b0 = (1.0 - cosw) / (2.0 * a0);
    double b1 = (1.0 - cosw) / a0;
    double a1 = -2.0 * cosw / a0;
    double a2 = (1.0 - alpha) / a0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    for (int i = 0; i < len; i++) {
        double x = (double)pcm[i] / 32768.0;
        double y = b0 * x + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
        double v = y * 32768.0;
        if (v > 32767.0) v = 32767.0;
        if (v < -32768.0) v = -32768.0;
        pcm[i] = (int16_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
    }
}

cc_synth *cc_synth_new(int sample_rate) {
    cc_synth *s = calloc(1, sizeof(*s));
    if (s) { s->sample_rate = sample_rate; s->enabled = 1; }
    return s;
}

void cc_synth_free(cc_synth *s) {
    if (!s) return;
    free(s->pcm);
    free(s);
}

void cc_synth_set_enabled(cc_synth *s, int on) {
    s->enabled = on ? 1 : 0;   /* exact [0x513E] semantics: never cut current */
}

int cc_synth_play(cc_synth *s, int id) {
    if (!s->enabled) return 0;
    const cc_snd_entry *e = 0;
    for (int i = 0; i < cc_snd_count; i++)
        if (cc_snd[i].id == id) { e = &cc_snd[i]; break; }
    if (!e) return 0;
    int len = 0;
    int16_t *pcm = render_trace(e->trace, e->ticks, s->sample_rate, &len);
    if (!pcm) return 0;
    speaker_shape(pcm, len, s->sample_rate);
    free(s->pcm);
    s->pcm = pcm;
    s->pcm_len = len;
    s->pcm_pos = 0;
    s->current_id = id;
    return 1;
}

const int16_t *cc_synth_pcm(const cc_synth *s, int *len) {
    *len = s ? s->pcm_len : 0;
    return s ? s->pcm : 0;
}

void cc_synth_stop(cc_synth *s) {
    s->pcm_pos = s->pcm_len;   /* exhausts the current sound on next fill */
    s->current_id = 0;
}

void cc_synth_fill(cc_synth *s, int16_t *out, int frames, int *ended_id) {
    *ended_id = 0;
    int n = 0;
    while (n < frames) {
        if (s->pcm && s->pcm_pos < s->pcm_len) {
            /* audio.js: float sample (amplitude/32768) through master 0.7 */
            float f = (float)s->pcm[s->pcm_pos] / 32768.0f * VOLUME;
            int v = (int)(f * 32767.0f);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out[n++] = (int16_t)v;
            s->pcm_pos++;
        } else {
            if (s->pcm && s->current_id) *ended_id = s->current_id;
            s->pcm_pos = s->pcm_len;
            out[n++] = 0;
        }
    }
}
