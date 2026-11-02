/* Crystal Caves shell - state machine core.
 *
 * Shell state, text-window page setup, story/message helpers, the input
 * cursor animation, step (PIT-tick) handling and the notify_* hooks.  The
 * original shell uses overlapping Turbo Pascal segments and DOS file I/O;
 * this module reimplements the observable flow with the same text blocks,
 * key letters, screens and save/restore restrictions.  The JS page owns
 * localStorage, canvas presentation and the browser key map.
 */
#include "shell_internal.h"

/* Input cursor animation (0x0FC5:0x0A18): F2 glyphs 45..49 at the '^'
 * marker cell, one frame every 21 PIT ticks (the original compares
 * [0x2B60] > 0x14). */
#define CURSOR_TICKS 21

int g_shell_state;
int g_timer;
int g_return_state;
int g_wait_sound;       /* finale ID 26: hold until [0x513F] clears */
int g_pending_ending;   /* enter CC1.END after the ID 26 fanfare */
int g_story_kind;       /* 0 none, 1 intro, 2 finale */
int g_story_level;      /* -1 = no level background */
int g_finale_pending;
unsigned int g_pending_score;

int g_cursor_ticks;
int g_cursor_frame;

TextPage g_pages[MAX_PAGES];
int g_page_count;
int g_page_index;
const ShellPageDef *g_story_pages;
int g_story_page_count;

int slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
void scopy(char *d, const char *s, int cap) {
    int i = 0;
    if (!d || cap <= 0) return;
    while (s && s[i] && i < cap - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

void cursor_reset(void) {
    g_cursor_ticks = 0;
    g_cursor_frame = 0;
}

void cursor_step(int ticks) {
    if (ticks <= 0) return;
    g_cursor_ticks += ticks;
    while (g_cursor_ticks >= CURSOR_TICKS) {
        g_cursor_ticks -= CURSOR_TICKS;
        g_cursor_frame = (g_cursor_frame % 5) + 1;
    }
}

void clear_pages(void) {
    cursor_reset();
    g_page_count = 0;
    g_page_index = 0;
    g_story_pages = 0;
    g_story_page_count = 0;
}

void add_block_pages(int block) {
    int total = cc_text_block_lines(block);
    for (int start = 0; start < total && g_page_count < MAX_PAGES;
         start += PAGE_LINES) {
        g_pages[g_page_count].block = block;
        g_pages[g_page_count].start = start;
        g_page_count++;
    }
}

void set_pages(const int *blocks, int n, int return_state,
               int story_kind, int story_level) {
    clear_pages();
    for (int i = 0; i < n; i++) add_block_pages(blocks[i]);
    g_return_state = return_state;
    g_story_kind = story_kind;
    g_story_level = story_level;
    g_shell_state = CC_SHELL_TEXT;
    cursor_reset();
    if (story_level >= 0) cc_objects_init_silent(story_level);
    /* Text-window page effect (0x23); 0x10930 plays it after drawing a page. */
    cc_sound_play(35);
}

void set_story_pages(const ShellPageDef *pages, int n,
                     int return_state, int story_kind,
                     int story_level) {
    clear_pages();
    g_story_pages = pages;
    g_story_page_count = n;
    g_return_state = return_state;
    g_story_kind = story_kind;
    g_story_level = story_level;
    g_shell_state = CC_SHELL_TEXT;
    cursor_reset();
    if (story_level >= 0) cc_objects_init_silent(story_level);
    /* 0x10930: page/window effect. */
    cc_sound_play(35);
}

void show_block(int block, int return_state) {
    int one[1];
    one[0] = block;
    set_pages(one, 1, return_state, 0, -1);
}

/* ID 0x22: high-score display/entry screen. */
void enter_high_score(void) {
    g_shell_state = CC_SHELL_HISCORE;
    cc_sound_play(34);
}

/* ID 0x20: CC1.END ending screen. */
void enter_ending(void) {
    g_shell_state = CC_SHELL_ENDING;
    cc_sound_play(32);
}

/* 0xFB33: play the all-caves fanfare (ID 0x1A), wait for [0x513F] to clear,
 * then enter the ending screen. */
void begin_ending_fanfare(void) {
    cc_sound_play(26);
    if (cc_sound_playing()) {
        g_wait_sound = 1;
        g_pending_ending = 1;
    } else {
        enter_ending();
    }
}

void cc_shell_init(void) {
    g_shell_state = CC_SHELL_LOGO;
    g_timer = 0;
    cursor_reset();
    g_scroll_phase = 0;
    g_scroll_frame = 0;
    g_scroll_row = 0;
    g_scroll_dir = 1;
    g_scroll_ticks = 0;
    g_wait_sound = 0;
    g_pending_ending = 0;
    g_return_state = CC_SHELL_MENU;
    g_story_kind = 0;
    g_story_level = -1;
    g_finale_pending = 0;
    g_pending_score = 0;
    g_request_pending = 0;
    g_request_kind = 0;
    g_request_slot = -1;
    g_config_dirty = 0;
    clear_pages();
    for (int i = 0; i < HISCORE_COUNT; i++) {
        scopy(g_hiscores[i].name, g_hiscore_default_names[i],
              HISCORE_NAME_MAX + 1);
        g_hiscores[i].score = g_hiscore_default_scores[i];
    }
    g_name[0] = 0;
    g_name_len = 0;
    g_warp_value = -1;
    g_key_action = 0;
    g_keys[0] = 0x4B;   /* left arrow */
    g_keys[1] = 0x4D;   /* right arrow */
    g_keys[2] = 0x1D;   /* Ctrl */
    g_keys[3] = 0x38;   /* Alt */
    for (int i = 0; i < 4; i++) g_key_tmp[i] = g_keys[i];
    cc_shell_set_key(0, g_keys[0]);
    cc_shell_set_key(1, g_keys[1]);
    cc_shell_set_key(2, g_keys[2]);
    cc_shell_set_key(3, g_keys[3]);
    /* 0x1AAEB: the Apogee logo plays ID 32 before its 721-tick hold. */
    cc_sound_play(32);
}

int cc_shell_state(void) { return g_shell_state; }
int cc_shell_message_block(void) {
    if (g_shell_state != CC_SHELL_MESSAGE || g_page_count <= 0 ||
        g_page_index < 0 || g_page_index >= g_page_count) return -1;
    return g_pages[g_page_index].block;
}

static int prompt_block(void) {
    if (g_shell_state == CC_SHELL_VIDEO) return SHELL_TEXT_MENU_VIDEO;
    if (g_shell_state == CC_SHELL_QUIT_CONFIRM) return SHELL_TEXT_MENU_QUIT_DOS;
    if (g_shell_state == CC_SHELL_QUIT) return SHELL_TEXT_MENU_QUIT;
    if (g_shell_state == CC_SHELL_HELP) return SHELL_TEXT_MENU_HELP;
    return -1;
}

int cc_shell_page_count(void) {
    return g_story_pages ? g_story_page_count : g_page_count;
}
int cc_shell_page_index(void) { return g_page_index; }
int cc_shell_page_line_count(void) {
    int pb = prompt_block();
    if (pb >= 0) return g_shell_text_blocks[pb].count;
    if (g_story_pages) {
        if (g_page_index < 0 || g_page_index >= g_story_page_count) return 0;
        return g_story_pages[g_page_index].count;
    }
    if (g_page_index < 0 || g_page_index >= g_page_count) return 0;
    return PAGE_LINES;
}
const char *cc_shell_page_line(int index) {
    int pb = prompt_block();
    if (pb >= 0) {
        if (index < 0 || index >= g_shell_text_blocks[pb].count) return "";
        return g_shell_text_blocks[pb].lines[index];
    }
    if (g_story_pages) {
        if (g_page_index < 0 || g_page_index >= g_story_page_count)
            return "";
        const ShellPageDef *p = &g_story_pages[g_page_index];
        if (index < 0 || index >= p->count) return "";
        return p->lines[index];
    }
    if (g_page_index < 0 || g_page_index >= g_page_count) return "";
    int block = g_pages[g_page_index].block;
    int start = g_pages[g_page_index].start;
    if (block < 0 || block >= SHELL_TEXT_COUNT) return "";
    const ShellTextBlock *b = &g_shell_text_blocks[block];
    if (start + index >= b->count) return "";
    return b->lines[start + index];
}

int cc_shell_hud_active(void) { return g_shell_state == CC_SHELL_GAME; }
int cc_shell_overlay(void) {
    if (g_shell_state == CC_SHELL_PAUSE || g_shell_state == CC_SHELL_HELP ||
        g_shell_state == CC_SHELL_QUIT || g_shell_state == CC_SHELL_SAVE ||
        g_shell_state == CC_SHELL_RESTORE || g_shell_state == CC_SHELL_WARP ||
        g_shell_state == CC_SHELL_MESSAGE) return 1;
    /* The video/quit prompts are modal over the in-game help/quit menus, but
     * full screens when opened from the main menu. */
    if (g_shell_state == CC_SHELL_VIDEO || g_shell_state == CC_SHELL_QUIT_CONFIRM)
        return g_return_state == CC_SHELL_HELP ||
               g_return_state == CC_SHELL_QUIT ||
               g_return_state == CC_SHELL_GAME;
    return 0;
}

void cc_shell_message(int block_id) {
    if (g_shell_state != CC_SHELL_GAME && g_shell_state != CC_SHELL_MESSAGE) return;
    show_block(block_id, CC_SHELL_GAME);
    /* messages are overlays on the game view */
    g_shell_state = CC_SHELL_MESSAGE;
    g_story_level = -1;
    g_story_kind = 0;
    clear_pages();
    add_block_pages(block_id);
}

void cc_shell_notify_last_crystal(void) {
    if (g_shell_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_LAST_CRYSTAL);
}

void cc_shell_notify_gravity(void) {
    if (g_shell_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_GRAVITY);
}

void cc_shell_notify_power_shot(void) {
    if (g_shell_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_POWER_SHOT);
}

void cc_shell_notify_red_mushroom(void) {
    if (g_shell_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_RED_MUSHROOM);
}

void cc_shell_notify_poison(void) {
    if (g_shell_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_POISON);
}

void cc_shell_notify_stunned(void) {
    if (g_shell_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_STUNNED);
}

void cc_shell_notify_perfect_health(void) {
    if (g_shell_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_PERFECT_HEALTH);
}

void cc_shell_notify_more_crystals(void) {
    if (g_shell_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_MORE_CRYSTALS);
}

void cc_shell_notify_lever(void) {
    if (g_shell_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_LEVER);
}

void cc_shell_finale_done(void) {
    if (g_shell_state == CC_SHELL_GAME) {
        g_pending_score = cc_score();
        g_finale_pending = 1;
    }
}

void cc_shell_step(void) { cc_shell_step_ticks(1); }

void cc_shell_step_ticks(int ticks) {
    if (ticks < 0) ticks = 0;
    /* 0xFB33: the all-caves fanfare is played and the sequence waits for
     * [0x513F] to clear before continuing. */
    if (g_wait_sound) {
        if (cc_sound_playing()) return;
        g_wait_sound = 0;
        if (g_pending_ending) {
            g_pending_ending = 0;
            enter_ending();
        }
    }
    g_timer += ticks;
    if (g_shell_state == CC_SHELL_LOGO) {
        if (g_timer > CC_SHELL_APOGEE_TICKS) {
            scroll_reset();
            g_timer = 0;
        }
    } else if (g_shell_state == CC_SHELL_CREDITS || g_shell_state == CC_SHELL_TITLE) {
        scroll_advance(ticks);
    } else if (g_shell_state == CC_SHELL_TEXT || g_shell_state == CC_SHELL_MESSAGE ||
               g_shell_state == CC_SHELL_PAUSE || g_shell_state == CC_SHELL_HELP ||
               g_shell_state == CC_SHELL_QUIT || g_shell_state == CC_SHELL_SAVE ||
               g_shell_state == CC_SHELL_RESTORE || g_shell_state == CC_SHELL_WARP ||
               g_shell_state == CC_SHELL_KEY_REDEFINE ||
               g_shell_state == CC_SHELL_KEY_CONFIRM ||
               g_shell_state == CC_SHELL_QUIT_CONFIRM ||
               g_shell_state == CC_SHELL_VIDEO || g_shell_state == CC_SHELL_DOS) {
        cursor_step(ticks);
    }
    if (g_finale_pending) {
        g_finale_pending = 0;
        if (g_shell_state == CC_SHELL_GAME) {
            set_story_pages(g_finale_pages, g_finale_page_count,
                            CC_SHELL_ENDING, 2, 1);
        }
    }
}
