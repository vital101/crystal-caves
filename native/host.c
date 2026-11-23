/* Crystal Caves Episode 1 - native desktop host (Windows/macOS/Linux).
 *
 * Native port of wasm/game.js (+ wasm/game/*.js): same fixed-step loop (140.06 Hz PIT ticks,
 * 8 ticks per game frame), same render path (320x200 RGBA view framebuffer
 * blitted integer-scaled), same input model (DOS set-1 scan codes + shell key
 * values, gamepad axes/buttons), same save/restore handshake, same sound
 * event pump.  Persistence lives in the SDL pref dir instead of
 * localStorage (see persist.c).  Assets are embedded (assets_embed.c).
 *
 * Usage: caves [--smoke N]   (--smoke runs N headless frames, dummy drivers)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "objects.h"
#include "shell.h"
#include "render.h"
#include "text.h"
#include "sound.h"
#include "cc_host.h"
#include "assets_embed.h"
#include "keymap.h"
#include "persist.h"
#include "synth.h"

#define W 320
#define H 200

/* Original timing: one game frame is 8 PIT ticks at divisor 0x2147. */
static const double STEP_MS = 1000.0 * 8 * 0x2147 / 1193182.0; /* ~57.13 ms */
static const double TICK_MS = 1000.0 / 140.06;

static const int KEY_BACKSPACE = 8, KEY_ENTER = 13, KEY_ESC = 27,
                 KEY_SPACE = 32, KEY_F1 = 0x13B;

static const int JUMP_FLAG = 0x338B;
static const int JUMP_ACTION = 2;
static const int JUMP_ALIAS_SCANS[2] = { 0x39, 0x48 }; /* Space, Up */

static unsigned char *g_stateP;
static int g_stateSize;
static cc_synth *g_synth;
static SDL_AudioStream *g_audio;
static int g_sound_ended;

/* ------------------------------------------------------------ rendering */

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;

static void present(void) {
    SDL_UpdateTexture(g_texture, NULL, cc_view_framebuffer(), W * 4);
    SDL_RenderClear(g_renderer);
    SDL_RenderTexture(g_renderer, g_texture, NULL, NULL);
    SDL_RenderPresent(g_renderer);
}

static void renderGame(void) {
    int level = cc_cur_level();
    int camX = cc_cam_x(), camY = cc_cam_y();
    cc_render_view_cells(level, camX, camY);
    for (int s = 0; s < cc_obj_count(); s++) {
        if (cc_obj_state(s) == 2 || cc_obj_kind(s) == 0) continue;
        int blk = cc_obj_tile(s), sh = cc_obj_draw_shape(s);
        if (!blk || !sh) continue;
        cc_draw_object_view(blk, sh, cc_obj_x(s) - camX, cc_obj_y(s) - camY);
    }
    if (cc_shell_hud_active()) cc_hud_render_screen();
    if (cc_shell_overlay()) cc_shell_render();
    present();
}

static void renderShell(void) {
    cc_shell_render();
    present();
}

static void render(void) {
    if (cc_shell_state() == 4 || cc_shell_overlay()) renderGame();
    else renderShell();
}

/* ------------------------------------------------------- persistence glue */

static void persist_config(void) {
    cc_config c;
    c.version = 2;
    c.sound = cc_shell_sound_on();
    c.joystick = cc_shell_joystick_on();
    c.video = cc_shell_video_alt();
    for (int i = 0; i < 4; i++) c.keys[i] = cc_shell_key_for_action(i);
    cc_synth_set_enabled(g_synth, c.sound);
    cc_save_config(&c);
}

static void handle_request(void) {
    if (!cc_shell_request_pending()) return;
    int kind = cc_shell_request_kind();
    int slot = cc_shell_request_slot();
    int ok = 0;
    if (kind == 1)
        ok = cc_save_slot(slot, g_stateP, g_stateSize);
    else if (kind == 2)
        ok = cc_load_slot(slot, g_stateP, g_stateSize, 1);
    cc_shell_request_done(ok);
    render();
}

/* ---------------------------------------------------------------- input */

static int g_jumpHeld[0x80];   /* scan codes currently held for jump */
static int g_jumpCount;

static int is_jump_scan(int scan) {
    if (!scan || scan >= 0x80) return 0;
    if (scan == cc_shell_key_for_action(JUMP_ACTION)) return 1;
    for (int i = 0; i < 2; i++)
        if (scan == JUMP_ALIAS_SCANS[i] && cc_shell_action_for_scan(scan) < 0)
            return 1;
    return 0;
}

static void set_jump_source(int scan, int down) {
    if (scan < 0 || scan >= 0x80) return;
    if (down && !g_jumpHeld[scan]) { g_jumpHeld[scan] = 1; g_jumpCount++; }
    if (!down && g_jumpHeld[scan]) { g_jumpHeld[scan] = 0; g_jumpCount--; }
    cc_input_set(JUMP_FLAG, g_jumpCount ? 1 : 0);
}

