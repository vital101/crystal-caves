/* Crystal Caves render core - blitters (Phase 2 split).
 *
 * shape_ptr/blit_to/blit are the planar-GFX blit pipeline; cc_draw_object/
 * cc_draw_object_view draw one object sprite and cc_view_draw_glyph blits
 * an 8x8 font glyph into the 320x200 view.  blit_to is file-local;
 * shape_ptr and blit are shared with render.c via this header.
 */
#ifndef RENDER_BLIT_H
#define RENDER_BLIT_H

#include "render_internal.h"

/* 1-based block/shape -> 160-byte planar shape, or 0. */
const unsigned char *shape_ptr(int block, int shape);

/* Blit a planar shape into the 640x400 level canvas. */
void blit(const unsigned char *shape, int px, int py);

#endif /* RENDER_BLIT_H */
