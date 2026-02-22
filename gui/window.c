#include "window.h"
#include "graphics.h"
#include "../kernel/mm/heap.h"
#include "../kernel/lib/string.h"
#include "../kernel/lib/printf.h"
#include "../kernel/drivers/framebuffer.h"

/* Window colors */
#define TITLEBAR_FOCUSED    0xFF1A3366  /* Dark blue */
#define TITLEBAR_UNFOCUSED  0xFF333344  /* Gray-blue */
#define TITLEBAR_TEXT       0xFFFFFFFF  /* White */
#define CLOSE_BTN_BG       0xFFCC3333  /* Red */
#define CLOSE_BTN_HOVER    0xFFFF4444  /* Bright red */
#define CLOSE_BTN_TEXT     0xFFFFFFFF  /* White */
#define WIN_BORDER_COLOR   0xFF555566  /* Gray */
#define WIN_CLIENT_BG      0xFF1A1A2E  /* Dark background */

/* Z-ordered window list (index 0 = backmost) */
static window_t windows[MAX_WINDOWS];
static window_t *z_order[MAX_WINDOWS];
static uint32_t  win_count = 0;
static uint32_t  next_id = 1;

void wm_init(void) {
    memset(windows, 0, sizeof(windows));
    memset(z_order, 0, sizeof(z_order));
    win_count = 0;
    next_id = 1;
}

window_t *wm_create_window(const char *title, int16_t x, int16_t y,
                            uint16_t w, uint16_t h,
                            win_event_fn on_event, win_paint_fn on_paint) {
    if (win_count >= MAX_WINDOWS) return NULL;

    /* Find a free slot */
    window_t *win = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].id == 0) {
            win = &windows[i];
            break;
        }
    }
    if (!win) return NULL;

    win->id = next_id++;
    strncpy(win->title, title, WIN_TITLE_MAX - 1);
    win->title[WIN_TITLE_MAX - 1] = '\0';
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->client_w = w - 2 * BORDER_WIDTH;
    win->client_h = h - TITLEBAR_HEIGHT - 2 * BORDER_WIDTH;
    win->visible = true;
    win->focused = false;
    win->dragging = false;
    win->close_hovered = false;
    win->on_event = on_event;
    win->on_paint = on_paint;
    win->user_data = NULL;

    /* Allocate client canvas */
    size_t canvas_size = (size_t)win->client_w * win->client_h * sizeof(uint32_t);
    win->canvas = (uint32_t *)kmalloc(canvas_size);
    if (win->canvas) {
        /* Fill with client background */
        for (uint32_t i = 0; i < (uint32_t)win->client_w * win->client_h; i++) {
            win->canvas[i] = WIN_CLIENT_BG;
        }
    }

    /* Add to z-order (on top) */
    z_order[win_count] = win;
    win_count++;

    /* Focus this window */
    for (uint32_t i = 0; i < win_count; i++) {
        z_order[i]->focused = false;
    }
    win->focused = true;

    kprintf("[WM] Created window '%s' (id=%u, %ux%u)\n", title, win->id, w, h);
    return win;
}

