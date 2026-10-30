/* Crystal Caves - Phase 5 shell state machine.
 *
 * The original shell uses overlapping Turbo Pascal segments and DOS file I/O;
 * this module reimplements the observable flow with the same text blocks,
 * key letters, screens and save/restore restrictions.  The JS page owns
 * localStorage, canvas presentation and the browser key map.
 */
#include "shell.h"
#include "text.h"
#include "render.h"
#include "objects.h"
#include "shell_text_data.h"
#include "shell_exact_data.h"

#define MAX_PAGES 96
#define PAGE_LINES 18
#define HISCORE_NAME_MAX 20
#define HISCORE_COUNT 10

typedef struct { int block; int start; } TextPage;
typedef struct { char name[24]; unsigned int score; } HiScore;

static int g_state;
static int g_timer;
static int g_return_state;
static int g_story_kind;       /* 0 none, 1 intro, 2 finale */
static int g_story_level;      /* -1 = no level background */
static int g_finale_pending;
static unsigned int g_pending_score;

static TextPage g_pages[MAX_PAGES];
static int g_page_count;
static int g_page_index;
static const ShellPageDef *g_story_pages;
static int g_story_page_count;

/* Selected keys before confirmation, and the committed action keys. */
static int g_key_tmp[4];

static int g_request_pending;
static int g_request_kind;     /* 1 save, 2 restore */
static int g_request_slot;

static int g_sound_on = 1;
static int g_joystick_on = 0;
static int g_video_alt = 0;

static HiScore g_hiscores[10];
static int g_hiscore_count;
static char g_name[24];
static int g_name_len;
static int g_warp_value = -1;
static int g_keys[4] = { 0, 0, 0, 0 };
static int g_key_action;

static const int INSTRUCTION_BLOCKS[] = {
    SHELL_TEXT_INSTRUCTIONS_1, SHELL_TEXT_INSTRUCTIONS_2,
    SHELL_TEXT_INSTRUCTIONS_3, SHELL_TEXT_INSTRUCTIONS_3_2,
    SHELL_TEXT_INSTRUCTIONS_3_3, SHELL_TEXT_INSTRUCTIONS_3_4
};

