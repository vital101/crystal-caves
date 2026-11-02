/* Crystal Caves shell - key dispatch.
 *
 * cc_shell_key()/cc_shell_key_scan() per-state key handling, the text
 * window paging key, ASCII -> DOS scan-code compatibility, the key
 * redefine flow entry points and the debug warp command.
 */
#include "shell_internal.h"

static const int INSTRUCTION_BLOCKS[] = {
    SHELL_TEXT_INSTRUCTIONS_1, SHELL_TEXT_INSTRUCTIONS_2,
    SHELL_TEXT_INSTRUCTIONS_3, SHELL_TEXT_INSTRUCTIONS_3_2,
    SHELL_TEXT_INSTRUCTIONS_3_3, SHELL_TEXT_INSTRUCTIONS_3_4
};

int g_warp_value = -1;

static int lower(int k) {
    if (k >= 'A' && k <= 'Z') return k - 'A' + 'a';
    return k;
}

/* Compatibility for callers that still pass ASCII menu keys. */
static int scan_from_ascii(int key) {
    if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
    if (key >= 'A' && key <= 'Z') {
        static const unsigned char map[26] = {
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24,
            0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14,
            0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C
        };
        return map[key - 'A'];
    }
    if (key >= '0' && key <= '9')
        return (key == '0') ? 0x0B : (0x02 + key - '1');
    if (key == ' ') return 0x39;
    if (key == 13) return 0x1C;
    if (key == 27) return 0x01;
    return 0;
}

static void start_intro_story(void) {
    g_pending_score = cc_score();
    set_story_pages(g_intro_pages, g_intro_page_count,
                    CC_SHELL_GAME, 1, 0);
}

/* Main-menu / help video compatibility prompt (0x1A688): the original sets
 * [0xA0EC]=1 for Y and 0 for N/Esc. */
static void start_video_prompt(int return_state) {
    g_return_state = return_state;
    clear_pages();
    g_shell_state = CC_SHELL_VIDEO;
    cursor_reset();
    cc_sound_play(SND_TEXT_PAGE);
}

/* "Quit to DOS (Y-N)" confirm (0x114A1): Y sets [0x3396]=1, N/Esc clear it. */
static void start_quit_confirm(int return_state) {
    g_return_state = return_state;
    clear_pages();
    g_shell_state = CC_SHELL_QUIT_CONFIRM;
    cursor_reset();
    cc_sound_play(SND_TEXT_PAGE);
}

static void start_instructions(int return_state) {
    set_pages(INSTRUCTION_BLOCKS,
              (int)(sizeof(INSTRUCTION_BLOCKS) / sizeof(int)),
              return_state, 0, -1);
}

/* The original "Game saved." and "You can only restore..." windows say
 * "Press ESCAPE"; all other message windows accept any key. */
static int message_escape_only(void) {
    if (g_shell_state != CC_SHELL_MESSAGE) return 0;
    if (g_page_index < 0 || g_page_index >= g_page_count) return 0;
    int block = g_pages[g_page_index].block;
    return block == SHELL_TEXT_MESSAGE_SAVED ||
           block == SHELL_TEXT_MESSAGE_RESTORE_HUB_ONLY;
}

static int text_key(int key) {
    if (key == CC_KEY_ESC) {
            if (g_story_kind == 1) {
                cc_load_arg(0);
                g_shell_state = CC_SHELL_GAME;
                g_story_kind = 0;
                g_story_level = -1;
                return 1;
            }
            if (g_story_kind == 2) {
                begin_ending_fanfare();
                g_story_kind = 0;
                g_story_level = -1;
                return 1;
            }
            g_shell_state = g_return_state;
            return 1;
    }
    if (message_escape_only()) return 1;
    if (key == 0) return 1;
    g_page_index++;
    int pages = g_story_pages ? g_story_page_count : g_page_count;
    if (g_page_index < pages) {
        cc_sound_play(SND_TEXT_PAGE);
        return 1;
    }
    if (g_story_kind == 1) {
        cc_load_arg(0);
        g_shell_state = CC_SHELL_GAME;
    } else if (g_story_kind == 2) {
        begin_ending_fanfare();
    } else {
        g_shell_state = g_return_state;
    }
    g_story_kind = 0;
    g_story_level = -1;
    return 1;
}