void wm_destroy_window(uint32_t id) {
    /* Find and remove from z_order */
    int idx = -1;
    for (uint32_t i = 0; i < win_count; i++) {
        if (z_order[i]->id == id) {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0) return;

    window_t *win = z_order[idx];

    /* Free canvas */
    if (win->canvas) {
        kfree(win->canvas);
    }

    /* Shift z_order down */
    for (uint32_t i = idx; i + 1 < win_count; i++) {
        z_order[i] = z_order[i + 1];
    }
    win_count--;

    /* Clear slot */
    memset(win, 0, sizeof(window_t));

    /* Focus topmost */
    if (win_count > 0) {
        z_order[win_count - 1]->focused = true;
    }
}

void wm_bring_to_front(uint32_t id) {
    int idx = -1;
    for (uint32_t i = 0; i < win_count; i++) {
        if (z_order[i]->id == id) {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0 || (uint32_t)idx == win_count - 1) return;

    window_t *win = z_order[idx];

    /* Shift everything above down */
    for (uint32_t i = idx; i + 1 < win_count; i++) {
        z_order[i] = z_order[i + 1];
    }
    z_order[win_count - 1] = win;

    /* Update focus */
    for (uint32_t i = 0; i < win_count; i++) {
        z_order[i]->focused = false;
    }
    win->focused = true;
}

/* Check if point (px,py) is inside the close button of a window */
static bool hit_close_button(window_t *win, int16_t px, int16_t py) {
    int16_t btn_x = win->x + win->width - TITLEBAR_HEIGHT;
    int16_t btn_y = win->y;
    return (px >= btn_x && px < win->x + win->width &&
            py >= btn_y && py < btn_y + TITLEBAR_HEIGHT);
}

/* Check if point is inside the minimize button (left of close button) */
static bool hit_minimize_button(window_t *win, int16_t px, int16_t py) {
    int16_t btn_x = win->x + win->width - 2 * TITLEBAR_HEIGHT;
    int16_t btn_y = win->y;
    return (px >= btn_x && px < btn_x + TITLEBAR_HEIGHT &&
            py >= btn_y && py < btn_y + TITLEBAR_HEIGHT);
}

/* Check if point is in titlebar (excluding close and minimize buttons) */
static bool hit_titlebar(window_t *win, int16_t px, int16_t py) {
    return (px >= win->x && px < win->x + win->width - 2 * TITLEBAR_HEIGHT &&
            py >= win->y && py < win->y + TITLEBAR_HEIGHT);
}

/* Check if point is inside window bounds */
static bool hit_window(window_t *win, int16_t px, int16_t py) {
    return (px >= win->x && px < win->x + win->width &&
            py >= win->y && py < win->y + win->height);
}

void wm_dispatch_event(gui_event_t *ev) {
    int16_t mx = ev->mouse_x;
    int16_t my = ev->mouse_y;

    /* Handle window dragging */
    if (ev->type == EVT_MOUSE_MOVE) {
        for (uint32_t i = 0; i < win_count; i++) {
            window_t *win = z_order[i];
            if (win->dragging) {
                win->x = mx - win->drag_ox;
                win->y = my - win->drag_oy;
                return;
            }
            /* Update close/minimize button hover state */
            if (hit_window(win, mx, my)) {
                win->close_hovered = hit_close_button(win, mx, my);
                win->minimize_hovered = hit_minimize_button(win, mx, my);
            } else {
                win->close_hovered = false;
                win->minimize_hovered = false;
            }
        }
    }

    if (ev->type == EVT_MOUSE_UP) {
        /* Stop dragging */
        for (uint32_t i = 0; i < win_count; i++) {
            z_order[i]->dragging = false;
        }
    }

    if (ev->type == EVT_MOUSE_DOWN) {
        /* Hit test from front to back */
        for (int i = (int)win_count - 1; i >= 0; i--) {
            window_t *win = z_order[i];
            if (!win->visible || !hit_window(win, mx, my)) continue;

            /* Bring to front */
            wm_bring_to_front(win->id);

            /* Close button? */
            if (hit_close_button(win, mx, my)) {
                gui_event_t close_ev = {0};
                close_ev.type = EVT_CLOSE;
                close_ev.target_window = win->id;
                if (win->on_event) win->on_event(win, &close_ev);
                return;
            }

            /* Minimize button? */
            if (hit_minimize_button(win, mx, my)) {
                win->visible = false;
                win->minimized = true;
                win->focused = false;
                /* Focus next topmost visible window */
                for (int j = (int)win_count - 1; j >= 0; j--) {
                    if (z_order[j]->visible) {
                        z_order[j]->focused = true;
                        break;
                    }
                }
                return;
            }

            /* Titlebar drag? */
            if (hit_titlebar(win, mx, my)) {
                win->dragging = true;
                win->drag_ox = mx - win->x;
                win->drag_oy = my - win->y;
                return;
            }

            /* Client area: translate to local coordinates and forward */
            int16_t local_x = mx - win->x - BORDER_WIDTH;
            int16_t local_y = my - win->y - TITLEBAR_HEIGHT - BORDER_WIDTH;
            if (local_x >= 0 && local_y >= 0 &&
                local_x < win->client_w && local_y < win->client_h) {
                ev->mouse_x = local_x;
                ev->mouse_y = local_y;
                ev->target_window = win->id;
                if (win->on_event) win->on_event(win, ev);
            }
            return;
        }
    }

    if (ev->type == EVT_KEY_DOWN) {
        /* Forward to focused window */
        window_t *focused = wm_get_focused();
        if (focused && focused->on_event) {
            ev->target_window = focused->id;
            focused->on_event(focused, ev);
        }
    }
}

window_t *wm_get_focused(void) {
    for (int i = (int)win_count - 1; i >= 0; i--) {
        if (z_order[i]->focused) return z_order[i];
    }
    return NULL;
}

window_t *wm_get_window(uint32_t id) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].id == id) return &windows[i];
    }
    return NULL;
}

window_t **wm_get_window_list(uint32_t *count) {
    *count = win_count;
    return z_order;
}

void wm_clear_canvas(window_t *win, uint32_t color) {
    if (!win || !win->canvas) return;
    for (uint32_t i = 0; i < (uint32_t)win->client_w * win->client_h; i++) {
        win->canvas[i] = color;
    }
}
