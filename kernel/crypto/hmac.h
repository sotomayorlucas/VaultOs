#ifndef VAULTOS_HMAC_H
#define VAULTOS_HMAC_H

#include "../lib/types.h"

#define HMAC_SHA256_SIZE 32

void hmac_sha256(const uint8_t *key, size_t key_len,
                  const uint8_t *data, size_t data_len,
                  uint8_t mac[32]);

/* Constant-time comparison (prevents timing attacks) */
bool hmac_verify(const uint8_t *expected, const uint8_t *computed, size_t len);

#endif /* VAULTOS_HMAC_H */