int cc_shell_key_scan(int scan) {
    scan &= 0x7F;
    if (g_shell_state == CC_SHELL_KEY_REDEFINE) {
        if (scan == 0x01) {
            g_shell_state = CC_SHELL_HELP;
            return 1;
        }
        if (scan == 0) return 1;
        {
            const char *new_name = g_key_names[scan];
            for (int i = 0; i < g_key_action; i++) {
                const char *old_name = g_key_names[g_key_tmp[i]];
                int same = 1;
                for (int j = 0; new_name[j] || old_name[j]; j++) {
                    if (new_name[j] != old_name[j]) { same = 0; break; }
                }
                if (same) {
                    show_block(SHELL_TEXT_MENU_KEY_UNAVAILABLE,
                               CC_SHELL_KEY_REDEFINE);
                    g_shell_state = CC_SHELL_MESSAGE;
                    g_return_state = CC_SHELL_KEY_REDEFINE;
                    clear_pages();
                    add_block_pages(SHELL_TEXT_MENU_KEY_UNAVAILABLE);
                    return 1;
                }
            }
        }
        if (g_key_action < 4) {
            g_key_tmp[g_key_action++] = scan;
            if (g_key_action >= 4) g_shell_state = CC_SHELL_KEY_CONFIRM;
        }
        return 1;
    }
    if (g_shell_state == CC_SHELL_KEY_CONFIRM) {
        if (scan == 0x01) {
            g_shell_state = CC_SHELL_HELP;
            return 1;
        }
        if (scan == 0x31) {  /* N: keep the previous bindings */
            g_key_action = 0;
            for (int i = 0; i < 4; i++) g_key_tmp[i] = g_keys[i];
            g_shell_state = CC_SHELL_KEY_REDEFINE;
            return 1;
        }
        for (int i = 0; i < 4; i++) cc_shell_set_key(i, g_key_tmp[i]);
        g_shell_state = CC_SHELL_HELP;
        return 1;
    }
    return 0;
}

/* Per-shell-state key handlers; cc_shell_key dispatches to these. */

static int menu_key(int key) {
    int k = lower(key);
    if (k == 'n' || key == CC_KEY_ENTER || key == CC_KEY_SPACE) {
        cc_sound_play(SND_MENU_ACCEPT);
        start_intro_story();
    } else if (k == 'r') {
        g_return_state = CC_SHELL_MENU;
        g_shell_state = CC_SHELL_RESTORE;
    } else if (k == 's') {
        set_story_pages(g_intro_pages, g_intro_page_count,
                        CC_SHELL_MENU, 0, 0);
    } else if (k == 'i') {
        start_instructions(CC_SHELL_MENU);
    } else if (k == 'o') {
        {
            static const int blocks[3] = {
                SHELL_TEXT_ORDERING_EPISODES,
                SHELL_TEXT_ORDERING_OFFER,
                SHELL_TEXT_ORDERING_DISK
            };
            set_pages(blocks, 3, CC_SHELL_MENU, 0, -1);
        }
    } else if (k == 'h') {
        enter_high_score();
    } else if (k == 'f') {
        {
            static const int blocks[4] = {
                SHELL_TEXT_FOREIGN, SHELL_TEXT_FOREIGN_AU,
                SHELL_TEXT_FOREIGN_DE, SHELL_TEXT_FOREIGN_UK
            };
            set_pages(blocks, 4, CC_SHELL_MENU, 0, -1);
        }
    } else if (k == 'c') {
        show_block(SHELL_TEXT_BBS, CC_SHELL_MENU);
    } else if (k == 'v') {
        start_video_prompt(CC_SHELL_MENU);
    } else if (k == 'a') {
        show_block(SHELL_TEXT_ABOUT, CC_SHELL_MENU);
    } else if (k == 'q' || key == CC_KEY_ESC) {
        start_quit_confirm(CC_SHELL_MENU);
    }
    return 1;
}

