#ifndef VAULTOS_KEYBOARD_H
#define VAULTOS_KEYBOARD_H

#include "../lib/types.h"

#define KB_BUFFER_SIZE 256

/* Extended key codes (0x80-0x99) - safe range, unused by ASCII */
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_HOME    0x84
#define KEY_END     0x85
#define KEY_DELETE  0x86
#define KEY_INSERT  0x87
#define KEY_PGUP    0x88
#define KEY_PGDN    0x89

#define KEY_F1      0x90
#define KEY_F2      0x91
#define KEY_F3      0x92
#define KEY_F4      0x93
#define KEY_F5      0x94
#define KEY_F6      0x95
#define KEY_F7      0x96
#define KEY_F8      0x97
#define KEY_F9      0x98
#define KEY_F10     0x99

void  keyboard_init(void);
char  keyboard_getchar(void);       /* Blocking: waits for input */
bool  keyboard_has_input(void);     /* Non-blocking check */
char  keyboard_getchar_nonblock(void); /* Returns 0 if no input */
bool  keyboard_ctrl_held(void);     /* Query Ctrl modifier state */

#endif /* VAULTOS_KEYBOARD_H */
