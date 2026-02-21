#include "widgets.h"
#include "graphics.h"
#include "../kernel/lib/string.h"
#include "../kernel/drivers/framebuffer.h"
#include "../kernel/drivers/keyboard.h"
#include "../kernel/drivers/font.h"

/* Widget colors */
#define BTN_NORMAL    0xFF2A2A4A
#define BTN_HOVER     0xFF3A3A6A
#define BTN_PRESSED   0xFF1A1A3A
#define BTN_TEXT      0xFFFFFFFF
#define BTN_BORDER    0xFF555577

#define TB_BG         0xFF0A0A1A
#define TB_BORDER     0xFF444466
#define TB_FOCUSED    0xFF6666AA
#define TB_TEXT       0xFF00DDAA
#define TB_CURSOR     0xFFFFCC00

#define LV_BG         0xFF0A0A1A
#define LV_BORDER     0xFF444466
#define LV_ITEM_BG    0xFF0A0A1A
#define LV_ITEM_SEL   0xFF1A3366
#define LV_ITEM_TEXT  0xFF00DDAA
#define LV_ITEM_H     18

/* Static widget pool */
#define MAX_WIDGETS 128
static widget_t widget_pool[MAX_WIDGETS];
static uint32_t widget_count = 0;

/* Allocate a widget from the pool */
static widget_t *alloc_widget(void) {
    if (widget_count >= MAX_WIDGETS) return NULL;
    widget_t *w = &widget_pool[widget_count++];
    memset(w, 0, sizeof(widget_t));
    return w;
}

widget_t *widget_create_label(int16_t x, int16_t y, const char *text,
                               uint32_t fg, uint32_t bg) {
    widget_t *w = alloc_widget();
    if (!w) return NULL;
    w->type = W_LABEL;
    w->x = x;
    w->y = y;
    w->w = (int16_t)(strlen(text) * FONT_WIDTH);
    w->h = FONT_HEIGHT;
    w->fg = fg;
    w->bg = bg;
    strncpy(w->text, text, WIDGET_TEXT_MAX - 1);
    return w;
}

widget_t *widget_create_button(int16_t x, int16_t y, int16_t w, int16_t h,
                                const char *text, widget_click_fn on_click) {
    widget_t *wid = alloc_widget();
    if (!wid) return NULL;
    wid->type = W_BUTTON;
    wid->x = x;
    wid->y = y;
    wid->w = w;
    wid->h = h;
    wid->fg = BTN_TEXT;
    wid->bg = BTN_NORMAL;
    wid->on_click = on_click;
    strncpy(wid->text, text, WIDGET_TEXT_MAX - 1);
    return wid;
}

widget_t *widget_create_textbox(int16_t x, int16_t y, int16_t w, int16_t h) {
    widget_t *wid = alloc_widget();
    if (!wid) return NULL;
    wid->type = W_TEXTBOX;
    wid->x = x;
    wid->y = y;
    wid->w = w;
    wid->h = h;
    wid->fg = TB_TEXT;
    wid->bg = TB_BG;
    wid->cursor_pos = 0;
    wid->text_len = 0;
    wid->text[0] = '\0';
    return wid;
}

widget_t *widget_create_listview(int16_t x, int16_t y, int16_t w, int16_t h) {
    widget_t *wid = alloc_widget();
    if (!wid) return NULL;
    wid->type = W_LISTVIEW;
    wid->x = x;
    wid->y = y;
    wid->w = w;
    wid->h = h;
    wid->fg = LV_ITEM_TEXT;
    wid->bg = LV_BG;
    wid->lv_items = NULL;
    wid->lv_count = 0;
    wid->lv_selected = -1;
    wid->lv_scroll = 0;
    return wid;
}

void widget_add(window_t *win, widget_t *w) {
    if (!win || !w) return;
    w->next = (widget_t *)win->user_data;
    win->user_data = w;
}

