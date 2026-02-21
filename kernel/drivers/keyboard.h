#ifndef VAULTOS_KEYBOARD_H
#define VAULTOS_KEYBOARD_H

#include "../lib/types.h"

#define KB_BUFFER_SIZE 256

void  keyboard_init(void);
char  keyboard_getchar(void);       /* Blocking: waits for input */
bool  keyboard_has_input(void);     /* Non-blocking check */
char  keyboard_getchar_nonblock(void); /* Returns 0 if no input */

#endif /* VAULTOS_KEYBOARD_H */
