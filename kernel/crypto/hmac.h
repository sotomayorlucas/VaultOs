#ifndef VAULTOS_HMAC_H
#define VAULTOS_HMAC_H

#include "../lib/types.h"
#include "sha256.h"

#define HMAC_SHA256_SIZE 32

/* Pre-computed HMAC context: caches SHA-256 state after ipad/opad processing.
   Eliminates re-hashing 128 bytes (ipad+opad) per HMAC when key is reused. */
typedef struct {
    sha256_ctx_t inner_base;  /* SHA-256 state after processing ipad */
    sha256_ctx_t outer_base;  /* SHA-256 state after processing opad */
} hmac_ctx_t;

/* Initialize pre-computed HMAC context for a fixed key */
void hmac_ctx_init(hmac_ctx_t *ctx, const uint8_t *key, size_t key_len);

/* Compute HMAC using pre-computed context (fast: skips ipad/opad hashing) */
void hmac_ctx_compute(const hmac_ctx_t *ctx,
                       const uint8_t *data, size_t data_len,
                       uint8_t mac[32]);

/* One-shot HMAC (convenience, uses hmac_ctx internally) */
void hmac_sha256(const uint8_t *key, size_t key_len,
                  const uint8_t *data, size_t data_len,
                  uint8_t mac[32]);

/* Constant-time comparison (prevents timing attacks) */
bool hmac_verify(const uint8_t *expected, const uint8_t *computed, size_t len);

#endif /* VAULTOS_HMAC_H */