static void release_jump(void) {
    memset(g_jumpHeld, 0, sizeof(g_jumpHeld));
    g_jumpCount = 0;
    cc_input_set(JUMP_FLAG, 0);
}

static void release_all(void) {
    release_jump();
    for (int i = 0; i < 4; i++)
        cc_input_scan(cc_shell_key_for_action(i), 0);
}

static void on_key_down(const SDL_KeyboardEvent *ev) {
    int st = cc_shell_state();
    int scan = cc_scan_from_sdl(ev->scancode);
    uint32_t k = ev->key;

    if (k == SDLK_F1) { cc_shell_key(KEY_F1); return; }
    if (k == SDLK_ESCAPE) { cc_shell_key(KEY_ESC); return; }
    if (st == 16 || st == 17) { /* keyboard redefine / confirm */
        cc_shell_key_scan(scan ? scan : (k == SDLK_SPACE ? 0x39 : 0));
        if (cc_shell_state() == 6) persist_config();
        return;
    }
    if (st == 4) { /* game */
        if (k == 'p' || k == 'P') { cc_shell_key(112); return; }
        if (scan) {
            if (is_jump_scan(scan)) set_jump_source(scan, 1);
            else cc_input_scan(scan, 1);
        }
        return;
    }
    if (st == 12) { /* name entry */
        int c = cc_keychar_from_sdl(k, ev->mod);
        if (c) cc_shell_key(c);
        else if (k == SDLK_RETURN) cc_shell_key(KEY_ENTER);
    } else if (k == SDLK_RETURN) {
        cc_shell_key(KEY_ENTER);
    } else if (k == SDLK_BACKSPACE) {
        cc_shell_key(KEY_BACKSPACE);
    } else {
        int c = cc_keychar_from_sdl(k, ev->mod);
        if (st == 3 || st == 14) cc_shell_key(c ? c : KEY_ENTER);
        else cc_shell_key(c ? c : (k == SDLK_SPACE ? KEY_ENTER : 0));
    }
    /* The original polls the live keyboard every frame, so an action key
     * pressed while a modal is up is already held when play resumes. */
    if (scan && st != 12) {
        if (is_jump_scan(scan)) set_jump_source(scan, 1);
        else cc_input_scan(scan, 1);
    }
    if (cc_shell_state() == 11 || cc_shell_state() == 12) cc_save_hiscores();
    if (cc_shell_config_dirty()) {
        persist_config();
        cc_shell_config_clear_dirty();
    }
}

static void on_key_up(const SDL_KeyboardEvent *ev) {
    int scan = cc_scan_from_sdl(ev->scancode);
    if (!scan) return;
    /* Always pass the release through, whatever the shell state. */
    if (is_jump_scan(scan) || (scan < 0x80 && g_jumpHeld[scan]))
        set_jump_source(scan, 0);
    else
        cc_input_scan(scan, 0);
}

/* ---------------------------------------------------------------- audio */

static void audio_callback(void *ud, SDL_AudioStream *stream,
                           int additional_bytes, int total_bytes) {
    (void)ud; (void)total_bytes;
    int frames = additional_bytes / 2;
    if (frames <= 0) return;
    int16_t *buf = malloc((size_t)frames * sizeof(int16_t));
    if (!buf) return;
    int ended = 0;
    cc_synth_fill(g_synth, buf, frames, &ended);
    if (ended) g_sound_ended = ended;   /* main thread calls cc_sound_end() */
    SDL_PutAudioStreamData(stream, buf, frames * 2);
    free(buf);
}

static void pump_sound(void) {
    if (g_sound_ended) {
        cc_sound_end((uint16_t)g_sound_ended);
        g_sound_ended = 0;
    }
    int n = cc_sound_event_count();
    for (int i = 0; i < n; i++) cc_synth_play(g_synth, cc_sound_event_at(i));
    if (n) cc_sound_event_clear();
    if (!cc_sound_playing()) cc_synth_stop(g_synth);
}

/* ------------------------------------------------------------ gamepad */

static SDL_Gamepad *g_pad;

static void poll_gamepad(void) {
    if (cc_shell_state() != 4 || !cc_shell_joystick_on() || !g_pad) return;
    float ax = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float ay = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    cc_input_set(0x338E, ax < -0.4f ? 1 : 0);
    cc_input_set(0x338F, ax > 0.4f ? 1 : 0);
    cc_input_set(0x338B, (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_SOUTH) ||
                          ay < -0.5f) ? 1 : 0);
    cc_input_set(0x3390, SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_WEST) ? 1 : 0);
}

/* ----------------------------------------------------------------- boot */

