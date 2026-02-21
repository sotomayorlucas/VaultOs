#ifndef VAULTOS_SERIAL_H
#define VAULTOS_SERIAL_H

#include "../lib/types.h"

#define COM1_PORT 0x3F8

void serial_init(void);
void serial_putchar(char c);
void serial_write(const char *str);

#endif /* VAULTOS_SERIAL_H */
