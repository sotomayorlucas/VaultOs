#ifndef VAULTOS_WIDGETS_H
#define VAULTOS_WIDGETS_H

#include "../kernel/lib/types.h"
#include "window.h"
#include "event.h"

#define WIDGET_TEXT_MAX  128
#define LISTVIEW_MAX_ITEMS 256
#define LISTVIEW_ITEM_MAX  128

typedef enum {
    W_LABEL,
    W_BUTTON,
    W_TEXTBOX,
    W_LISTVIEW
} widget_type_t;

typedef struct widget widget_t;

typedef void (*widget_click_fn)(widget_t *w, window_t *win);

struct widget {
    widget_type_t type;
    int16_t   x, y, w, h;          /* Position relative to window client area */
    char      text[WIDGET_TEXT_MAX];
    bool      focused;
    bool      hovered;
    bool      pressed;
    uint32_t  fg, bg;

    /* Callbacks */
    widget_click_fn on_click;

    /* TextBox state */
    uint16_t  cursor_pos;
    uint16_t  text_len;

    /* ListView state */
    char     (*lv_items)[LISTVIEW_ITEM_MAX]; /* Pointer to item array */
    uint16_t  lv_count;
    int16_t   lv_selected;
    int16_t   lv_scroll;
    void     (*on_select)(widget_t *w, window_t *win, int16_t index);

    /* Linked list */
    widget_t *next;
};

/* Create widgets (returns pointer to static pool) */
widget_t *widget_create_label(int16_t x, int16_t y, const char *text,
                               uint32_t fg, uint32_t bg);
widget_t *widget_create_button(int16_t x, int16_t y, int16_t w, int16_t h,
                                const char *text, widget_click_fn on_click);
widget_t *widget_create_textbox(int16_t x, int16_t y, int16_t w, int16_t h);
widget_t *widget_create_listview(int16_t x, int16_t y, int16_t w, int16_t h);

/* Add a widget to a window's widget chain */
void widget_add(window_t *win, widget_t *w);

/* Draw all widgets for a window */
void widget_draw_all(window_t *win);

/* Dispatch mouse/keyboard events to widgets */
void widget_dispatch(window_t *win, gui_event_t *ev);

/* ListView helpers */
void listview_clear(widget_t *lv);
void listview_add_item(widget_t *lv, const char *text);
int16_t listview_get_selected(widget_t *lv);

/* TextBox helpers */
const char *textbox_get_text(widget_t *tb);
void textbox_set_text(widget_t *tb, const char *text);
void textbox_clear(widget_t *tb);

#endif /* VAULTOS_WIDGETS_H */
