#ifndef VAULTOS_WINDOW_H
#define VAULTOS_WINDOW_H

#include "../kernel/lib/types.h"
#include "event.h"

#define MAX_WINDOWS      16
#define TITLEBAR_HEIGHT  20
#define BORDER_WIDTH     1
#define WIN_TITLE_MAX    64

/* Window event callback */
typedef struct window window_t;
typedef void (*win_event_fn)(window_t *win, gui_event_t *ev);
typedef void (*win_paint_fn)(window_t *win);

struct window {
    uint32_t  id;
    char      title[WIN_TITLE_MAX];
    int16_t   x, y;               /* Position of outer frame */
    uint16_t  width, height;      /* Total size including decoration */
    uint16_t  client_w, client_h; /* Usable client area */
    uint32_t *canvas;             /* Client area pixel buffer */
    bool      visible;
    bool      focused;
    bool      dragging;
    int16_t   drag_ox, drag_oy;   /* Mouse offset during drag */
    bool      close_hovered;      /* Close button hover state */
    win_event_fn on_event;
    win_paint_fn on_paint;
    void     *user_data;
};

/* Initialize window manager */
void wm_init(void);

/* Create a window (returns window pointer, or NULL on failure) */
window_t *wm_create_window(const char *title, int16_t x, int16_t y,
                            uint16_t w, uint16_t h,
                            win_event_fn on_event, win_paint_fn on_paint);

/* Destroy a window */
void wm_destroy_window(uint32_t id);

/* Bring window to front */
void wm_bring_to_front(uint32_t id);

/* Dispatch a GUI event to the appropriate window */
void wm_dispatch_event(gui_event_t *ev);

/* Get the focused window (or NULL) */
window_t *wm_get_focused(void);

/* Get window by ID (or NULL) */
window_t *wm_get_window(uint32_t id);

/* Get window list for rendering (back to front order) */
window_t **wm_get_window_list(uint32_t *count);

/* Clear the client canvas of a window */
void wm_clear_canvas(window_t *win, uint32_t color);

#endif /* VAULTOS_WINDOW_H */
