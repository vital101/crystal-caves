/* Native golden-master digest harness: a C port of wasm/test_refactor_digest.js.
 *
 *   cc_digest <group> [out.json]
 *
 * where <group> is one of: static_render, gameplay, transitions, warp, shell,
 * save_round_trip.  Each group runs in a fresh process (fresh static state,
 * like a fresh WebAssembly.Instance in the JS harness) and emits the same
 * JSON structure as the corresponding key in notes/refactor_digests.json.
 * native/compare_digests.js assembles the group files and compares them
 * against the committed fixture, so the native build is held to the same
 * 929 SHA-256 snapshots as the wasm build.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "objects.h"
#include "shell.h"
#include "render.h"
#include "text.h"
#include "sound.h"
#include "cc_host.h"
#include "assets_embed.h"
#include "sha256.h"

#define W 320
#define H 200
#define TEX_W 640
#define TEX_H 400
#define STATE_FRAMES 480
#define SNAPSHOT_EVERY 10

/* --------------------------------------------------------- string buffer */

typedef struct { char *p; size_t len, cap; } sb;

static void sb_init(sb *b) { b->cap = 1 << 16; b->len = 0; b->p = malloc(b->cap); b->p[0] = 0; }
static void sb_need(sb *b, size_t n) {
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap) b->cap *= 2;
        b->p = realloc(b->p, b->cap);
    }
}
static void sb_puts(sb *b, const char *s) {
    size_t n = strlen(s);
    sb_need(b, n);
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}
static void sb_printf(sb *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    sb_need(b, 4096);
    int n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n > 0) b->len += (size_t)n < b->cap - b->len ? (size_t)n : b->cap - b->len - 1;
}

/* JSON.stringify-compatible escaping for a string of code units 0..255
 * (the JS harness builds strings with String.fromCharCode(byte), so bytes
 * >= 0x80 become two-byte UTF-8 in the hashed/serialized output). */
static void sb_json_str(sb *b, const char *s, int maxlen) {
    sb_puts(b, "\"");
    for (int i = 0; s[i] && i < maxlen; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  sb_puts(b, "\\\""); break;
        case '\\': sb_puts(b, "\\\\"); break;
        case 8:    sb_puts(b, "\\b");  break;
        case 9:    sb_puts(b, "\\t");  break;
        case 10:   sb_puts(b, "\\n");  break;
        case 12:   sb_puts(b, "\\f");  break;
        case 13:   sb_puts(b, "\\r");  break;
        default:
            if (c < 0x20) sb_printf(b, "\\u%04x", c);
            else if (c < 0x80) { sb_need(b, 1); b->p[b->len++] = (char)c; b->p[b->len] = 0; }
            else { sb_printf(b, "%c%c", (char)(0xC0 | (c >> 6)), (char)(0x80 | (c & 63))); }
        }
    }
    sb_puts(b, "\"");
}

/* ------------------------------------------------------------------ core */

static unsigned char *g_stateP, *g_loadP;
static int g_stateSize;
static unsigned char g_statebuf[1 << 16];

static void boot(void) {
    cc_set_assets(cc_asset_gfx, cc_asset_levels);
    cc_set_cells(cc_asset_cells);
    cc_set_level_overlay(cc_asset_overlay);
    cc_set_screens(cc_asset_screens);
    cc_text_set_fonts(cc_asset_mni_f1, cc_asset_mni_f2, cc_asset_mni_spl);
    cc_set_palette(cc_palette32);
    cc_sound_init();
    cc_sound_set_enabled(0);
    cc_sound_log_clear();
    g_stateSize = cc_state_size();
    g_stateP = cc_alloc((unsigned)g_stateSize);
    g_loadP = cc_alloc((unsigned)g_stateSize);
    if (!g_stateP || !g_loadP) { fprintf(stderr, "alloc failed\n"); exit(1); }
}

static int state_bytes(unsigned char *out, int cap) {
    int n = cc_state_save(g_stateP, g_stateSize);
    if (!(n > 0 && n <= cap)) {
        fprintf(stderr, "cc_state_save failed (%d)\n", cc_state_last_error());
        exit(1);
    }
    memcpy(out, g_stateP, (size_t)n);
    return n;
}

