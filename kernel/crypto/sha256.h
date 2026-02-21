#ifndef VAULTOS_SHA256_H
#define VAULTOS_SHA256_H

#include "../lib/types.h"

#define SHA256_DIGEST_SIZE 32
#define SHA256_BLOCK_SIZE  64

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[64];
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[32]);

/* One-shot convenience */
void sha256(const uint8_t *data, size_t len, uint8_t digest[32]);

#endif /* VAULTOS_SHA256_H */