static int slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void scopy(char *d, const char *s, int cap) {
    int i = 0;
    if (!d || cap <= 0) return;
    while (s && s[i] && i < cap - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static void clear_pages(void) {
    g_page_count = 0;
    g_page_index = 0;
    g_story_pages = 0;
    g_story_page_count = 0;
}

static void add_block_pages(int block) {
    int total = cc_text_block_lines(block);
    for (int start = 0; start < total && g_page_count < MAX_PAGES;
         start += PAGE_LINES) {
        g_pages[g_page_count].block = block;
        g_pages[g_page_count].start = start;
        g_page_count++;
    }
}

static void set_pages(const int *blocks, int n, int return_state,
                      int story_kind, int story_level) {
    clear_pages();
    for (int i = 0; i < n; i++) add_block_pages(blocks[i]);
    g_return_state = return_state;
    g_story_kind = story_kind;
    g_story_level = story_level;
    g_state = CC_SHELL_TEXT;
    if (story_level >= 0) cc_objects_init(story_level);
}

static void set_story_pages(const ShellPageDef *pages, int n,
                            int return_state, int story_kind,
                            int story_level) {
    clear_pages();
    g_story_pages = pages;
    g_story_page_count = n;
    g_return_state = return_state;
    g_story_kind = story_kind;
    g_story_level = story_level;
    g_state = CC_SHELL_TEXT;
    if (story_level >= 0) cc_objects_init(story_level);
}

static void show_block(int block, int return_state) {
    int one[1];
    one[0] = block;
    set_pages(one, 1, return_state, 0, -1);
}

void cc_shell_init(void) {
    g_state = CC_SHELL_LOGO;
    g_timer = 0;
    g_return_state = CC_SHELL_MENU;
    g_story_kind = 0;
    g_story_level = -1;
    g_finale_pending = 0;
    g_pending_score = 0;
    g_request_pending = 0;
    g_request_kind = 0;
    g_request_slot = -1;
    clear_pages();
    g_hiscore_count = HISCORE_COUNT;
    for (int i = 0; i < HISCORE_COUNT; i++) {
        scopy(g_hiscores[i].name, g_hiscore_default_names[i],
              sizeof(g_hiscores[i].name));
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
}

int cc_shell_state(void) { return g_state; }
int cc_shell_message_block(void) {
    if (g_state != CC_SHELL_MESSAGE || g_page_count <= 0 ||
        g_page_index < 0 || g_page_index >= g_page_count) return -1;
    return g_pages[g_page_index].block;
}

int cc_shell_page_count(void) {
    return g_story_pages ? g_story_page_count : g_page_count;
}
int cc_shell_page_index(void) { return g_page_index; }
int cc_shell_page_line_count(void) {
    if (g_story_pages) {
        if (g_page_index < 0 || g_page_index >= g_story_page_count) return 0;
        return g_story_pages[g_page_index].count;
    }
    if (g_page_index < 0 || g_page_index >= g_page_count) return 0;
    return PAGE_LINES;
}
const char *cc_shell_page_line(int index) {
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

int cc_shell_hud_active(void) { return g_state == CC_SHELL_GAME; }
int cc_shell_overlay(void) {
    return g_state == CC_SHELL_PAUSE || g_state == CC_SHELL_HELP ||
           g_state == CC_SHELL_QUIT || g_state == CC_SHELL_SAVE ||
           g_state == CC_SHELL_RESTORE || g_state == CC_SHELL_WARP ||
           g_state == CC_SHELL_MESSAGE;
}

void cc_shell_message(int block_id) {
    if (g_state != CC_SHELL_GAME && g_state != CC_SHELL_MESSAGE) return;
    show_block(block_id, CC_SHELL_GAME);
    /* messages are overlays on the game view */
    g_state = CC_SHELL_MESSAGE;
    g_story_level = -1;
    g_story_kind = 0;
    clear_pages();
    add_block_pages(block_id);
}

void cc_shell_notify_last_crystal(void) {
    if (g_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_LAST_CRYSTAL);
}

void cc_shell_notify_gravity(void) {
    if (g_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_GRAVITY);
}

void cc_shell_notify_power_shot(void) {
    if (g_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_POWER_SHOT);
}

void cc_shell_notify_red_mushroom(void) {
    if (g_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_RED_MUSHROOM);
}

void cc_shell_notify_poison(void) {
    if (g_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_POISON);
}

void cc_shell_notify_stunned(void) {
    if (g_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_STUNNED);
}

void cc_shell_notify_perfect_health(void) {
    if (g_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_PERFECT_HEALTH);
}

void cc_shell_notify_more_crystals(void) {
    if (g_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_MORE_CRYSTALS);
}

void cc_shell_notify_lever(void) {
    if (g_state == CC_SHELL_GAME) cc_shell_message(SHELL_TEXT_MESSAGE_LEVER);
}

void cc_shell_finale_done(void) {
    if (g_state == CC_SHELL_GAME) {
        g_pending_score = cc_score();
        g_finale_pending = 1;
    }
}

void cc_shell_step(void) { cc_shell_step_ticks(1); }

void cc_shell_step_ticks(int ticks) {
    if (ticks < 0) ticks = 0;
    g_timer += ticks;
    if (g_state == CC_SHELL_LOGO) {
        if (g_timer > CC_SHELL_APOGEE_TICKS) {
            g_state = CC_SHELL_CREDITS;
            g_timer = 0;
        }
    } else if (g_state == CC_SHELL_CREDITS) {
        if (g_timer > CC_SHELL_CREDITS_TICKS) {
            g_state = CC_SHELL_TITLE;
            g_timer = 0;
        }
    } else if (g_state == CC_SHELL_TITLE) {
        if (g_timer > CC_SHELL_TITLE_TICKS) {
            g_state = CC_SHELL_MENU;
            g_timer = 0;
        }
    }
    if (g_finale_pending) {
        g_finale_pending = 0;
        if (g_state == CC_SHELL_GAME)
            set_story_pages(g_finale_pages, g_finale_page_count,
                            CC_SHELL_ENDING, 2, 1);
    }
}

/* ---- save/restore handshake -------------------------------------------- */

int cc_shell_request_pending(void) { return g_request_pending; }
int cc_shell_request_kind(void) { return g_request_kind; }
int cc_shell_request_slot(void) { return g_request_slot; }

void cc_shell_request_done(int ok) {
    g_request_pending = 0;
    if (g_request_kind == 1) {
        if (ok) {
            show_block(SHELL_TEXT_MESSAGE_SAVED, CC_SHELL_GAME);
            g_state = CC_SHELL_MESSAGE;
            clear_pages();
            add_block_pages(SHELL_TEXT_MESSAGE_SAVED);
            g_return_state = CC_SHELL_GAME;
        } else {
            int block = (cc_cur_level() == 2)
                ? SHELL_TEXT_MESSAGE_FILE_ERROR
                : SHELL_TEXT_MESSAGE_SAVE_HUB_ONLY;
            show_block(block, CC_SHELL_GAME);
            g_state = CC_SHELL_MESSAGE;
            clear_pages();
            add_block_pages(block);
            g_return_state = CC_SHELL_GAME;
        }
    } else if (g_request_kind == 2) {
        if (ok) {
            g_state = CC_SHELL_GAME;
        } else {
            show_block(SHELL_TEXT_MESSAGE_FILE_ERROR, CC_SHELL_GAME);
            g_state = CC_SHELL_MESSAGE;
            clear_pages();
            add_block_pages(SHELL_TEXT_MESSAGE_FILE_ERROR);
            g_return_state = CC_SHELL_GAME;
        }
    }
}

/* ---- high scores -------------------------------------------------------- */

int cc_shell_hiscore_count(void) { return g_hiscore_count; }
const char *cc_shell_hiscore_name(int i) {
    return (i >= 0 && i < g_hiscore_count) ? g_hiscores[i].name : "";
}
unsigned int cc_shell_hiscore_score(int i) {
    return (i >= 0 && i < g_hiscore_count) ? g_hiscores[i].score : 0;
}
void cc_shell_hiscore_set(int i, const char *name, unsigned int score) {
    if (i < 0 || i >= HISCORE_COUNT) return;
    scopy(g_hiscores[i].name, name ? name : "", sizeof(g_hiscores[i].name));
    g_hiscores[i].score = score;
    if (g_hiscore_count < i + 1) g_hiscore_count = i + 1;
}
int cc_shell_hiscore_qualifies(unsigned int score) {
    if (g_hiscore_count < HISCORE_COUNT) return 1;
    return score > g_hiscores[HISCORE_COUNT - 1].score;
}
void cc_shell_hiscore_add(const char *name, unsigned int score) {
    if (!cc_shell_hiscore_qualifies(score)) return;
    int pos = HISCORE_COUNT;
    for (int i = 0; i < HISCORE_COUNT; i++) {
        if (score > g_hiscores[i].score) { pos = i; break; }
    }
    if (pos >= HISCORE_COUNT) return;
    for (int i = HISCORE_COUNT - 1; i > pos; i--)
        g_hiscores[i] = g_hiscores[i - 1];
    scopy(g_hiscores[pos].name, name ? name : "",
          sizeof(g_hiscores[pos].name));
    g_hiscores[pos].score = score;
    g_hiscore_count = HISCORE_COUNT;
}

/* ---- config ------------------------------------------------------------- */

int cc_shell_sound_on(void) { return g_sound_on; }
int cc_shell_joystick_on(void) { return g_joystick_on; }
int cc_shell_video_alt(void) { return g_video_alt; }
int cc_shell_key_redefine_active(void) {
    return g_state == CC_SHELL_KEY_REDEFINE;
}
int cc_shell_key_redefine_step(void) { return g_key_action; }
int cc_shell_key_for_action(int action) {
    if (action < 0 || action > 3) return 0;
    return g_keys[action];
}
int cc_shell_key_name(int scan, const char **name) {
    if (scan < 0 || scan > 0x7F) {
        if (name) *name = "NULL";
        return 0;
    }
    if (name) *name = g_key_names[scan];
    return g_key_names[scan][0] != 'N' || g_key_names[scan][1] != 'U';
}
int cc_shell_action_for_scan(int scan) {
    for (int i = 0; i < 4; i++)
        if (g_keys[i] == scan) return i;
    return -1;
}
void cc_shell_set_key(int action, int key) {
    static const int globals[4] = { 0x2B6C, 0x2B6E, 0x2B70, 0x2B7A };
    if (action < 0 || action > 3) return;
    if (key < 0 || key > 0x7F) return;
    g_keys[action] = key;
    cc_global_set(globals[action], key & 0xFFFF);
}
void cc_input_scan(int scan, int down) {
    scan &= 0x7F;
    if (scan == g_keys[0]) cc_input_set(0x338E, down ? 1 : 0);
    if (scan == g_keys[1]) cc_input_set(0x338F, down ? 1 : 0);
    if (scan == g_keys[2]) cc_input_set(0x338B, down ? 1 : 0);
    if (scan == g_keys[3]) cc_input_set(0x3390, down ? 1 : 0);
}

void cc_shell_set_config(int sound, int joystick, int video) {
    g_sound_on = sound ? 1 : 0;
    g_joystick_on = joystick ? 1 : 0;
    g_video_alt = video ? 1 : 0;
}

/* ---- rendering ---------------------------------------------------------- */

static void draw_lines_centered(const char *const *lines, int count);

static void draw_hiscore_screen(void) {
    char lines[17][40];
    const char *ptrs[17];

    lines[0][0] = 0;
    scopy(lines[0], "   Top Intergalactic Traders", sizeof(lines[0]));
    scopy(lines[1], " -----------------------------", sizeof(lines[1]));
    scopy(lines[2], " ", sizeof(lines[2]));
    for (int i = 0; i < HISCORE_COUNT; i++) {
        int n = 0;
        lines[3 + i][n++] = ' ';
        const char *nm = g_hiscores[i].name;
        for (int j = 0; j < HISCORE_NAME_MAX; j++)
            lines[3 + i][n++] = nm[j] ? nm[j] : ' ';
        lines[3 + i][n++] = ' ';
        unsigned int v = g_hiscores[i].score;
        char d[12];
        int dn = 0;
        if (!v) d[dn++] = '0';
        while (v) { d[dn++] = (char)('0' + v % 10); v /= 10; }
        for (int j = dn - 1; j >= 0; j--)
            lines[3 + i][n++] = d[j];
        lines[3 + i][n] = 0;
    }
    scopy(lines[13], " ", sizeof(lines[13]));
    scopy(lines[14], " -----------------------------", sizeof(lines[14]));
    scopy(lines[15], " ", sizeof(lines[15]));
    scopy(lines[16], "         Press Any Key ^", sizeof(lines[16]));
    for (int i = 0; i < 17; i++) ptrs[i] = lines[i];
    draw_lines_centered(ptrs, 17);
}

static void draw_name_entry(void) {
    char line[48];
    const char *ptrs[4];
    scopy(line, " Enter your name: ", sizeof(line));
    int n = slen(line);
    for (int i = 0; i < g_name_len && n < (int)sizeof(line) - 2; i++)
        line[n++] = g_name[i];
    line[n++] = '^';
    line[n] = 0;
    ptrs[0] = " Your score is in the top ten! ";
    ptrs[1] = line;
    ptrs[2] = " ";
    ptrs[3] = "   ";
    draw_lines_centered(ptrs, 4);
}

/* Shell windows are screen-space UI: the original wrappers are called with
 * the window/camera offsets clear.  Keep the player's camera/scroll globals
 * untouched for the next game frame. */
static void draw_block_centered(int block, int start, int max) {
    int cx = cc_global_get(0x512e);
    int cy = cc_global_get(0x5130);
    int scroll = cc_global_get(0x2b5c);
    cc_global_set(0x512e, 0);
    cc_global_set(0x5130, 0);
    cc_global_set(0x2b5c, 0);
    cc_text_draw_block(block, start, max);
    cc_global_set(0x512e, cx);
    cc_global_set(0x5130, cy);
    cc_global_set(0x2b5c, scroll);
}

static void draw_lines_centered(const char *const *lines, int count) {
    int cx = cc_global_get(0x512e);
    int cy = cc_global_get(0x5130);
    int scroll = cc_global_get(0x2b5c);
    cc_global_set(0x512e, 0);
    cc_global_set(0x5130, 0);
    cc_global_set(0x2b5c, 0);
    cc_text_draw_lines(lines, count);
    cc_global_set(0x512e, cx);
    cc_global_set(0x5130, cy);
    cc_global_set(0x2b5c, scroll);
}

void cc_shell_render(void) {
    switch (g_state) {
        case CC_SHELL_LOGO:
            cc_draw_screen(0);
            break;
        case CC_SHELL_TITLE:
            cc_draw_screen(1);
            break;
        case CC_SHELL_MENU:
            cc_draw_screen(1);
            draw_block_centered(SHELL_TEXT_MENU_MAIN, 0, PAGE_LINES);
            break;
        case CC_SHELL_TEXT:
            if (g_story_level >= 0)
                cc_render_view_cells(g_story_level, cc_cam_x(), cc_cam_y());
            else
                cc_view_clear(0);
            if (g_story_pages) {
                if (g_page_index < g_story_page_count)
                    draw_lines_centered(g_story_pages[g_page_index].lines,
                                        g_story_pages[g_page_index].count);
            } else if (g_page_index < g_page_count) {
                draw_block_centered(g_pages[g_page_index].block,
                                   g_pages[g_page_index].start, PAGE_LINES);
            }
            break;
        case CC_SHELL_ENDING:
            cc_draw_screen(3);
            break;
        case CC_SHELL_CREDITS:
            cc_draw_screen(2);
            break;
        case CC_SHELL_KEY_REDEFINE: {
            static const char *const prompt[4] = {
                "Select key to move left: ^",
                "Select key to move right: ^",
                "Select key to jump: ^",
                "Select key to fire: ^"
            };
            const char *lines[5];
            lines[0] = "KEYBOARD REDEFINE";
            lines[1] = (g_key_action >= 0 && g_key_action < 4)
                ? prompt[g_key_action] : "Select key: ^";
            lines[2] = " ";
            lines[3] = "Press ESC to cancel.";
            lines[4] = " ";
            cc_view_clear(0);
            draw_lines_centered(lines, 5);
            break;
        }
        case CC_SHELL_KEY_CONFIRM: {
            char left[32], right[32], jump[32], fire[32];
            const char *lines[6];
            const char *nm;
            cc_shell_key_name(g_key_tmp[0], &nm);
            left[0] = 0; scopy(left, "LEFT  - ", sizeof(left));
            scopy(left + slen(left), nm, sizeof(left) - slen(left));
            cc_shell_key_name(g_key_tmp[1], &nm);
            right[0] = 0; scopy(right, "RIGHT - ", sizeof(right));
            scopy(right + slen(right), nm, sizeof(right) - slen(right));
            cc_shell_key_name(g_key_tmp[2], &nm);
            jump[0] = 0; scopy(jump, "JUMP  - ", sizeof(jump));
            scopy(jump + slen(jump), nm, sizeof(jump) - slen(jump));
            cc_shell_key_name(g_key_tmp[3], &nm);
            fire[0] = 0; scopy(fire, "FIRE  - ", sizeof(fire));
            scopy(fire + slen(fire), nm, sizeof(fire) - slen(fire));
            lines[0] = "KEYBOARD REDEFINED";
            lines[1] = left;
            lines[2] = right;
            lines[3] = jump;
            lines[4] = fire;
            lines[5] = "Are these selections ok (Y-N): ^";
            cc_view_clear(0);
            draw_lines_centered(lines, 6);
            break;
        }
        case CC_SHELL_HISCORE:
            draw_hiscore_screen();
            break;
        case CC_SHELL_NAME_ENTRY:
            draw_name_entry();
            break;
        case CC_SHELL_PAUSE:
            draw_block_centered(SHELL_TEXT_MESSAGE_PAUSE, 0, PAGE_LINES);
            break;
        case CC_SHELL_HELP:
            draw_block_centered(SHELL_TEXT_MENU_HELP, 0, PAGE_LINES);
            break;
        case CC_SHELL_QUIT:
            draw_block_centered(SHELL_TEXT_MENU_QUIT, 0, PAGE_LINES);
            break;
        case CC_SHELL_SAVE:
            cc_view_fill_rect(40, 80, 240, 40, 1);
            cc_text_draw_string(56, 92, "SAVE GAME (0 - 9) ^");
            break;
        case CC_SHELL_RESTORE:
            cc_view_fill_rect(40, 80, 240, 40, 1);
            cc_text_draw_string(56, 92, "RESTORE GAME (0 - 9) ^");
            break;
        case CC_SHELL_WARP:
            draw_block_centered(SHELL_TEXT_MENU_WARP, 0, PAGE_LINES);
            break;
        case CC_SHELL_MESSAGE:
            if (g_page_index < g_page_count)
                draw_block_centered(g_pages[g_page_index].block,
                                   g_pages[g_page_index].start, PAGE_LINES);
            break;
        case CC_SHELL_GAME:
        default:
            break;
    }
}

/* ---- input -------------------------------------------------------------- */

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

static void start_instructions(int return_state) {
    set_pages(INSTRUCTION_BLOCKS,
              (int)(sizeof(INSTRUCTION_BLOCKS) / sizeof(int)),
              return_state, 0, -1);
}

static void start_finale_story(void) {
    g_pending_score = cc_score();
    set_story_pages(g_finale_pages, g_finale_page_count,
                    CC_SHELL_ENDING, 2, 1);
}

static int text_key(int key) {
    if (key == CC_KEY_ESC) {
            if (g_story_kind == 1) {
                cc_load_arg(0);
                g_state = CC_SHELL_GAME;
                g_story_kind = 0;
                g_story_level = -1;
                return 1;
            }
            if (g_story_kind == 2) {
                g_state = CC_SHELL_ENDING;
                g_story_kind = 0;
                g_story_level = -1;
                return 1;
            }
            g_state = g_return_state;
            return 1;
    }
    if (key == 0) return 1;
    g_page_index++;
    int pages = g_story_pages ? g_story_page_count : g_page_count;
    if (g_page_index < pages) return 1;
    if (g_story_kind == 1) {
        cc_load_arg(0);
        g_state = CC_SHELL_GAME;
    } else if (g_story_kind == 2) {
        g_state = CC_SHELL_ENDING;
    } else {
        g_state = g_return_state;
    }
    g_story_kind = 0;
    g_story_level = -1;
    return 1;
}

int cc_shell_key_scan(int scan) {
    scan &= 0x7F;
    if (g_state == CC_SHELL_KEY_REDEFINE) {
        if (scan == 0x01) {
            g_state = CC_SHELL_HELP;
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
                    g_state = CC_SHELL_MESSAGE;
                    g_return_state = CC_SHELL_KEY_REDEFINE;
                    clear_pages();
                    add_block_pages(SHELL_TEXT_MENU_KEY_UNAVAILABLE);
                    return 1;
                }
            }
        }
        if (g_key_action < 4) {
            g_key_tmp[g_key_action++] = scan;
            if (g_key_action >= 4) g_state = CC_SHELL_KEY_CONFIRM;
        }
        return 1;
    }
    if (g_state == CC_SHELL_KEY_CONFIRM) {
        if (scan == 0x01) {
            g_state = CC_SHELL_HELP;
            return 1;
        }
        if (scan == 0x31) {  /* N: keep the previous bindings */
            g_key_action = 0;
            for (int i = 0; i < 4; i++) g_key_tmp[i] = g_keys[i];
            g_state = CC_SHELL_KEY_REDEFINE;
            return 1;
        }
        for (int i = 0; i < 4; i++) cc_shell_set_key(i, g_key_tmp[i]);
        g_state = CC_SHELL_HELP;
        return 1;
    }
    return 0;
}

int cc_shell_key(int key) {
    g_timer = 0;
    switch (g_state) {
        case CC_SHELL_LOGO:
            g_state = CC_SHELL_CREDITS;
            return 1;
        case CC_SHELL_CREDITS:
            g_state = CC_SHELL_TITLE;
            return 1;
        case CC_SHELL_TITLE:
            g_state = CC_SHELL_MENU;
            return 1;
        case CC_SHELL_MENU: {
            int k = lower(key);
            if (k == 'n' || key == CC_KEY_ENTER || key == CC_KEY_SPACE) {
                start_intro_story();
            } else if (k == 'r') {
                g_return_state = CC_SHELL_MENU;
                g_state = CC_SHELL_RESTORE;
            } else if (k == 's') {
                set_story_pages(g_intro_pages, g_intro_page_count,
                                CC_SHELL_MENU, 0, 0);
            } else if (k == 'i') {
                start_instructions(CC_SHELL_MENU);
            } else if (k == 'o') {
                {
                    const int blocks[2] = { SHELL_TEXT_ORDERING_DISK,
                                            SHELL_TEXT_ORDERING };
                    set_pages(blocks, 2, CC_SHELL_MENU, 0, -1);
                }
            } else if (k == 'h') {
                g_state = CC_SHELL_HISCORE;
            } else if (k == 'f') {
                show_block(SHELL_TEXT_FOREIGN, CC_SHELL_MENU);
            } else if (k == 'c') {
                show_block(SHELL_TEXT_BBS, CC_SHELL_MENU);
            } else if (k == 'v') {
                show_block(SHELL_TEXT_MENU_VIDEO, CC_SHELL_MENU);
            } else if (k == 'a') {
                show_block(SHELL_TEXT_ABOUT, CC_SHELL_MENU);
            } else if (k == 'q' || key == CC_KEY_ESC) {
                g_state = CC_SHELL_TITLE;
            }
            return 1;
        }
        case CC_SHELL_TEXT:
        case CC_SHELL_MESSAGE:
            return text_key(key);
        case CC_SHELL_ENDING:
            if (cc_shell_hiscore_qualifies(g_pending_score)) {
                g_name[0] = 0;
                g_name_len = 0;
                g_state = CC_SHELL_NAME_ENTRY;
            } else {
                g_state = CC_SHELL_HISCORE;
            }
            return 1;
        case CC_SHELL_HISCORE:
            g_state = CC_SHELL_TITLE;
            return 1;
        case CC_SHELL_NAME_ENTRY:
            if (key == CC_KEY_BACKSPACE) {
                if (g_name_len > 0) g_name[--g_name_len] = 0;
            } else if (key == CC_KEY_ENTER) {
                cc_shell_hiscore_add(g_name, g_pending_score);
                g_state = CC_SHELL_HISCORE;
            } else if (g_name_len < HISCORE_NAME_MAX &&
                       ((key >= 'A' && key <= 'Z') ||
                        (key >= 'a' && key <= 'z') ||
                        (key >= '0' && key <= '9') ||
                        key == ' ')) {
                g_name[g_name_len++] = (char)key;
                g_name[g_name_len] = 0;
            }
            return 1;
        case CC_SHELL_GAME:
            if (key == CC_KEY_ESC || key == CC_KEY_F1) {
                g_return_state = CC_SHELL_GAME;
                g_state = CC_SHELL_HELP;
                return 1;
            }
            if (key == 'p' || key == 'P') {
                g_state = CC_SHELL_PAUSE;
                return 1;
            }
            return 0;
        case CC_SHELL_PAUSE:
            g_state = CC_SHELL_GAME;
            return 1;
        case CC_SHELL_HELP: {
            int k = lower(key);
            if (key == CC_KEY_ESC) {
                g_state = CC_SHELL_GAME;
            } else if (k == 'i') {
                start_instructions(CC_SHELL_HELP);
            } else if (k == 't') {
                g_sound_on = !g_sound_on;
            } else if (k == 'j') {
                g_joystick_on = !g_joystick_on;
            } else if (k == 'c') {
                show_block(SHELL_TEXT_MESSAGE_JOYSTICK_CALIBRATE, CC_SHELL_HELP);
            } else if (k == 'v') {
                show_block(SHELL_TEXT_MENU_VIDEO, CC_SHELL_HELP);
            } else if (k == 'k') {
                g_key_action = 0;
                for (int i = 0; i < 4; i++) g_key_tmp[i] = g_keys[i];
                g_state = CC_SHELL_KEY_REDEFINE;
            } else if (k == 'w') {
                g_warp_value = -1;
                g_state = CC_SHELL_WARP;
            } else if (k == 'r') {
                g_return_state = CC_SHELL_GAME;
                g_state = CC_SHELL_RESTORE;
            } else if (k == 's') {
                g_return_state = CC_SHELL_GAME;
                g_state = CC_SHELL_SAVE;
            } else if (k == 'q') {
                g_state = CC_SHELL_QUIT;
            }
            return 1;
        }
        case CC_SHELL_QUIT: {
            int k = lower(key);
            if (k == 'd' || k == 't') {
                g_state = CC_SHELL_TITLE;
            } else if (k == 'm') {
                cc_load_arg(0);
                g_state = CC_SHELL_GAME;
            } else if (key == CC_KEY_ESC) {
                g_state = CC_SHELL_HELP;
            }
            return 1;
        }
        case CC_SHELL_SAVE:
        case CC_SHELL_RESTORE:
            if (key >= '0' && key <= '9') {
                int allow = (cc_cur_level() == 2) ||
                            (g_state == CC_SHELL_RESTORE &&
                             g_return_state == CC_SHELL_MENU);
                if (!allow) {
                    int block = (g_state == CC_SHELL_SAVE)
                        ? SHELL_TEXT_MESSAGE_SAVE_HUB_ONLY
                        : SHELL_TEXT_MESSAGE_RESTORE_HUB_ONLY;
                    show_block(block, CC_SHELL_GAME);
                    g_state = CC_SHELL_MESSAGE;
                    clear_pages();
                    add_block_pages(block);
                    g_return_state = CC_SHELL_GAME;
                } else {
                    g_request_kind = (g_state == CC_SHELL_SAVE) ? 1 : 2;
                    g_request_slot = key - '0';
                    g_request_pending = 1;
                }
            } else if (key == CC_KEY_ESC) {
                g_state = CC_SHELL_GAME;
            }
            return 1;
        case CC_SHELL_KEY_REDEFINE:
        case CC_SHELL_KEY_CONFIRM:
            return cc_shell_key_scan(scan_from_ascii(key));
        case CC_SHELL_WARP: {
            int k = lower(key);
            if (k >= '0' && k <= '9') {
                int d = k - '0';
                if (g_warp_value < 0) g_warp_value = d;
                else g_warp_value = g_warp_value * 10 + d;
                if (g_warp_value > 16) g_warp_value %= 10;
            } else if (key == CC_KEY_ENTER) {
                if (g_warp_value >= 1 && g_warp_value <= 16) {
                    cc_shell_warp(g_warp_value);
                    g_state = CC_SHELL_GAME;
                }
                g_warp_value = -1;
            } else if (key == CC_KEY_ESC) {
                g_warp_value = -1;
                g_state = CC_SHELL_HELP;
            }
            return 1;
        }
        default:
            g_state = CC_SHELL_TITLE;
            return 1;
    }
}

void cc_shell_warp(int level_arg) {
    if (level_arg >= 1 && level_arg <= 16) {
        cc_warp_level(level_arg + 2);
        g_state = CC_SHELL_GAME;
    }
}