static void state_digest(char out[65]) {
    int n = state_bytes(g_statebuf, sizeof(g_statebuf));
    cc_sha256_hex(g_statebuf, (unsigned)n, out);
}
static void view_digest(char out[65]) {
    cc_sha256_hex(cc_view_framebuffer(), W * H * 4, out);
}
static void texture_digest(char out[65]) {
    cc_sha256_hex(cc_framebuffer(), TEX_W * TEX_H * 4, out);
}
static void sound_digest(char out[65]) {
    /* Drain the attempt log; hash count + total + IDs exactly like the JS. */
    int n = cc_sound_log_count();
    uint32_t total = (uint32_t)cc_sound_log_total();
    unsigned char *buf = malloc(8 + (size_t)n * 2);
    buf[0] = n & 0xFF; buf[1] = (n >> 8) & 0xFF; buf[2] = (n >> 16) & 0xFF; buf[3] = (n >> 24) & 0xFF;
    buf[4] = total & 0xFF; buf[5] = (total >> 8) & 0xFF; buf[6] = (total >> 16) & 0xFF; buf[7] = (total >> 24) & 0xFF;
    for (int i = 0; i < n; i++) {
        uint16_t id = cc_sound_log_at(i);
        buf[8 + i * 2] = id & 0xFF;
        buf[8 + i * 2 + 1] = (id >> 8) & 0xFF;
    }
    cc_sound_log_clear();
    cc_sha256_hex(buf, (unsigned)(8 + n * 2), out);
    free(buf);
}

static void read_cstring(const char *p, char *out, int max) {
    int i;
    for (i = 0; i < max && p[i]; i++) out[i] = p[i];
    out[i] = 0;
}

/* JSON.stringify of the shell summary, in key insertion order, then hashed. */
static void shell_digest(char out[65]) {
    sb b;
    sb_init(&b);
    int pageCount = cc_shell_page_count();
    int pageLineCount = cc_shell_page_line_count();
    char name[256];
    sb_printf(&b, "{\"state\":%d,\"pageCount\":%d,\"pageIndex\":%d,"
                 "\"pageLines\":%d,\"message\":%d,\"requestPending\":%d,"
                 "\"requestKind\":%d,\"requestSlot\":%d,\"level\":%d,\"score\":%u,"
                 "\"hiscores\":[",
             cc_shell_state(), pageCount, cc_shell_page_index(), pageLineCount,
             cc_shell_message_block(), cc_shell_request_pending(),
             cc_shell_request_kind(), cc_shell_request_slot(),
             (int)cc_cur_level(), cc_score());
    int hc = cc_shell_hiscore_count();
    for (int i = 0; i < hc; i++) {
        read_cstring(cc_shell_hiscore_name(i), name, 256);
        if (i) sb_puts(&b, ",");
        sb_puts(&b, "[");
        sb_json_str(&b, name, 256);
        sb_printf(&b, ",%u]", cc_shell_hiscore_score(i));
    }
    sb_puts(&b, "],\"lines\":[");
    for (int i = 0; i < pageLineCount; i++) {
        read_cstring(cc_shell_page_line(i), name, 256);
        if (i) sb_puts(&b, ",");
        sb_json_str(&b, name, 256);
    }
    sb_puts(&b, "]}");
    cc_sha256_hex(b.p, (unsigned)b.len, out);
    free(b.p);
}

static void render_game(void) {
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
}
static void render_shell(void) { cc_shell_render(); }
static void render(void) {
    if (cc_shell_state() == 4 || cc_shell_overlay()) render_game();
    else render_shell();
}

static void enter_game_shell(void) {
    cc_shell_init();
    cc_shell_key(13);                   /* logo -> title/credits */
    cc_shell_key(13);                   /* title -> main menu */
    cc_shell_key('n');                  /* new game -> intro story */
    for (int i = 0; i < 8 && cc_shell_state() == 3; i++)
        cc_shell_key(13);
    if (cc_shell_state() != 4) {
        fprintf(stderr, "enterGameShell: shell state %d\n", cc_shell_state());
        exit(1);
    }
}

/* ------------------------------------------------------------- inputs */

static const struct { const char *name; int flag; } INPUT_FLAGS[4] = {
    { "left", 0x338E }, { "right", 0x338F }, { "jump", 0x338B }, { "fire", 0x3390 },
};
static const struct { int count; int right, jump, fire, left; } PHASES[8] = {
    { 60, 0, 0, 0, 0 }, { 60, 1, 0, 0, 0 }, { 60, 1, 1, 0, 0 },
    { 60, 1, 0, 1, 0 }, { 60, 0, 0, 0, 1 }, { 60, 0, 1, 0, 1 },
    { 60, 0, 0, 1, 1 }, { 60, 0, 0, 0, 0 },
};