/* Draw a single widget to the window canvas */
static void draw_widget(widget_t *w, window_t *win) {
    if (!win->canvas) return;
    uint16_t cw = win->client_w;
    uint16_t ch = win->client_h;

    /* Helper: draw pixel to canvas with bounds check */
    #define CPIX(px, py, color) do { \
        if ((px) >= 0 && (px) < cw && (py) >= 0 && (py) < ch) \
            win->canvas[(py) * cw + (px)] = (color); \
    } while(0)

    /* Helper: draw horizontal line to canvas */
    #define CHLINE(x0, y0, width, color) do { \
        for (int16_t _i = 0; _i < (width); _i++) CPIX((x0)+_i, y0, color); \
    } while(0)

    /* Helper: draw text to canvas */
    #define CTEXT(tx, ty, str, fg_c, bg_c) do { \
        const char *_s = (str); int16_t _x = (tx); \
        while (*_s) { \
            const uint8_t *_g = font_8x16[(uint8_t)*_s]; \
            for (int _gy = 0; _gy < FONT_HEIGHT; _gy++) { \
                uint8_t _bits = _g[_gy]; \
                for (int _gx = 0; _gx < FONT_WIDTH; _gx++) { \
                    CPIX(_x + _gx, (ty) + _gy, \
                         (_bits & (0x80 >> _gx)) ? (fg_c) : (bg_c)); \
                } \
            } \
            _x += FONT_WIDTH; _s++; \
        } \
    } while(0)

    switch (w->type) {
    case W_LABEL:
        CTEXT(w->x, w->y, w->text, w->fg, w->bg);
        break;

    case W_BUTTON: {
        uint32_t bg = w->pressed ? BTN_PRESSED : (w->hovered ? BTN_HOVER : BTN_NORMAL);
        /* Fill */
        for (int16_t row = 0; row < w->h; row++)
            CHLINE(w->x, w->y + row, w->w, bg);
        /* Border */
        CHLINE(w->x, w->y, w->w, BTN_BORDER);
        CHLINE(w->x, w->y + w->h - 1, w->w, BTN_BORDER);
        for (int16_t row = 0; row < w->h; row++) {
            CPIX(w->x, w->y + row, BTN_BORDER);
            CPIX(w->x + w->w - 1, w->y + row, BTN_BORDER);
        }
        /* Centered text */
        int16_t tw = (int16_t)(strlen(w->text) * FONT_WIDTH);
        int16_t tx = w->x + (w->w - tw) / 2;
        int16_t ty = w->y + (w->h - FONT_HEIGHT) / 2;
        CTEXT(tx, ty, w->text, w->fg, bg);
        break;
    }

    case W_TEXTBOX: {
        uint32_t border = w->focused ? TB_FOCUSED : TB_BORDER;
        /* Fill background */
        for (int16_t row = 0; row < w->h; row++)
            CHLINE(w->x, w->y + row, w->w, w->bg);
        /* Border */
        CHLINE(w->x, w->y, w->w, border);
        CHLINE(w->x, w->y + w->h - 1, w->w, border);
        for (int16_t row = 0; row < w->h; row++) {
            CPIX(w->x, w->y + row, border);
            CPIX(w->x + w->w - 1, w->y + row, border);
        }
        /* Text */
        int16_t ty = w->y + (w->h - FONT_HEIGHT) / 2;
        CTEXT(w->x + 4, ty, w->text, w->fg, w->bg);
        /* Cursor */
        if (w->focused) {
            int16_t cx = w->x + 4 + w->cursor_pos * FONT_WIDTH;
            for (int16_t row = 0; row < FONT_HEIGHT; row++)
                CPIX(cx, ty + row, TB_CURSOR);
        }
        break;
    }

    case W_LISTVIEW: {
        /* Fill background */
        for (int16_t row = 0; row < w->h; row++)
            CHLINE(w->x, w->y + row, w->w, w->bg);
        /* Border */
        CHLINE(w->x, w->y, w->w, LV_BORDER);
        CHLINE(w->x, w->y + w->h - 1, w->w, LV_BORDER);
        for (int16_t row = 0; row < w->h; row++) {
            CPIX(w->x, w->y + row, LV_BORDER);
            CPIX(w->x + w->w - 1, w->y + row, LV_BORDER);
        }
        /* Items */
        if (w->lv_items) {
            int16_t visible = (w->h - 2) / LV_ITEM_H;
            for (int16_t i = 0; i < visible && (i + w->lv_scroll) < w->lv_count; i++) {
                int16_t idx = i + w->lv_scroll;
                int16_t iy = w->y + 1 + i * LV_ITEM_H;
                uint32_t item_bg = (idx == w->lv_selected) ? LV_ITEM_SEL : LV_ITEM_BG;
                for (int16_t row = 0; row < LV_ITEM_H; row++)
                    CHLINE(w->x + 1, iy + row, w->w - 2, item_bg);
                CTEXT(w->x + 4, iy + 1, w->lv_items[idx], w->fg, item_bg);
            }
        }
        break;
    }
    }

    #undef CPIX
    #undef CHLINE
    #undef CTEXT
}

void widget_draw_all(window_t *win) {
    widget_t *w = (widget_t *)win->user_data;
    while (w) {
        draw_widget(w, win);
        w = w->next;
    }
}

/* Check if point is inside a widget */
static bool widget_hit(widget_t *w, int16_t px, int16_t py) {
    return (px >= w->x && px < w->x + w->w &&
            py >= w->y && py < w->y + w->h);
}

