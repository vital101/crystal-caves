#ifndef CC_HOST_H
#define CC_HOST_H

/* Declarations for core exports that the wasm Makefile exports but the
 * public headers (render.h etc.) do not declare.  The native host uses these
 * exactly like game.html uses the wasm instance exports. */

#ifdef __cplusplus
extern "C" {
#endif

void *cc_alloc(unsigned size);

void cc_set_assets(const unsigned char *gfx, const unsigned char *levels);
void cc_set_palette(const unsigned int *pal32);

unsigned char *cc_framebuffer(void);      /* 640x400 RGBA level texture */
unsigned char *cc_view_framebuffer(void); /* 320x200 RGBA screen */
int cc_tex_width(void);
int cc_tex_height(void);
int cc_view_width(void);
int cc_view_height(void);

void cc_render_cells(int level, unsigned char *out_rgba);
void cc_render_level(int level, unsigned char *out_rgba);
void cc_render_view(int level, int cam_x, int cam_y);
void cc_draw_object(int block, int shape, int px, int py);
void cc_draw_object_view(int block, int shape, int px, int py);

#ifdef __cplusplus
}
#endif

#endif