/* Ending screen: qualify into name entry or straight to the table. */
static int ending_key(void) {
    if (cc_shell_hiscore_qualifies(g_pending_score)) {
        g_name[0] = 0;
        g_name_len = 0;
        g_shell_state = CC_SHELL_NAME_ENTRY;
        cc_sound_play(SND_HISCORE_NAME);
    } else {
        enter_high_score();
    }
    return 1;
}

/* 20-character high-score name entry. */
static int name_entry_key(int key) {
    if (key == CC_KEY_BACKSPACE) {
        if (g_name_len > 0) g_name[--g_name_len] = 0;
    } else if (key == CC_KEY_ENTER) {
        cc_shell_hiscore_add(g_name, g_pending_score);
        enter_high_score();
    } else if (g_name_len < HISCORE_NAME_MAX &&
               ((key >= 'A' && key <= 'Z') ||
                (key >= 'a' && key <= 'z') ||
                (key >= '0' && key <= '9') ||
                key == ' ')) {
        g_name[g_name_len++] = (char)key;
        g_name[g_name_len] = 0;
    }
    return 1;
}

static int game_key(int key) {
    if (key == CC_KEY_ESC || key == CC_KEY_F1) {
        g_return_state = CC_SHELL_GAME;
        g_shell_state = CC_SHELL_HELP;
        return 1;
    }
    if (key == 'p' || key == 'P') {
        g_shell_state = CC_SHELL_PAUSE;
        return 1;
    }
    return 0;
}

/* Pause menu: sound/joystick/video toggles, key redefine, warp, save/
 * restore, quit list. */
static int help_key(int key) {
    int k = lower(key);
    if (key == CC_KEY_ESC) {
        g_shell_state = CC_SHELL_GAME;
    } else if (k == 'i') {
        start_instructions(CC_SHELL_HELP);
    } else if (k == 't') {
        g_sound_on = !g_sound_on;
        g_config_dirty = 1;
        /* Exact original semantics: [0x513E] only gates future
         * play_sound calls; the current effect finishes. */
        cc_sound_set_enabled(g_sound_on);
    } else if (k == 'j') {
        g_joystick_on = !g_joystick_on;
        g_config_dirty = 1;
    } else if (k == 'c') {
        show_block(SHELL_TEXT_MESSAGE_JOYSTICK_CALIBRATE, CC_SHELL_HELP);
    } else if (k == 'v') {
        start_video_prompt(CC_SHELL_HELP);
    } else if (k == 'k') {
        g_key_action = 0;
        for (int i = 0; i < 4; i++) g_key_tmp[i] = g_keys[i];
        g_shell_state = CC_SHELL_KEY_REDEFINE;
    } else if (k == 'w') {
        g_warp_value = -1;
        g_shell_state = CC_SHELL_WARP;
    } else if (k == 'r') {
        g_return_state = CC_SHELL_GAME;
        g_shell_state = CC_SHELL_RESTORE;
    } else if (k == 's') {
        g_return_state = CC_SHELL_GAME;
        g_shell_state = CC_SHELL_SAVE;
    } else if (k == 'q') {
        g_shell_state = CC_SHELL_QUIT;
    }
    return 1;
}

/* Quit list: DOS, back to attract, restart hub, or back to pause. */
static int quit_key(int key) {
    int k = lower(key);
    if (k == 'd') {
        start_quit_confirm(CC_SHELL_QUIT);
    } else if (k == 't') {
        cc_sound_stop();
        scroll_reset();
    } else if (k == 'm') {
        cc_load_arg(0);
        g_shell_state = CC_SHELL_GAME;
    } else if (key == CC_KEY_ESC) {
        g_shell_state = CC_SHELL_HELP;
    }
    return 1;
}

/* Y/N prompts: video mode toggle and the quit-to-DOS confirmation. */
static int video_prompt_key(int key) {
    int k = lower(key);
    if (k == 'y') {
        g_video_alt = 1;
        g_config_dirty = 1;
        g_shell_state = g_return_state;
    } else if (k == 'n') {
        g_video_alt = 0;
        g_config_dirty = 1;
        g_shell_state = g_return_state;
    } else if (key == CC_KEY_ESC) {
        g_shell_state = g_return_state;
    }
    return 1;
}

