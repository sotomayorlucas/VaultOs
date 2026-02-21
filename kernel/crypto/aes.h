#ifndef VAULTOS_AES_H
#define VAULTOS_AES_H

#include "../lib/types.h"

#define AES_BLOCK_SIZE 16
#define AES_KEY_SIZE   16   /* AES-128 */
#define AES_NUM_ROUNDS 10

typedef struct {
    uint8_t round_key[176]; /* Expanded key: 11 round keys * 16 bytes */
} aes_ctx_t;

void aes_init(aes_ctx_t *ctx, const uint8_t key[16]);
void aes_encrypt_block(aes_ctx_t *ctx, uint8_t block[16]);
void aes_decrypt_block(aes_ctx_t *ctx, uint8_t block[16]);

/* CBC mode */
void aes_cbc_encrypt(aes_ctx_t *ctx, const uint8_t iv[16],
                      const uint8_t *plaintext, uint8_t *ciphertext,
                      size_t length); /* length must be multiple of 16 */

void aes_cbc_decrypt(aes_ctx_t *ctx, const uint8_t iv[16],
                      const uint8_t *ciphertext, uint8_t *plaintext,
                      size_t length);

/* Padding helpers */
size_t aes_padded_size(size_t data_len);
void   aes_pkcs7_pad(uint8_t *buf, size_t data_len, size_t buf_len);
size_t aes_pkcs7_unpad(const uint8_t *buf, size_t buf_len);

#endif /* VAULTOS_AES_H */
