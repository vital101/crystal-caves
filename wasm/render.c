/* Crystal Caves Episode 1 - wasm render core (original, clean-room).
 *
 * Freestanding: compiles with either `emcc` or `clang --target=wasm32
 * -nostdlib` (linked by wasm-ld).  Consumes the portable intermediate from
 * work/tools/export_assets.py:
 *   gfx.bin     CC1.GFX (23 blocks x 8003 B; block & shape indices are 1-based)
 *   levels.bin  19 levels x 40*25 cells x 6 B [bA,sA,bB,sB,bC,sC]
 *   palette   32 x uint32 0x00RRGGBB (loaded via cc_set_palette)
 *
 * A shape is 16x16, 5 EGA-style bitplanes, 32 bytes/plane.  This only
 * rasterises the baked tile map; gameplay is a later phase. */

#define BLK_SIZE      8003
#define SHAPE_BYTES   160
#define SHAPE_W       16
#define SHAPE_H       16
#define PLANES        5
#define BLOCKS        23
#define LEVEL_W       40
#define LEVEL_H       25
#define LEVELS        19
#define CELL_BYTES    6
#define TILE_W        640
#define TILE_H        400

/* --- tiny freestanding runtime ------------------------------------------ */
void *memset(void *d, int c, unsigned n) {
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned n) {
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}

/* bump allocator over a static arena (JS copies assets in via cc_alloc). */
static unsigned char g_heap[1 << 20];
static unsigned int g_heap_top = 0;

void *cc_alloc(unsigned size) {
    size = (size + 15u) & ~15u;
    if (g_heap_top + size > sizeof(g_heap)) return 0;
    void *p = &g_heap[g_heap_top];
    g_heap_top += size;
    return p;
}

/* --- render state ------------------------------------------------------- */
static const unsigned char *g_gfx = 0;
static const unsigned char *g_levels = 0;
static unsigned int g_palette[32];
static unsigned char g_rgba[TILE_W * TILE_H * 4];

void cc_set_assets(const unsigned char *gfx, const unsigned char *levels) {
    g_gfx = gfx;
    g_levels = levels;
}
void cc_set_palette(const unsigned int *pal32) {
    memcpy(g_palette, pal32, sizeof(g_palette));
}
unsigned char *cc_framebuffer(void) { return g_rgba; }
int cc_tex_width(void)  { return TILE_W; }
int cc_tex_height(void) { return TILE_H; }

/* 1-based block/shape -> 160-byte planar shape, or 0. */
static const unsigned char *shape_ptr(int block, int shape) {
    if (block < 1 || block > BLOCKS || shape < 1 || shape > 50) return 0;
    return g_gfx + (unsigned)(block - 1) * BLK_SIZE + 3
                  + (unsigned)(shape - 1) * SHAPE_BYTES;
}

static void blit(const unsigned char *shape, int px, int py) {
    for (int y = 0; y < SHAPE_H; y++) {
        int fy = py + y;
        if (fy < 0 || fy >= TILE_H) continue;
        for (int x = 0; x < SHAPE_W; x++) {
            int fx = px + x;
            if (fx < 0 || fx >= TILE_W) continue;
            int ci = 0;
            for (int p = 0; p < PLANES; p++) {
                const unsigned char *plane = shape + p * (SHAPE_BYTES / PLANES);
                unsigned char byte = plane[y * (SHAPE_W / 8) + (x >> 3)];
                if (byte & (0x80u >> (x & 7))) ci |= (1 << p);
            }
            unsigned int c = g_palette[ci & 31];
            unsigned char *d = &g_rgba[(fy * TILE_W + fx) * 4];
            d[0] = (c >> 16) & 0xFF; d[1] = (c >> 8) & 0xFF;
            d[2] = c & 0xFF; d[3] = 0xFF;
        }
    }
}

/* Render a baked level: layer B, then A, then C (matching the original). */
void cc_render_level(int level, unsigned char *out_rgba) {
    memset(g_rgba, 0, sizeof(g_rgba));
    if (!g_gfx || !g_levels) { memcpy(out_rgba, g_rgba, sizeof(g_rgba)); return; }
    if (level < 0 || level >= LEVELS) level = 0;

    const unsigned char *cells =
        g_levels + (unsigned)level * LEVEL_W * LEVEL_H * CELL_BYTES;
    for (int r = 0; r < LEVEL_H; r++) {
        for (int c = 0; c < LEVEL_W; c++) {
            const unsigned char *cell = cells + (unsigned)(r * LEVEL_W + c) * CELL_BYTES;
            /* cell layout: [bA,sA,bB,sB,bC,sC]; draw order B, A, C. */
            int bA = cell[0], sA = cell[1], bB = cell[2], sB = cell[3];
            int bC = cell[4], sC = cell[5];
            int px = c * SHAPE_W, py = r * SHAPE_H;
            const unsigned char *s;
            if ((s = shape_ptr(bB, sB))) blit(s, px, py);
            if ((s = shape_ptr(bA, sA))) blit(s, px, py);
            if ((s = shape_ptr(bC, sC))) blit(s, px, py);
        }
    }
    memcpy(out_rgba, g_rgba, sizeof(g_rgba));
}