void widget_dispatch(window_t *win, gui_event_t *ev) {
    widget_t *w = (widget_t *)win->user_data;

    if (ev->type == EVT_MOUSE_DOWN) {
        /* Unfocus all textboxes first */
        widget_t *iter = (widget_t *)win->user_data;
        while (iter) {
            if (iter->type == W_TEXTBOX) iter->focused = false;
            iter = iter->next;
        }

        while (w) {
            if (widget_hit(w, ev->mouse_x, ev->mouse_y)) {
                if (w->type == W_BUTTON) {
                    w->pressed = true;
                    if (w->on_click) w->on_click(w, win);
                } else if (w->type == W_TEXTBOX) {
                    w->focused = true;
                } else if (w->type == W_LISTVIEW && w->lv_items) {
                    int16_t item_y = ev->mouse_y - w->y - 1;
                    int16_t idx = item_y / LV_ITEM_H + w->lv_scroll;
                    if (idx >= 0 && idx < w->lv_count) {
                        w->lv_selected = idx;
                        if (w->on_select) w->on_select(w, win, idx);
                    }
                }
                return;
            }
            w = w->next;
        }
        return;
    }

    if (ev->type == EVT_MOUSE_UP) {
        w = (widget_t *)win->user_data;
        while (w) {
            w->pressed = false;
            w = w->next;
        }
        return;
    }

    if (ev->type == EVT_MOUSE_MOVE) {
        while (w) {
            w->hovered = widget_hit(w, ev->mouse_x, ev->mouse_y);
            w = w->next;
        }
        return;
    }

    if (ev->type == EVT_KEY_DOWN) {
        /* Forward to focused textbox */
        w = (widget_t *)win->user_data;
        while (w) {
            if (w->type == W_TEXTBOX && w->focused) {
                uint8_t key = ev->key;
                if (key == '\b') {
                    /* Backspace */
                    if (w->cursor_pos > 0) {
                        memmove(&w->text[w->cursor_pos - 1],
                                &w->text[w->cursor_pos],
                                w->text_len - w->cursor_pos);
                        w->cursor_pos--;
                        w->text_len--;
                        w->text[w->text_len] = '\0';
                    }
                } else if (key == KEY_LEFT) {
                    if (w->cursor_pos > 0) w->cursor_pos--;
                } else if (key == KEY_RIGHT) {
                    if (w->cursor_pos < w->text_len) w->cursor_pos++;
                } else if (key == KEY_HOME) {
                    w->cursor_pos = 0;
                } else if (key == KEY_END) {
                    w->cursor_pos = w->text_len;
                } else if (key == KEY_DELETE) {
                    if (w->cursor_pos < w->text_len) {
                        memmove(&w->text[w->cursor_pos],
                                &w->text[w->cursor_pos + 1],
                                w->text_len - w->cursor_pos - 1);
                        w->text_len--;
                        w->text[w->text_len] = '\0';
                    }
                } else if (key >= 0x20 && key < 0x7F) {
                    /* Printable character */
                    if (w->text_len < WIDGET_TEXT_MAX - 1) {
                        memmove(&w->text[w->cursor_pos + 1],
                                &w->text[w->cursor_pos],
                                w->text_len - w->cursor_pos);
                        w->text[w->cursor_pos] = (char)key;
                        w->cursor_pos++;
                        w->text_len++;
                        w->text[w->text_len] = '\0';
                    }
                }
                return;
            }
            w = w->next;
        }

        /* Forward scroll keys to focused listview */
        w = (widget_t *)win->user_data;
        while (w) {
            if (w->type == W_LISTVIEW && w->lv_items) {
                uint8_t key = ev->key;
                if (key == KEY_UP && w->lv_selected > 0) {
                    w->lv_selected--;
                    if (w->lv_selected < w->lv_scroll) w->lv_scroll = w->lv_selected;
                } else if (key == KEY_DOWN && w->lv_selected < w->lv_count - 1) {
                    w->lv_selected++;
                    int16_t visible = (w->h - 2) / LV_ITEM_H;
                    if (w->lv_selected >= w->lv_scroll + visible)
                        w->lv_scroll = w->lv_selected - visible + 1;
                }
                return;
            }
            w = w->next;
        }
    }
}

void listview_clear(widget_t *lv) {
    lv->lv_count = 0;
    lv->lv_selected = -1;
    lv->lv_scroll = 0;
}

void listview_add_item(widget_t *lv, const char *text) {
    if (!lv->lv_items || lv->lv_count >= LISTVIEW_MAX_ITEMS) return;
    strncpy(lv->lv_items[lv->lv_count], text, LISTVIEW_ITEM_MAX - 1);
    lv->lv_items[lv->lv_count][LISTVIEW_ITEM_MAX - 1] = '\0';
    lv->lv_count++;
}

int16_t listview_get_selected(widget_t *lv) {
    return lv->lv_selected;
}

const char *textbox_get_text(widget_t *tb) {
    return tb->text;
}

void textbox_set_text(widget_t *tb, const char *text) {
    strncpy(tb->text, text, WIDGET_TEXT_MAX - 1);
    tb->text[WIDGET_TEXT_MAX - 1] = '\0';
    tb->text_len = (uint16_t)strlen(tb->text);
    tb->cursor_pos = tb->text_len;
}

void textbox_clear(widget_t *tb) {
    tb->text[0] = '\0';
    tb->text_len = 0;
    tb->cursor_pos = 0;
}
