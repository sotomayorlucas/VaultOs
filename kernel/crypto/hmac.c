#include "hmac.h"
#include "sha256.h"
#include "../lib/string.h"

void hmac_ctx_init(hmac_ctx_t *ctx, const uint8_t *key, size_t key_len) {
    uint8_t key_block[SHA256_BLOCK_SIZE];
    uint8_t pad[SHA256_BLOCK_SIZE];

    /* Normalize key to block size */
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256(key, key_len, key_block);
        memset(key_block + SHA256_DIGEST_SIZE, 0,
               SHA256_BLOCK_SIZE - SHA256_DIGEST_SIZE);
    } else {
        memcpy(key_block, key, key_len);
        memset(key_block + key_len, 0, SHA256_BLOCK_SIZE - key_len);
    }

    /* Pre-compute inner SHA-256 state (after processing ipad) */
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++)
        pad[i] = key_block[i] ^ 0x36;
    sha256_init(&ctx->inner_base);
    sha256_update(&ctx->inner_base, pad, SHA256_BLOCK_SIZE);

    /* Pre-compute outer SHA-256 state (after processing opad) */
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++)
        pad[i] = key_block[i] ^ 0x5C;
    sha256_init(&ctx->outer_base);
    sha256_update(&ctx->outer_base, pad, SHA256_BLOCK_SIZE);
}

void hmac_ctx_compute(const hmac_ctx_t *ctx,
                       const uint8_t *data, size_t data_len,
                       uint8_t mac[32]) {
    uint8_t inner_hash[SHA256_DIGEST_SIZE];

    /* Clone pre-computed inner state and hash data */
    sha256_ctx_t inner = ctx->inner_base;
    sha256_update(&inner, data, data_len);
    sha256_final(&inner, inner_hash);

    /* Clone pre-computed outer state and hash inner result */
    sha256_ctx_t outer = ctx->outer_base;
    sha256_update(&outer, inner_hash, SHA256_DIGEST_SIZE);
    sha256_final(&outer, mac);
}

void hmac_sha256(const uint8_t *key, size_t key_len,
                  const uint8_t *data, size_t data_len,
                  uint8_t mac[32]) {
    hmac_ctx_t ctx;
    hmac_ctx_init(&ctx, key, key_len);
    hmac_ctx_compute(&ctx, data, data_len, mac);
}

bool hmac_verify(const uint8_t *expected, const uint8_t *computed, size_t len) {
    /* Constant-time comparison to prevent timing attacks */
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= expected[i] ^ computed[i];
    }
    return diff == 0;
}