static void apply_inputs(int frame) {
    int t = (frame - 1) % STATE_FRAMES;
    int fl[4] = { 0, 0, 0, 0 };
    for (int p = 0; p < 8; p++) {
        if (t < PHASES[p].count) {
            fl[0] = PHASES[p].left; fl[1] = PHASES[p].right;
            fl[2] = PHASES[p].jump; fl[3] = PHASES[p].fire;
            break;
        }
        t -= PHASES[p].count;
    }
    for (int i = 0; i < 4; i++) cc_input_set(INPUT_FLAGS[i].flag, fl[i]);
}

static void fail(const char *msg) {
    fprintf(stderr, "FAIL %s\n", msg);
    exit(1);
}

/* Emit one snapshot object into b (no braces). */
static void snapshot(sb *b, int frame) {
    char s[65], v[65], snd[65];
    state_digest(s);
    view_digest(v);
    sound_digest(snd);
    sb_printf(b, "{\"frame\":%d,\"state\":\"%s\",\"view\":\"%s\",\"sound\":\"%s\"}",
              frame, s, v, snd);
}

static void level_name(char *out, int level) {
    sprintf(out, "level%02d", level);
}

/* ------------------------------------------------------------ scenarios */

static void run_static_render(sb *out) {
    boot();
    char name[16], tex[65];
    sb_puts(out, "{");
    for (int level = 0; level < 19; level++) {
        cc_objects_init(level);
        cc_render_cells(level, cc_framebuffer());
        texture_digest(tex);
        level_name(name, level);
        if (level) sb_puts(out, ",");
        sb_printf(out, "\"%s\":\"%s\"", name, tex);
    }
    sb_puts(out, "}");
}

static void run_gameplay(sb *out) {
    boot();
    enter_game_shell();
    char name[16];
    sb_puts(out, "{");
    for (int level = 2; level <= 18; level++) {
        cc_objects_init(level);
        cc_sound_log_clear();
        cc_hud_tick();
        render_game();
        level_name(name, level);
        if (level > 2) sb_puts(out, ",");
        sb_printf(out, "\"%s\":[", name);
        snapshot(out, 0);
        for (int f = 1; f <= STATE_FRAMES; f++) {
            apply_inputs(f);
            cc_objects_step_full();
            cc_player_draw_sound();
            cc_hud_tick();
            if (f % SNAPSHOT_EVERY == 0) {
                render_game();
                sb_puts(out, ",");
                snapshot(out, f);
            }
        }
        if (cc_unknown_handler_hits() != 0) fail("unknown handlers");
        if (cc_sound_unknown_hits() != 0) fail("unknown sounds");
        sb_puts(out, "]");
    }
    sb_puts(out, "}");
}

static void find_door_cells(int doors[17][2]) {
    cc_objects_init(2);
    memset(doors, 0, sizeof(int) * 17 * 2);
    for (int r = 0; r < 25; r++)
        for (int c = 0; c < 40; c++) {
            int w0 = (cc_cell_byte(c, r, 0x00) & 0xFF) |
                     ((cc_cell_byte(c, r, 0x01) & 0xFF) << 8);
            if (w0 >= 1 && w0 <= 16) { doors[w0][0] = c; doors[w0][1] = r; }
        }
    for (int id = 1; id <= 16; id++)
        if (!doors[id][0] && !doors[id][1]) fail("hub door cell not found");
}

static int find_exit_cell(int *ec, int *er) {
    for (int r = 0; r < 25; r++)
        for (int c = 0; c < 40; c++) {
            int w3 = (cc_cell_byte(c, r, 0x06) & 0xFF) |
                     ((cc_cell_byte(c, r, 0x07) & 0xFF) << 8);
            int w5 = (cc_cell_byte(c, r, 0x0A) & 0xFF) |
                     ((cc_cell_byte(c, r, 0x0B) & 0xFF) << 8);
            if (w5 == 0x0C && (w3 == 0x11 || w3 == 0x0D)) { *ec = c; *er = r; return 1; }
        }
    return 0;
}

