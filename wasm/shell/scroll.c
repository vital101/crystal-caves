/* Crystal Caves shell - title/credits attract loop.
 *
 * The original title/credits bounce (0x0FC5:0x8265) holds 433 PIT ticks at
 * each end and advances one row per vertical retrace (~2 PIT ticks),
 * traversing 201 rows (0..200 forward, 200..0 backward) per pass.
 * phase is 0 = title hold, 1 = forward traverse, 2 = credits hold,
 * 3 = backward traverse.
 */
#include "shell_internal.h"

#define SCROLL_HOLD   CC_SHELL_ATTRACT_HOLD_TICKS
#define SCROLL_ROWS   CC_SHELL_SCROLL_ROWS
#define SCROLL_TICKS  CC_SHELL_SCROLL_TICKS_PER_ROW

int g_scroll_phase;
int g_scroll_frame;
int g_scroll_row;
int g_scroll_dir;
int g_scroll_ticks;

void scroll_reset(void) {
    g_scroll_phase = 0;
    g_scroll_frame = 0;
    g_scroll_row = 0;
    g_scroll_dir = 1;
    g_scroll_ticks = 0;
    g_shell_state = CC_SHELL_CREDITS;
}

void scroll_enter_next(void) {
    if (g_scroll_phase == 0) {
        g_scroll_phase = 1;      /* forward: title -> credits */
        g_scroll_frame = 0;
        g_scroll_row = 0;
        g_scroll_dir = 1;
        g_shell_state = CC_SHELL_CREDITS;
    } else {
        g_scroll_phase = 3;      /* backward: credits -> title */
        g_scroll_frame = 0;
        g_scroll_row = SCROLL_ROWS;
        g_scroll_dir = -1;
        g_shell_state = CC_SHELL_TITLE;
    }
}

/* One traversed frame is one vertical retrace.  The original draws 201 rows
 * per pass (0..200 forward, 200..0 backward) and then holds the endpoint. */
#define SCROLL_FRAMES (SCROLL_ROWS + 1)

void scroll_advance(int ticks) {
    g_scroll_ticks += ticks;
    for (;;) {
        if (g_scroll_phase == 0 || g_scroll_phase == 2) {
            if (g_scroll_ticks < SCROLL_HOLD) break;
            g_scroll_ticks -= SCROLL_HOLD;
            scroll_enter_next();
            continue;
        } else {
            int steps = g_scroll_ticks / SCROLL_TICKS;
            int take = SCROLL_FRAMES - g_scroll_frame;
            if (steps <= 0) break;
            if (steps >= take) {
                g_scroll_ticks -= take * SCROLL_TICKS;
                g_scroll_frame = SCROLL_FRAMES;
            } else {
                g_scroll_ticks -= steps * SCROLL_TICKS;
                g_scroll_frame += steps;
            }
            if (g_scroll_frame >= SCROLL_FRAMES) {
                if (g_scroll_phase == 1) {
                    g_scroll_row = SCROLL_ROWS;
                    g_scroll_phase = 2;      /* hold credits */
                    g_shell_state = CC_SHELL_TITLE;
                } else {
                    g_scroll_row = 0;
                    g_scroll_dir = 1;
                    g_scroll_phase = 0;      /* hold title */
                    g_shell_state = CC_SHELL_CREDITS;
                }
                continue;
            }
            g_scroll_row = (g_scroll_phase == 1)
                ? g_scroll_frame : (SCROLL_ROWS - g_scroll_frame);
        }
    }
}

int cc_shell_scroll_row(void) { return g_scroll_row; }
int cc_shell_scroll_dir(void) { return g_scroll_dir; }
int cc_shell_scroll_phase(void) { return g_scroll_phase; }
