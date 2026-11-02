/* Crystal Caves render core - PCX screens and view primitives (Phase 2
 * split).
 *
 * The shell and text modules draw into the 320x200 view through these
 * functions (public in render.h) so they do not depend on renderer
 * internals.  view_pixel is additionally shared with blit.c for the glyph
 * blitter.
 */
#ifndef RENDER_SCREENS_H
#define RENDER_SCREENS_H

#include "render_internal.h"

/* Bounds-checked palette write into the 320x200 view canvas. */
void view_pixel(int x, int y, int color);

#endif /* RENDER_SCREENS_H */