static void run_transitions(sb *out) {
    boot();
    enter_game_shell();
    int doors[17][2];
    find_door_cells(doors);
    char key[32];
    sb_puts(out, "{");
    int first = 1;
    for (int id = 1; id <= 16; id++) {
        int dc = doors[id][0], dr = doors[id][1];
        cc_objects_init(2);
        cc_sound_log_clear();
        cc_global_set(0x4611, dc * 16 + 5);
        cc_global_set(0x4613, dr * 16);
        int frames = 0;
        for (; frames < 8; frames++) {
            cc_objects_step_full();
            if (cc_cur_level() != 2) break;
        }
        if (cc_cur_level() != id + 2) fail("hub door loaded wrong level");
        render_game();
        if (!first) sb_puts(out, ",");
        first = 0;
        sprintf(key, "hub_to_cave_%02d", id);
        sb_printf(out, "\"%s\":", key);
        snapshot(out, frames + 1);

        int ec, er;
        if (!find_exit_cell(&ec, &er)) fail("cave exit cell not found");
        cc_global_set(0x4611, ec * 16 + 3);
        cc_global_set(0x4613, er * 16);
        cc_global_set(0xA0F5, 1);   /* exit unlocked */
        cc_global_set(0x513C, 1);   /* interaction enabled */
        cc_global_set(0xA0BE, id);
        frames = 0;
        for (; frames < 24; frames++) {
            cc_objects_step_full();
            if (cc_cur_level() == 2) break;
        }
        if (cc_cur_level() != 2) fail("cave exit loaded wrong level");
        render_game();
        sb_puts(out, ",");
        sprintf(key, "cave_%02d_to_hub", id);
        sb_printf(out, "\"%s\":", key);
        snapshot(out, frames + 1);
    }
    sb_puts(out, "}");
}

static void run_warp(sb *out) {
    boot();
    enter_game_shell();
    char name[16];
    sb_puts(out, "{");
    for (int level = 0; level < 19; level++) {
        cc_objects_init(2);
        cc_warp_level(level);
        if (cc_cur_level() != level) fail("warp loaded wrong level");
        render_game();
        level_name(name, level);
        if (level) sb_puts(out, ",");
        sb_printf(out, "\"%s\":", name);
        snapshot(out, 1);
    }
    sb_puts(out, "}");
}

static void shell_checkpoint(sb *out, const char *name, int *first) {
    char s[65], v[65], sh[65], snd[65];
    render();
    state_digest(s);
    view_digest(v);
    shell_digest(sh);
    sound_digest(snd);
    if (!*first) sb_puts(out, ",");
    *first = 0;
    sb_printf(out, "\"%s\":{\"state\":\"%s\",\"view\":\"%s\",\"shell\":\"%s\",\"sound\":\"%s\"}",
              name, s, v, sh, snd);
}

static void expect_state(int want, const char *name) {
    if (cc_shell_state() != want) {
        fprintf(stderr, "FAIL %s: shell state %d, expected %d\n",
                name, cc_shell_state(), want);
        exit(1);
    }
}

