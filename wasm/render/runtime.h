/* Crystal Caves render core - freestanding runtime (Phase 2 split).
 *
 * memset/memcpy replacements for -nostdlib and the bump allocator over the
 * static arena that JS copies assets into.
 */
#ifndef RENDER_RUNTIME_H
#define RENDER_RUNTIME_H

#ifdef CC_HOST_LIBC
/* Native host build (native/): use the platform libc instead of the
 * hand-written freestanding copies in runtime.c (the wasm build defines
 * neither and stays -nostdlib). */
#include <string.h>
#else
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
#endif

/* bump allocator over a static arena (JS copies assets in via cc_alloc). */
void *cc_alloc(unsigned size);

#endif /* RENDER_RUNTIME_H */
