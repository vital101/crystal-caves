/* Crystal Caves render core - tiny freestanding runtime.
 */
#include "runtime.h"

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
static unsigned char g_heap[4 << 20];
static unsigned int g_heap_top = 0;

void *cc_alloc(unsigned size) {
    size = (size + 15u) & ~15u;
    if (g_heap_top + size > sizeof(g_heap)) return 0;
    void *p = &g_heap[g_heap_top];
    g_heap_top += size;
    return p;
}
