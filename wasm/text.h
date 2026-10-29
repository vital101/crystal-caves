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
int  cc_text_block_lines(int block_id);
void cc_hud_render(void);

#ifdef __cplusplus
}
#endif

#endif
