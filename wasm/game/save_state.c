/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"

/* ---------------------------------------------------------------- save state
 * Versioned deterministic snapshot.
 *
 * v2 header (12 bytes, little-endian):
 *   0..3   magic "CCS2"
 *   4..5   version 2
 *   6..7   payload size (sizeof(cc_state_t))
 *   8..11  CRC-32/ISO-HDLC of the payload
 *   12..   raw cc_state_t image
 *
 * v1 (Phase 5) used an 8-byte "CCS1" header and a 23,540-byte payload that
 * contained host-only sound fields after the deterministic prefix.  v1 is
 * accepted and migrated by copying the first sizeof(cc_state_t) bytes. */
#define CC_SAVE_MAGIC_V1   0x31534343u /* "CCS1" */
#define CC_SAVE_MAGIC_V2   0x32534343u /* "CCS2" */
#define CC_SAVE_VERSION_V1 1u
#define CC_SAVE_VERSION_V2 2u
#define CC_SAVE_HDR_V1     8
#define CC_SAVE_HDR_V2     12
#define CC_SAVE_V1_PAYLOAD 23540

cc_state_t g_load_tmp;
int g_state_error = CC_STATE_OK;

uint16_t rd16_(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
uint32_t rd32_(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void wr16_(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)(v >> 8);
}
void wr32_(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}
uint32_t crc32_(const unsigned char *p, int n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n-- > 0) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

int state_valid_(const cc_state_t *st) {
    if (st->cur_level < 0 || st->cur_level > 18) return 0;
    if (st->next_level < -2 || st->next_level > 18) return 0;
    if (st->obj_cursor >= OBJ_MAX) return 0;
    for (int i = 0; i < 17; i++)
        if (st->cave_flags[i] > 1) return 0;
    if (st->spawn_col_override != 0xFFFF && st->spawn_col_override > 40)
        return 0;
    if (st->spawn_row_override != 0xFFFF && st->spawn_row_override > 24)
        return 0;
    if (st->exit_col < 0 || st->exit_col >= CELL_COLS) return 0;
    if (st->exit_row < 0 || st->exit_row >= CELL_ROWS) return 0;
    if (st->death_state > 3) return 0;
    return 1;
}

int cc_state_size(void) {
    int v2 = CC_SAVE_HDR_V2 + (int)sizeof(cc_state_t);
    int v1 = CC_SAVE_HDR_V1 + CC_SAVE_V1_PAYLOAD;
    return v2 > v1 ? v2 : v1;
}

int cc_state_save(unsigned char *out, int cap) {
    int need = CC_SAVE_HDR_V2 + (int)sizeof(cc_state_t);
    g_state_error = CC_STATE_OK;
    if (!out || cap < need) {
        g_state_error = CC_STATE_ERR_ARG;
        return 0;
    }
    memcpy_(out + CC_SAVE_HDR_V2, (&g_state), sizeof(cc_state_t));
    wr32_(out, CC_SAVE_MAGIC_V2);
    wr16_(out + 4, CC_SAVE_VERSION_V2);
    wr16_(out + 6, (uint16_t)sizeof(cc_state_t));
    wr32_(out + 8, crc32_(out + CC_SAVE_HDR_V2, (int)sizeof(cc_state_t)));
    return need;
}

int cc_state_load_ex(const unsigned char *in, int len, int flags) {
    g_state_error = CC_STATE_OK;
    if (!in || len < CC_SAVE_HDR_V1) {
        g_state_error = CC_STATE_ERR_ARG;
        return 0;
    }
    uint32_t magic = rd32_(in);
    uint16_t ver = rd16_(in + 4);
    uint16_t sz = rd16_(in + 6);
    if (magic == CC_SAVE_MAGIC_V2) {
        if (ver != CC_SAVE_VERSION_V2) {
            g_state_error = CC_STATE_ERR_VERSION;
            return 0;
        }
        if (sz != (uint16_t)sizeof(cc_state_t) ||
            len < CC_SAVE_HDR_V2 + (int)sz) {
            g_state_error = CC_STATE_ERR_SIZE;
            return 0;
        }
        if (rd32_(in + 8) != crc32_(in + CC_SAVE_HDR_V2, (int)sz)) {
            g_state_error = CC_STATE_ERR_CRC;
            return 0;
        }
        memcpy_(&g_load_tmp, in + CC_SAVE_HDR_V2, sizeof(cc_state_t));
    } else if (magic == CC_SAVE_MAGIC_V1) {
        if (ver != CC_SAVE_VERSION_V1) {
            g_state_error = CC_STATE_ERR_VERSION;
            return 0;
        }
        if (sz != CC_SAVE_V1_PAYLOAD || len < CC_SAVE_HDR_V1 + (int)sz) {
            g_state_error = CC_STATE_ERR_SIZE;
            return 0;
        }
        /* The v1 gameplay prefix is layout-identical; the old tail held
         * host-only sound state and dead collectible fields. */
        memcpy_(&g_load_tmp, in + CC_SAVE_HDR_V1, sizeof(cc_state_t));
    } else {
        g_state_error = CC_STATE_ERR_MAGIC;
        return 0;
    }
    if (!state_valid_(&g_load_tmp)) {
        g_state_error = CC_STATE_ERR_INVALID;
        return 0;
    }
    if ((flags & CC_STATE_LOAD_HUB_ONLY) && g_load_tmp.cur_level != 2) {
        g_state_error = CC_STATE_ERR_NOT_HUB;
        return 0;
    }
    memcpy_((&g_state), &g_load_tmp, sizeof(cc_state_t));
    cc_sound_reset();
    cc_sound_log_clear();
    return 1;
}

int cc_state_load(const unsigned char *in, int len) {
    return cc_state_load_ex(in, len, 0);
}

int cc_state_last_error(void) { return g_state_error; }
