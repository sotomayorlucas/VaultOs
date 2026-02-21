#include "hmac.h"
#include "sha256.h"
#include "../lib/string.h"

void hmac_sha256(const uint8_t *key, size_t key_len,
                  const uint8_t *data, size_t data_len,
                  uint8_t mac[32]) {
    uint8_t key_block[SHA256_BLOCK_SIZE];
    uint8_t ipad[SHA256_BLOCK_SIZE];
    uint8_t opad[SHA256_BLOCK_SIZE];
    uint8_t inner_hash[SHA256_DIGEST_SIZE];
    sha256_ctx_t ctx;

    /* If key > block size, hash it first */
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256(key, key_len, key_block);
        memset(key_block + SHA256_DIGEST_SIZE, 0,
               SHA256_BLOCK_SIZE - SHA256_DIGEST_SIZE);
    } else {
        memcpy(key_block, key, key_len);
        memset(key_block + key_len, 0, SHA256_BLOCK_SIZE - key_len);
    }

    /* Inner padding */
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5C;
    }

    /* Inner hash: SHA256(ipad || data) */
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    /* Outer hash: SHA256(opad || inner_hash) */
    sha256_init(&ctx);
    sha256_update(&ctx, opad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner_hash, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, mac);
}

bool hmac_verify(const uint8_t *expected, const uint8_t *computed, size_t len) {
    /* Constant-time comparison to prevent timing attacks */
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= expected[i] ^ computed[i];
    }
    return diff == 0;
}
