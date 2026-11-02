/* Crystal Caves shell - rendering.
 *
 * cc_shell_render() draws the current screen for each shell state, plus
 * the centered-window and prompt draw helpers.  Shell windows are
 * screen-space UI: the original wrappers are called with the window/camera
 * offsets clear, so the helpers save/restore the player's camera/scroll
 * globals around every draw.
 */
#include "shell_internal.h"

static void draw_lines_centered_frame(const char *const *lines, int count, int frame);

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
    draw_lines_centered_frame(ptrs, 17, g_cursor_frame);
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
    draw_lines_centered_frame(ptrs, 4, g_cursor_frame);
}

/* Shell windows are screen-space UI: the original wrappers are called with
 * the window/camera offsets clear.  Keep the player's camera/scroll globals
 * untouched for the next game frame. */
static void draw_block_centered_frame(int block, int start, int max,
                                      int frame) {
    int cx = cc_global_get(0x512e);
    int cy = cc_global_get(0x5130);
    int scroll = cc_global_get(0x2b5c);
    cc_global_set(0x512e, 0);
    cc_global_set(0x5130, 0);
    cc_global_set(0x2b5c, 0);
    if (frame > 0) cc_text_draw_block_cursor(block, start, max, frame);
    else cc_text_draw_block(block, start, max);
    cc_global_set(0x512e, cx);
    cc_global_set(0x5130, cy);
    cc_global_set(0x2b5c, scroll);
}

static void draw_block_centered(int block, int start, int max) {
    draw_block_centered_frame(block, start, max, 0);
}

static void draw_lines_centered_frame(const char *const *lines, int count,
                                      int frame) {
    int cx = cc_global_get(0x512e);
    int cy = cc_global_get(0x5130);
    int scroll = cc_global_get(0x2b5c);
    cc_global_set(0x512e, 0);
    cc_global_set(0x5130, 0);
    cc_global_set(0x2b5c, 0);
    if (frame > 0) cc_text_draw_lines_cursor(lines, count, frame);
    else cc_text_draw_lines(lines, count);
    cc_global_set(0x512e, cx);
    cc_global_set(0x5130, cy);
    cc_global_set(0x2b5c, scroll);
}

/* Plain pixel-positioned prompt; the trailing '^' marks the cursor cell. */
static void draw_prompt_text(int x, int y, const char *s) {
    cc_text_draw_string(x, y, s);
    if (g_cursor_frame > 0) {
        int col = slen(s) - 1;
        cc_text_cursor_draw(x / 8 + col, y / 8, g_cursor_frame);
    }
}

void cc_shell_render(void) {
    switch (g_shell_state) {
        case CC_SHELL_LOGO:
            cc_draw_screen(0);
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
                    draw_lines_centered_frame(
                        g_story_pages[g_page_index].lines,
                        g_story_pages[g_page_index].count, g_cursor_frame);
            } else if (g_page_index < g_page_count) {
                draw_block_centered_frame(g_pages[g_page_index].block,
                                          g_pages[g_page_index].start,
                                          PAGE_LINES, g_cursor_frame);
            }
            break;
        case CC_SHELL_ENDING:
            cc_draw_screen(3);
            break;
        case CC_SHELL_CREDITS:
        case CC_SHELL_TITLE:
            cc_draw_screen_rows(1, 2, g_scroll_row);
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
            draw_lines_centered_frame(lines, 5, g_cursor_frame);
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
            draw_lines_centered_frame(lines, 6, g_cursor_frame);
            break;
        }
        case CC_SHELL_HISCORE:
            draw_hiscore_screen();
            break;
        case CC_SHELL_NAME_ENTRY:
            draw_name_entry();
            break;
        case CC_SHELL_PAUSE:
            draw_block_centered_frame(SHELL_TEXT_MESSAGE_PAUSE, 0,
                                      PAGE_LINES, g_cursor_frame);
            break;
        case CC_SHELL_HELP:
            draw_block_centered_frame(SHELL_TEXT_MENU_HELP, 0,
                                      PAGE_LINES, g_cursor_frame);
            break;
        case CC_SHELL_QUIT:
            draw_block_centered_frame(SHELL_TEXT_MENU_QUIT, 0,
                                      PAGE_LINES, g_cursor_frame);
            break;
        case CC_SHELL_SAVE:
            cc_view_fill_rect(40, 80, 240, 40, 1);
            draw_prompt_text(56, 92, "SAVE GAME (0 - 9) ^");
            break;
        case CC_SHELL_RESTORE:
            cc_view_fill_rect(40, 80, 240, 40, 1);
            draw_prompt_text(56, 92, "RESTORE GAME (0 - 9) ^");
            break;
        case CC_SHELL_WARP:
            draw_block_centered_frame(SHELL_TEXT_MENU_WARP, 0,
                                      PAGE_LINES, g_cursor_frame);
            break;
        case CC_SHELL_MESSAGE:
            if (g_page_index < g_page_count)
                draw_block_centered_frame(g_pages[g_page_index].block,
                                          g_pages[g_page_index].start,
                                          PAGE_LINES, g_cursor_frame);
            break;
        case CC_SHELL_VIDEO:
            /* Modal over the main menu or the in-game help menu. */
            if (g_return_state == CC_SHELL_MENU) {
                cc_draw_screen(1);
                draw_block_centered(SHELL_TEXT_MENU_MAIN, 0, PAGE_LINES);
            } else if (g_return_state == CC_SHELL_HELP) {
                draw_block_centered(SHELL_TEXT_MENU_HELP, 0, PAGE_LINES);
            } else if (g_return_state == CC_SHELL_QUIT) {
                draw_block_centered(SHELL_TEXT_MENU_QUIT, 0, PAGE_LINES);
            }
            draw_block_centered_frame(SHELL_TEXT_MENU_VIDEO, 0,
                                      PAGE_LINES, g_cursor_frame);
            break;
        case CC_SHELL_QUIT_CONFIRM:
            if (g_return_state == CC_SHELL_MENU) {
                cc_draw_screen(1);
                draw_block_centered(SHELL_TEXT_MENU_MAIN, 0, PAGE_LINES);
            } else if (g_return_state == CC_SHELL_QUIT) {
                draw_block_centered(SHELL_TEXT_MENU_QUIT, 0, PAGE_LINES);
            }
            draw_block_centered_frame(SHELL_TEXT_MENU_QUIT_DOS, 0,
                                      PAGE_LINES, g_cursor_frame);
            break;
        case CC_SHELL_DOS: {
            /* Original exits to DOS; the browser stops here with an exit
             * screen and any key restarts the attract sequence. */
            int cx = cc_global_get(0x512e);
            int cy = cc_global_get(0x5130);
            int scroll = cc_global_get(0x2b5c);
            cc_global_set(0x512e, 0);
            cc_global_set(0x5130, 0);
            cc_global_set(0x2b5c, 0);
            cc_view_clear(0);
            draw_prompt_text(40, 88, "Crystal Caves has ended.");
            draw_prompt_text(16, 104, "Press any key to restart. ^");
            cc_global_set(0x512e, cx);
            cc_global_set(0x5130, cy);
            cc_global_set(0x2b5c, scroll);
            break;
        }
        case CC_SHELL_GAME:
        default:
            break;
    }
}