static void run_shell(sb *out) {
    boot();
    cc_shell_init();
    sb_puts(out, "{");
    int first = 1;
    char name[32];
    const char *keyname;

    expect_state(0, "logo");
    shell_checkpoint(out, "logo", &first);

    cc_shell_key(13);
    expect_state(15, "credits");
    shell_checkpoint(out, "credits", &first);

    cc_shell_step_ticks(433 + 400);
    shell_checkpoint(out, "credits_scrolled", &first);

    cc_shell_key(13);
    expect_state(2, "menu");
    shell_checkpoint(out, "menu", &first);

    cc_shell_key('n');
    expect_state(3, "intro_story");
    shell_checkpoint(out, "intro_story", &first);

    for (int i = 0; i < 8 && cc_shell_state() == 3; i++) {
        cc_shell_key(13);
        sprintf(name, "intro_page_%d", i + 1);
        shell_checkpoint(out, name, &first);
    }
    expect_state(4, "intro_done");
    if (cc_cur_level() != 2) fail("intro loaded wrong level");
    shell_checkpoint(out, "intro_done", &first);

    cc_shell_key('p');
    expect_state(5, "pause");
    shell_checkpoint(out, "pause", &first);
    cc_shell_key('x');
    expect_state(4, "pause_resumed");
    shell_checkpoint(out, "pause_resumed", &first);

    cc_shell_key(27);
    expect_state(6, "help");
    shell_checkpoint(out, "help", &first);

    cc_shell_key('k');
    expect_state(16, "key_redefine");
    shell_checkpoint(out, "key_redefine", &first);
    {
        const int scans[4] = { 0x1E, 0x20, 0x39, 0x1C };
        for (int i = 0; i < 4; i++) cc_shell_key_scan(scans[i]);
    }
    expect_state(17, "key_confirm");
    shell_checkpoint(out, "key_confirm", &first);
    cc_shell_key_scan(0x15);   /* Y commits the bindings */
    expect_state(6, "key_committed");
    shell_checkpoint(out, "key_committed", &first);

    cc_shell_key('w');
    expect_state(10, "warp_prompt");
    shell_checkpoint(out, "warp_prompt", &first);
    cc_shell_key('1');
    cc_shell_key('6');
    cc_shell_key(13);
    expect_state(4, "warped");
    if (cc_cur_level() != 18) fail("warp 16 loaded wrong level");
    shell_checkpoint(out, "warped_level18", &first);

    cc_load_arg(0);
    if (cc_cur_level() != 2) fail("load_arg(0) loaded wrong level");
    shell_checkpoint(out, "back_to_hub", &first);

    /* Save/restore request handshake (host storage is not part of this). */
    cc_global_set(0x50A5, 4321);
    cc_shell_key(27);
    cc_shell_key('s');
    expect_state(8, "save_prompt");
    shell_checkpoint(out, "save_prompt", &first);
    cc_shell_key('0');
    if (cc_shell_request_pending() != 1 || cc_shell_request_kind() != 1 ||
        cc_shell_request_slot() != 0)
        fail("save request was not raised");
    shell_checkpoint(out, "save_request", &first);
    unsigned char saved[1 << 16];
    int saved_len = state_bytes(saved, sizeof(saved));
    cc_shell_request_done(1);
    expect_state(14, "save_done");
    shell_checkpoint(out, "save_done", &first);
    cc_shell_key(13);   /* "saved" message ignores Enter */
    cc_shell_key(27);
    expect_state(4, "after_save_message");
    shell_checkpoint(out, "after_save_message", &first);

    {
        char nm[8] = "TEST";
        cc_shell_hiscore_add(nm, 1234567);
        char got[256];
        read_cstring(cc_shell_hiscore_name(0), got, 256);
        if (strcmp(got, "TEST") || cc_shell_hiscore_score(0) != 1234567)
            fail("high-score insert did not land at row 0");
    }
    shell_checkpoint(out, "hiscore_added", &first);

    cc_global_set(0x50A5, 99);
    cc_shell_key(27);
    cc_shell_key('r');
    expect_state(9, "restore_prompt");
    shell_checkpoint(out, "restore_prompt", &first);
    cc_shell_key('0');
    if (cc_shell_request_pending() != 1 || cc_shell_request_kind() != 2)
        fail("restore request was not raised");
    shell_checkpoint(out, "restore_request", &first);
    memcpy(g_loadP, saved, (size_t)saved_len);
    if (!cc_state_load(g_loadP, saved_len)) fail("state load failed");
    cc_shell_request_done(1);
    expect_state(4, "restored");
    if ((cc_global_get(0x50A5) & 0xFFFF) != 4321)
        fail("restore did not recover the saved score");
    shell_checkpoint(out, "restored", &first);

    /* Finale: all caves complete -> story pages -> ending -> high scores. */
    cc_global_set(0x50A5, 0);
    cc_global_set(0x50A7, 2);
    cc_global_set(0xA0FB, 1);
    cc_objects_step_full();
    cc_shell_step();
    expect_state(3, "finale_story");
    shell_checkpoint(out, "finale_story", &first);
    for (int i = 0; i < 20 && cc_shell_state() == 3; i++) {
        cc_shell_key(13);
        sprintf(name, "finale_page_%d", i + 1);
        shell_checkpoint(out, name, &first);
    }
    expect_state(13, "ending");
    shell_checkpoint(out, "ending", &first);
    cc_shell_key(13);
    if (cc_shell_state() != 12) fail("ending key did not reach name entry");
    keyname = "DIGEST";
    for (const char *p = keyname; *p; p++) cc_shell_key(*p);
    shell_checkpoint(out, "name_entry", &first);
    cc_shell_key(13);
    expect_state(11, "final_hiscore");
    {
        int digestRow = -1;
        for (int i = 0; i < cc_shell_hiscore_count(); i++) {
            char got[256];
            read_cstring(cc_shell_hiscore_name(i), got, 256);
            if (!strcmp(got, "DIGEST")) digestRow = i;
        }
        if (digestRow < 0 || cc_shell_hiscore_score(digestRow) != 131072)
            fail("name entry did not insert DIGEST with score 131072");
        char got[256];
        read_cstring(cc_shell_hiscore_name(0), got, 256);
        if (strcmp(got, "TEST")) fail("high-score order changed");
    }
    shell_checkpoint(out, "final_hiscore", &first);
    sb_puts(out, "}");
}

