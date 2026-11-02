/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"

/* Handler offset (behaviour ID, segment 0x1F9A) for slot i. */
uint16_t cc_obj_handler(int i)  { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].handler : 0; }

/* Coverage metric: fraction of active (non-empty, non-inert-state) object
 * slots whose handler has a real implementation rather than the inert
 * fallback.  Used to track handler porting progress (Phase 5). */
int is_real_handler(uint16_t off) {
    switch (off) {
        case 0x1fb9: case 0x2241: case 0x2850: case 0x2877:
        case 0x831: case 0x1fc2: case 0x2176: case 0x323c: case 0x3be8:
        case 0x3d23: case 0x3e70: case 0x3f13: case 0x4a4b: case 0x4a82:
        case 0x298a: case 0x2d37: case 0x3416: case 0x2acf: case 0x2bd4:
        case 0x403a: case 0x55ec: case 0x62f2: case 0x6232: case 0x4c92:
        case 0x5c8f: case 0x4eeb: case 0x52e7: case 0x2f83: case 0x4ad3:
        case 0x36b0: case 0x3a2e: case 0x3f6c: case 0x2e25:
        case 0x3904: case 0x4145: case 0x47b9: case 0x32f6:
        case 0x48fe: case 0x4fd4: case 0x6458:
        case 0x5855: case 0x5bbe: case 0x6681: case 0x672b: case 0x69d3:
        case 0x6afa: case 0x6d31: case 0x6ff6: case 0x70e4:
            return 1;
        default: return 0;
    }
}
int cc_handler_coverage_num(void) {
    int n = 0, d = 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        if (g_state.obj[i].kind == 0) continue;
        if (g_state.obj[i].state == 2) continue;
        d++;
        if (is_real_handler(g_state.obj[i].handler)) n++;
    }
    return d ? (n * 100) / d : 100;
}
