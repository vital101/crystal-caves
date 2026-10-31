#ifndef TEXT_H
#define TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

void cc_text_set_fonts(const unsigned char *f1, const unsigned char *f2,
                       const unsigned char *spl);
void cc_text_draw_string(int x, int y, const char *s);
void cc_text_draw_frame(int x, int y, int w, int h);
void cc_text_draw_block(int block_id, int start_line, int max_lines);
void cc_text_draw_lines(const char *const *lines, int count);
int  cc_text_draw_block_cursor(int block_id, int start_line, int max_lines,
                               int frame);
int  cc_text_draw_lines_cursor(const char *const *lines, int count, int frame);
void cc_text_cursor_draw(int xcell, int ycell, int frame);
int  cc_text_block_lines(int block_id);
void cc_hud_tick(void);
void cc_hud_render(void);

#ifdef __cplusplus
}
#endif

#endif
