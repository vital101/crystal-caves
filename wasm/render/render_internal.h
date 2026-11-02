/* Crystal Caves render core - internal umbrella header (Phase 2 split).
 *
 * Geometry/format constants and the shared render state (assets, palette,
 * framebuffers) used by the render/ modules.  Definitions live in
 * render.c (asset setup and buffers) and runtime.c (heap).  Public API
 * stays in render.h; this header is for render/ modules only.
 */
#ifndef RENDER_INTERNAL_H
#define RENDER_INTERNAL_H

#include "render.h"
#include "objects.h"

#define BLK_SIZE      8003
#define SHAPE_BYTES   160
#define SHAPE_W       16
#define SHAPE_H       16
#define GROUPS        2
#define PALETTE_SIZE  16
#define BLOCKS        23
#define LEVEL_W       40
#define LEVEL_H       25
#define LEVELS        19
#define CELL_BYTES    6
#define TILE_W        640
#define TILE_H        400
#define SCREEN_W      320
#define SCREEN_H      200

/* Asset pointers and palette (defined in render.c). */
extern const unsigned char *g_gfx;
extern const unsigned char *g_levels;
extern unsigned int g_palette[PALETTE_SIZE];

/* Framebuffers (defined in render.c): 640x400 level canvas and 320x200
 * view canvas, both RGBA. */
extern unsigned char g_rgba[TILE_W * TILE_H * 4];
extern unsigned char g_view[SCREEN_W * SCREEN_H * 4];

#endif /* RENDER_INTERNAL_H */