static int quit_confirm_key(int key) {
    int k = lower(key);
    if (k == 'y') {
        cc_sound_stop();
        g_shell_state = CC_SHELL_DOS;
    } else if (k == 'n' || key == CC_KEY_ESC) {
        g_shell_state = g_return_state;
    }
    return 1;
}

/* Save/restore slot pick; the original only allows saving from the hub. */
static int slot_key(int key) {
    if (key >= '0' && key <= '9') {
        int allow = (cc_cur_level() == 2) ||
                    (g_shell_state == CC_SHELL_RESTORE &&
                     g_return_state == CC_SHELL_MENU);
        if (!allow) {
            int block = (g_shell_state == CC_SHELL_SAVE)
                ? SHELL_TEXT_MESSAGE_SAVE_HUB_ONLY
                : SHELL_TEXT_MESSAGE_RESTORE_HUB_ONLY;
            show_block(block, CC_SHELL_GAME);
            g_shell_state = CC_SHELL_MESSAGE;
            clear_pages();
            add_block_pages(block);
            g_return_state = CC_SHELL_GAME;
        } else {
            g_request_kind = (g_shell_state == CC_SHELL_SAVE) ? 1 : 2;
            g_request_slot = key - '0';
            g_request_pending = 1;
        }
    } else if (key == CC_KEY_ESC) {
        g_shell_state = CC_SHELL_GAME;
    }
    return 1;
}

/* Debug warp entry: one or two digits 1..16, then Enter. */
static int warp_key(int key) {
    int k = lower(key);
    if (k >= '0' && k <= '9') {
        int d = k - '0';
        if (g_warp_value < 0) g_warp_value = d;
        else g_warp_value = g_warp_value * 10 + d;
        if (g_warp_value > 16) g_warp_value %= 10;
    } else if (key == CC_KEY_ENTER) {
        if (g_warp_value >= 1 && g_warp_value <= 16) {
            cc_shell_warp(g_warp_value);
            g_shell_state = CC_SHELL_GAME;
        }
        g_warp_value = -1;
    } else if (key == CC_KEY_ESC) {
        g_warp_value = -1;
        g_shell_state = CC_SHELL_HELP;
    }
    return 1;
}

int cc_shell_key(int key) {
    if (g_wait_sound) return 1;
    g_timer = 0;
    switch (g_shell_state) {
        case CC_SHELL_LOGO:
            scroll_reset();
            return 1;
        case CC_SHELL_CREDITS:
        case CC_SHELL_TITLE:
            g_shell_state = CC_SHELL_MENU;
            return 1;
        case CC_SHELL_MENU:
            return menu_key(key);
        case CC_SHELL_TEXT:
        case CC_SHELL_MESSAGE:
            return text_key(key);
        case CC_SHELL_ENDING:
            return ending_key();
        case CC_SHELL_HISCORE:
            scroll_reset();
            return 1;
        case CC_SHELL_NAME_ENTRY:
            return name_entry_key(key);
        case CC_SHELL_GAME:
            return game_key(key);
        case CC_SHELL_PAUSE:
            g_shell_state = CC_SHELL_GAME;
            return 1;
        case CC_SHELL_HELP:
            return help_key(key);
        case CC_SHELL_QUIT:
            return quit_key(key);
        case CC_SHELL_VIDEO:
            return video_prompt_key(key);
        case CC_SHELL_QUIT_CONFIRM:
            return quit_confirm_key(key);
        case CC_SHELL_DOS:
            /* The original terminates and returns to DOS.  In the browser we
             * stop the game loop and show an exit screen; any key restarts
             * the attract sequence. */
            scroll_reset();
            return 1;
        case CC_SHELL_SAVE:
        case CC_SHELL_RESTORE:
            return slot_key(key);
        case CC_SHELL_KEY_REDEFINE:
        case CC_SHELL_KEY_CONFIRM:
            return cc_shell_key_scan(scan_from_ascii(key));
        case CC_SHELL_WARP:
            return warp_key(key);
        default:
            scroll_reset();
            return 1;
    }
}

void cc_shell_warp(int level_arg) {
    if (level_arg >= 1 && level_arg <= 16) {
        cc_warp_level(level_arg + 2);
        g_shell_state = CC_SHELL_GAME;
    }
}