static void run_save_round_trip(sb *out) {
    boot();
    enter_game_shell();
    cc_objects_init(2);
    cc_sound_log_clear();

    for (int f = 1; f <= 120; f++) {
        apply_inputs(f);
        cc_objects_step_full();
        cc_player_draw_sound();
        cc_hud_tick();
    }
    render_game();
    char savedState[65], savedView[65];
    state_digest(savedState);
    view_digest(savedView);
    unsigned char savedBytes[1 << 16];
    int savedLen = state_bytes(savedBytes, sizeof(savedBytes));

    /* Mutate the live state and record every snapshot. */
    sb mutated_sb, replay_sb;
    sb_init(&mutated_sb);
    sb_init(&replay_sb);
    char mutState[65], mutView[65];
    int first = 1;
    for (int f = 121; f <= 180; f++) {
        apply_inputs(f);
        cc_objects_step_full();
        cc_player_draw_sound();
        cc_hud_tick();
        if ((f - 120) % SNAPSHOT_EVERY == 0) {
            render_game();
            if (!first) sb_puts(&mutated_sb, ",");
            first = 0;
            snapshot(&mutated_sb, f - 120);
        }
    }
    state_digest(mutState);
    view_digest(mutView);
    if (!strcmp(mutState, savedState)) fail("mutation did not change state");

    memcpy(g_loadP, savedBytes, (size_t)savedLen);
    if (!cc_state_load(g_loadP, savedLen)) fail("state load failed");
    render_game();
    char resState[65], resView[65];
    state_digest(resState);
    view_digest(resView);
    if (strcmp(resState, savedState)) fail("restored state hash differs");
    if (strcmp(resView, savedView)) fail("restored view hash differs");

    first = 1;
    for (int f = 121; f <= 180; f++) {
        apply_inputs(f);
        cc_objects_step_full();
        cc_player_draw_sound();
        cc_hud_tick();
        if ((f - 120) % SNAPSHOT_EVERY == 0) {
            render_game();
            if (!first) sb_puts(&replay_sb, ",");
            first = 0;
            snapshot(&replay_sb, f - 120);
        }
    }
    /* The JS compares the mutation and replay snapshot hashes pairwise. */
    if (strcmp(mutated_sb.p, replay_sb.p))
        fail("save round-trip: replay snapshots differ");

    sb_printf(out, "{\"saved\":{\"state\":\"%s\",\"view\":\"%s\"},"
                   "\"mutated\":{\"state\":\"%s\",\"view\":\"%s\"},"
                   "\"restored\":{\"state\":\"%s\",\"view\":\"%s\"},"
                   "\"replay\":[%s]}",
              savedState, savedView, mutState, mutView, resState, resView,
              replay_sb.p);
    free(mutated_sb.p);
    free(replay_sb.p);
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: cc_digest <group> [out.json]\n");
        return 2;
    }
    sb out;
    sb_init(&out);
    if (!strcmp(argv[1], "static_render")) run_static_render(&out);
    else if (!strcmp(argv[1], "gameplay")) run_gameplay(&out);
    else if (!strcmp(argv[1], "transitions")) run_transitions(&out);
    else if (!strcmp(argv[1], "warp")) run_warp(&out);
    else if (!strcmp(argv[1], "shell")) run_shell(&out);
    else if (!strcmp(argv[1], "save_round_trip")) run_save_round_trip(&out);
    else { fprintf(stderr, "unknown group %s\n", argv[1]); return 2; }

    if (argc > 2) {
        FILE *f = fopen(argv[2], "wb");
        if (!f) { perror("fopen"); return 1; }
        fwrite(out.p, 1, out.len, f);
        fclose(f);
    } else {
        fwrite(out.p, 1, out.len, stdout);
        fputc('\n', stdout);
    }
    free(out.p);
    return 0;
}