static int boot(void) {
    cc_set_assets(cc_asset_gfx, cc_asset_levels);
    cc_set_cells(cc_asset_cells);
    cc_set_level_overlay(cc_asset_overlay);
    cc_set_screens(cc_asset_screens);
    cc_text_set_fonts(cc_asset_mni_f1, cc_asset_mni_f2, cc_asset_mni_spl);
    cc_set_palette(cc_palette32);

    g_stateSize = cc_state_size();
    g_stateP = cc_alloc((unsigned)g_stateSize);
    if (!g_stateP) return 0;

    cc_sound_init();
    cc_shell_init();

    cc_persist_init();
    cc_config cfg = cc_load_config();
    cc_shell_set_config(cfg.sound, cfg.joystick, cfg.video);
    for (int i = 0; i < 4; i++) cc_shell_set_key(i, cfg.keys[i]);
    cc_synth_set_enabled(g_synth, cfg.sound);
    cc_load_hiscores();
    cc_save_config(&cfg);
    return 1;
}

/* ------------------------------------------------------------ main loop */

int main(int argc, char **argv) {
    int smoke = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--smoke") && i + 1 < argc) smoke = atoi(argv[++i]);
    }
    if (smoke > 0) {
#ifdef _WIN32
        _putenv_s("SDL_VIDEODRIVER", "dummy");
        _putenv_s("SDL_AUDIODRIVER", "dummy");
#else
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    cc_keymap_init();

    g_window = SDL_CreateWindow("Crystal Caves Episode 1",
                                W * 3, H * 3, SDL_WINDOW_RESIZABLE);
    if (!g_window) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    g_renderer = SDL_CreateRenderer(g_window, NULL);
    if (!g_renderer) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderLogicalPresentation(g_renderer, W, H,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32,
                                  SDL_TEXTUREACCESS_STREAMING, W, H);
    if (!g_texture) { fprintf(stderr, "texture: %s\n", SDL_GetError()); return 1; }

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = 1;
    spec.freq = 44100;
    g_synth = cc_synth_new(spec.freq);
    g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                        &spec, audio_callback, NULL);
    if (g_audio) SDL_ResumeAudioStreamDevice(g_audio);

    if (!boot()) { fprintf(stderr, "boot failed\n"); return 1; }

    int running = 1;
    double last = 0, accum = 0, soundAccum = 0;
    renderShell();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                running = 0;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!ev.key.repeat || 1) on_key_down(&ev.key);
                break;
            case SDL_EVENT_KEY_UP:
                on_key_up(&ev.key);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                release_all();
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!g_pad) g_pad = SDL_OpenGamepad(ev.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (g_pad && SDL_GetGamepadID(g_pad) == ev.gdevice.which) {
                    SDL_CloseGamepad(g_pad);
                    g_pad = 0;
                }
                break;
            default:
                break;
            }
        }

        double now = SDL_GetTicksNS() / 1000000.0;
        if (smoke) now = last + STEP_MS + 1.0;   /* fixed step in smoke mode */
        if (!last) last = now;
        double dt = now - last;
        last = now;
        if (dt > 250) dt = 250;

        /* The original sound ISR runs on the same 140.06 Hz PIT as the game
         * timer, independently of the 17.5 Hz game frame. */
        soundAccum += dt;
        int soundTicks = (int)(soundAccum / TICK_MS);
        if (soundTicks > 32) soundTicks = 32;
        soundAccum -= soundTicks * TICK_MS;
        handle_request();
        if (cc_shell_state() != 4 && g_jumpCount) release_jump();
        poll_gamepad();
        for (int i = 0; i < soundTicks; i++) {
            cc_sound_tick();
            if (cc_shell_state() != 4) cc_shell_step_ticks(1);
        }

        if (cc_shell_state() == 4) {
            cc_shell_step();
            accum += dt;
            int steps = 0;
            while (accum >= STEP_MS && steps < 4) {
                cc_objects_step_full();
                cc_player_draw_sound();
                cc_hud_tick();
                cc_shell_step();
                accum -= STEP_MS;
                steps++;
                if (cc_shell_state() != 4) break;
            }
            pump_sound();
            if (steps) renderGame();
        } else {
            pump_sound();
            if (cc_shell_overlay()) renderGame();
            else renderShell();
        }

        if (smoke && --smoke == 0) {
            /* Report a framebuffer hash so CI can see the host rendered. */
            const unsigned char *fb = cc_view_framebuffer();
            uint32_t h = 0x811c9dc5u;
            long nonblack = 0;
            for (int i = 0; i < W * H; i++) {
                for (int b = 0; b < 4; b++) h = (h ^ fb[i * 4 + b]) * 0x01000193u;
                if (fb[i * 4] || fb[i * 4 + 1] || fb[i * 4 + 2]) nonblack++;
            }
            printf("SMOKE OK: view fb hash %08x, nonblack %ld, state %d\n",
                   h, nonblack, cc_shell_state());
            running = 0;
        }
        if (!smoke) SDL_Delay(1);
    }

    if (g_pad) SDL_CloseGamepad(g_pad);
    if (g_audio) SDL_DestroyAudioStream(g_audio);
    cc_synth_free(g_synth);
    SDL_DestroyTexture(g_texture);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
