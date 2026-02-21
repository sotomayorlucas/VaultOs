#include "event.h"
#include "../kernel/drivers/mouse.h"
#include "../kernel/drivers/keyboard.h"

#define EVENT_BUF_SIZE 256

static gui_event_t evt_buf[EVENT_BUF_SIZE];
static volatile uint32_t evt_head = 0;
static volatile uint32_t evt_tail = 0;

/* Track previous button state to detect press/release */
static uint8_t prev_buttons = 0;

void event_init(void) {
    evt_head = 0;
    evt_tail = 0;
    prev_buttons = 0;
}

void event_push(const gui_event_t *ev) {
    uint32_t next = (evt_head + 1) % EVENT_BUF_SIZE;
    if (next == evt_tail) return; /* Buffer full, drop event */

    evt_buf[evt_head] = *ev;
    evt_head = next;
}

bool event_poll(gui_event_t *ev) {
    if (evt_tail == evt_head) return false;

    *ev = evt_buf[evt_tail];
    evt_tail = (evt_tail + 1) % EVENT_BUF_SIZE;
    return true;
}

void event_pump(void) {
    /* Process mouse events */
    mouse_event_t mev;
    while (mouse_poll(&mev)) {
        int16_t mx, my;
        mouse_get_position(&mx, &my);

        /* Generate move event if there was movement */
        if (mev.dx != 0 || mev.dy != 0) {
            gui_event_t ev = {0};
            ev.type = EVT_MOUSE_MOVE;
            ev.mouse_x = mx;
            ev.mouse_y = my;
            ev.mouse_buttons = mev.buttons;
            event_push(&ev);
        }

        /* Detect button changes */
        uint8_t changed = mev.buttons ^ prev_buttons;
        for (int btn = 0; btn < 3; btn++) {
            if (changed & (1 << btn)) {
                gui_event_t ev = {0};
                ev.type = (mev.buttons & (1 << btn)) ? EVT_MOUSE_DOWN : EVT_MOUSE_UP;
                ev.mouse_x = mx;
                ev.mouse_y = my;
                ev.mouse_button = (1 << btn);
                ev.mouse_buttons = mev.buttons;
                event_push(&ev);
            }
        }
        prev_buttons = mev.buttons;
    }

    /* Process keyboard events */
    char key;
    while ((key = keyboard_getchar_nonblock()) != 0) {
        gui_event_t ev = {0};
        ev.type = EVT_KEY_DOWN;
        ev.key = (uint8_t)key;
        event_push(&ev);
    }
}
