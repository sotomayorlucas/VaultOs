#ifndef VAULTOS_MOUSE_H
#define VAULTOS_MOUSE_H

#include "../lib/types.h"

/* Mouse button bits */
#define MOUSE_BTN_LEFT   (1 << 0)
#define MOUSE_BTN_RIGHT  (1 << 1)
#define MOUSE_BTN_MIDDLE (1 << 2)

typedef struct {
    int16_t  dx, dy;         /* Relative movement */
    uint8_t  buttons;        /* Button state bitmask */
} mouse_event_t;

/* Initialize PS/2 mouse (enables IRQ 12) */
void mouse_init(void);

/* Non-blocking poll: returns true if an event was available */
bool mouse_poll(mouse_event_t *ev);

/* Get current absolute mouse position */
void mouse_get_position(int16_t *x, int16_t *y);

/* Set screen bounds for absolute position tracking */
void mouse_set_bounds(uint16_t width, uint16_t height);

#endif /* VAULTOS_MOUSE_H */
