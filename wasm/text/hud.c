/* Crystal Caves text - HUD rendering and power-shot cadence.
 */
#include "hud.h"

#include "objects.h"
#include "render.h"

/* ---- HUD ---------------------------------------------------------------- */

static int u32_digits(unsigned int v, char *buf) {
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

static void format_u32(unsigned int v, int width, char *buf) {
    char digits[12];
    int n = u32_digits(v, digits);
    if (n > width) {
        for (int i = 0; i < width; i++) buf[i] = digits[i];
    } else {
        int pad = width - n;
        for (int i = 0; i < pad; i++) buf[i] = ' ';
        for (int i = 0; i < n; i++) buf[pad + i] = digits[i];
    }
    buf[width] = 0;
}

static int gs(int off) { return cc_global_get(off); }

/* Original HUD power-shot cadence: called once per game frame, not once per
 * rendered frame.  [0xA0C6] counts 1..0x11 then [0xA0C4] is decremented. */
void cc_hud_tick(void) {
    /* The all-caves HUD branch returns before the power-timer section. */
    if (gs(0xa0fb)) return;
    int sub_t = gs(0xa0c6) & 0xFFFF;
    sub_t++;
    if (sub_t > 0x11) {
        sub_t = 0;
        int pt = (int)(int16_t)(gs(0xa0c4) & 0xFFFF);
        pt--;
        cc_global_set(0xa0c4, pt & 0xFFFF);
    }
    cc_global_set(0xa0c6, sub_t);
}

static void hud_render_sub(unsigned char sub) {
    char buf[16];
    int x, y = 1;

    /* Score: icon at cell 0, seven right-aligned digits at cells 1..7. */
    unsigned int score = ((unsigned int)(gs(0x50a7) & 0xFFFF) << 16)
                       | (unsigned int)(gs(0x50a5) & 0xFFFF);
    format_u32(score, 7, buf);
    {
        const unsigned char *g = glyph(g_f1, 14);
        if (g) cc_view_draw_glyph(g, 0 + sub, y);
    }
    x = 1 * CELL_W + sub;
    for (int i = 0; i < 7; i++) {
        if (buf[i] != ' ') {
            const unsigned char *g = glyph(g_spl, buf[i] - '0' + 8);
            if (g) cc_view_draw_glyph(g, x, y);
        }
        x += CELL_W;
    }

    if (gs(0xa0fb)) {
        /* All caves complete: "Crystals: " label + four digits. */
        /* The original label call includes a 7-pixel left margin. */
        x = 9 * CELL_W + sub + 7;
        cc_text_draw_string(x, y, "Crystals: ");
        format_u32((unsigned int)(gs(0x50a2) & 0xFFFF), 4, buf);
        x = 19 * CELL_W + sub;
        for (int i = 0; i < 4; i++) {
            if (buf[i] != ' ') {
                const unsigned char *g = glyph(g_spl, buf[i] - '0' + 8);
                if (g) cc_view_draw_glyph(g, x, y);
            }
            x += CELL_W;
        }
        return;
    }

    /* Ammo: icon at cell 11, three right-aligned digits at cells 12..14. */
    {
        const unsigned char *g = glyph(g_spl, 0);
        if (g) cc_view_draw_glyph(g, 11 * CELL_W + sub, y);
    }
    format_u32((unsigned int)(gs(0x509c) & 0xFFFF), 3, buf);
    x = 12 * CELL_W + sub;
    for (int i = 0; i < 3; i++) {
        if (buf[i] != ' ') {
            const unsigned char *g = glyph(g_spl, buf[i] - '0' + 8);
            if (g) cc_view_draw_glyph(g, x, y);
        }
        x += CELL_W;
    }

    /* Hearts at cells 19.. */
    int health = gs(0x509e) & 0xFFFF;
    if (health > 8) health = 8;
    for (int i = 0; i < health; i++) {
        const unsigned char *g = glyph(g_spl, 4);
        if (g) cc_view_draw_glyph(g, (19 + i) * CELL_W + sub, y);
    }

    /* Status icon (switch/door latch).  Its cell follows the hearts:
     * the original computes ([bp-0x66]+2 + health - 1) * 8. */
    if (gs(0xa0f9)) {
        int cell = 20 + health;
        const unsigned char *g = glyph(g_spl, 5);
        if (g) cc_view_draw_glyph(g, cell * CELL_W + sub, y);
    }

    /* Power-shot timer: *NN*, state advanced by cc_hud_tick() per game frame. */
    int pt = (int)(int16_t)(gs(0xa0c4) & 0xFFFF);
    if (pt > -1) {
        const unsigned char *star = glyph(g_f1, 20);
        if (star) cc_view_draw_glyph(star, 25 * CELL_W + sub, y);
        format_u32((unsigned int)pt, 2, buf);
        x = 26 * CELL_W + sub;
        for (int i = 0; i < 2; i++) {
            if (buf[i] != ' ') {
                const unsigned char *g = glyph(g_spl, buf[i] - '0' + 8);
                if (g) cc_view_draw_glyph(g, x, y);
            }
            x += CELL_W;
        }
        if (star) cc_view_draw_glyph(star, 28 * CELL_W + sub, y);
    }
}

/* Oracle-exact HUD for the standalone fixture: the original draws at
 * x + ([0x512E] & 7) because the EGA horizontal pixel-pan register shifts the
 * whole display by that amount. */
void cc_hud_render(void) {
    hud_render_sub((unsigned char)(gs(0x512e) & 7));
}

/* Composite game frame: the viewport crop already applies the full camera
 * scroll (including the fine 0..7 pixels), so the HUD is drawn at its fixed
 * screen position.  Adding the pan offset here would make the status row
 * jitter against the scrolling map. */
void cc_hud_render_screen(void) {
    hud_render_sub(0);
}
