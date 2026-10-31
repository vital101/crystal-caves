#ifndef RENDER_H
#define RENDER_H

#ifdef __cplusplus
extern "C" {
#endif

void cc_set_screens(const unsigned char *screens);
void cc_draw_screen(int idx);
void cc_draw_screen_rows(int top, int bottom, int row);
void cc_view_clear(int color);
void cc_view_fill_rect(int x, int y, int w, int h, int color);
void cc_view_set_pixel(int x, int y, int color);
void cc_view_draw_glyph(const unsigned char *glyph, int x, int y);
void cc_render_view_cells(int level, int cam_x, int cam_y);

#ifdef __cplusplus
}
#endif

#endif
