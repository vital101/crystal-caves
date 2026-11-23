#ifndef CC_SHA256_H
#define CC_SHA256_H

#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint64_t len;
    unsigned char buf[64];
    unsigned buflen;
} cc_sha256;

void cc_sha256_init(cc_sha256 *c);
void cc_sha256_update(cc_sha256 *c, const void *data, unsigned len);
void cc_sha256_final(cc_sha256 *c, unsigned char out[32]);
/* One-shot helper: hex digest (64 chars + NUL) of data[0..len). */
void cc_sha256_hex(const void *data, unsigned len, char out[65]);

#endif
