#ifndef VAULTOS_EVENT_H
#define VAULTOS_EVENT_H

#include "../kernel/lib/types.h"

typedef enum {
    EVT_NONE = 0,
    EVT_MOUSE_MOVE,
    EVT_MOUSE_DOWN,
    EVT_MOUSE_UP,
    EVT_KEY_DOWN,
    EVT_PAINT,
    EVT_CLOSE,
    EVT_FOCUS,
    EVT_BLUR
} event_type_t;

typedef struct {
    event_type_t type;
    int16_t  mouse_x, mouse_y;   /* Absolute screen position */
    uint8_t  mouse_button;       /* Which button changed (for DOWN/UP) */
    uint8_t  mouse_buttons;      /* Current button state bitmask */
    uint8_t  key;                /* Key code (for KEY_DOWN) */
    uint32_t target_window;      /* Window ID (0 = desktop/none) */
} gui_event_t;

/* Initialize event system */
void event_init(void);

/* Push an event into the queue */
void event_push(const gui_event_t *ev);

/* Poll an event from the queue (returns false if empty) */
bool event_poll(gui_event_t *ev);

/* Process raw mouse/keyboard input and generate GUI events */
void event_pump(void);

#endif /* VAULTOS_EVENT_H */
