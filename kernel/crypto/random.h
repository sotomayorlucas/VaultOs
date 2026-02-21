#ifndef VAULTOS_RANDOM_H
#define VAULTOS_RANDOM_H

#include "../lib/types.h"

void     random_init(void);
bool     random_hw_available(void);
uint64_t random_u64(void);
void     random_bytes(uint8_t *buf, size_t len);

#endif /* VAULTOS_RANDOM_H */
