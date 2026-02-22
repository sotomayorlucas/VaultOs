#include "compositor.h"
#include "graphics.h"
#include "window.h"
#include "../kernel/drivers/framebuffer.h"
#include "../kernel/drivers/mouse.h"
#include "../kernel/lib/string.h"

/* Mouse cursor bitmap (12x19, white with black outline) */
#define CURSOR_W 12
#define CURSOR_H 19

static const char cursor_data[CURSOR_H][CURSOR_W + 1] = {
    "X...........",
    "XX..........",
    "X#X.........",
    "X##X........",
    "X###X.......",
    "X####X......",
    "X#####X.....",
    "X######X....",
    "X#######X...",
    "X########X..",
    "X#####XXXXX.",
    "X##X##X.....",
    "X#X.X##X....",
    "XX..X##X....",
    "X....X##X...",
    ".....X##X...",
    "......X##X..",
    "......X##X..",
    ".......XX...",
};

/* Desktop gradient colors */
static uint32_t bg_top    = 0xFF0A0A2E;  /* Dark blue */
static uint32_t bg_bottom = 0xFF1A0A2E;  /* Dark purple */

/* Window decoration colors */
#define TITLEBAR_FOCUSED    0xFF1A3366
#define TITLEBAR_UNFOCUSED  0xFF333344
#define TITLEBAR_TEXT       0xFFFFFFFF
#define CLOSE_BTN_BG       0xFFCC3333
#define CLOSE_BTN_HOVER    0xFFFF4444
#define WIN_BORDER_COLOR   0xFF555566

void comp_init(void) {
    comp_mark_all_dirty();
}

void comp_mark_dirty(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    (void)x; (void)y; (void)w; (void)h;
    /* For simplicity, always do full redraws.
     * Dirty rect optimization can be added later. */
}

void comp_mark_all_dirty(void) {
    /* Nothing needed - we always redraw fully */
}

void comp_set_bg_color(uint32_t top_color, uint32_t bottom_color) {
    bg_top = top_color;
    bg_bottom = bottom_color;
}

/* Interpolate two ARGB colors */
static uint32_t lerp_color(uint32_t c1, uint32_t c2, uint32_t t, uint32_t max) {
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = r1 + (r2 - r1) * t / max;
    uint8_t g = g1 + (g2 - g1) * t / max;
    uint8_t b = b1 + (b2 - b1) * t / max;
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* Draw desktop background gradient */
static void draw_desktop(void) {
    uint16_t h = gfx_height();
    uint16_t w = gfx_width();

    for (uint16_t y = 0; y < h; y++) {
        uint32_t color = lerp_color(bg_top, bg_bottom, y, h);
        gfx_draw_hline(0, y, w, color);
    }
}

/* Draw window decorations and blit canvas */
static void draw_window(window_t *win) {
    if (!win->visible) return;

    int16_t x = win->x;
    int16_t y = win->y;
    uint16_t w = win->width;
    uint16_t h = win->height;

    /* Border */
    gfx_draw_rect(x, y, w, h, WIN_BORDER_COLOR);

    /* Titlebar */
    uint32_t tb_color = win->focused ? TITLEBAR_FOCUSED : TITLEBAR_UNFOCUSED;
    gfx_fill_rect(x + 1, y + 1, w - 2, TITLEBAR_HEIGHT - 1, tb_color);

    /* Title text (centered vertically in titlebar) */
    int16_t text_y = y + (TITLEBAR_HEIGHT - 16) / 2;
    gfx_draw_text(x + 6, text_y, win->title, TITLEBAR_TEXT, tb_color);

    /* Minimize button [_] */
    int16_t min_x = x + w - 2 * TITLEBAR_HEIGHT;
    uint32_t min_bg = win->minimize_hovered ? 0xFF444466 : 0xFF333344;
    gfx_fill_rect(min_x, y + 1, TITLEBAR_HEIGHT - 1, TITLEBAR_HEIGHT - 1, min_bg);
    gfx_draw_char(min_x + 6, text_y, '_', TITLEBAR_TEXT, min_bg);

    /* Close button [X] */
    int16_t btn_x = x + w - TITLEBAR_HEIGHT;
    uint32_t close_bg = win->close_hovered ? CLOSE_BTN_HOVER : CLOSE_BTN_BG;
    gfx_fill_rect(btn_x, y + 1, TITLEBAR_HEIGHT - 1, TITLEBAR_HEIGHT - 1, close_bg);
    gfx_draw_char(btn_x + 6, text_y, 'X', TITLEBAR_TEXT, close_bg);

    /* Client area: blit canvas */
    if (win->canvas) {
        int16_t cx = x + BORDER_WIDTH;
        int16_t cy = y + TITLEBAR_HEIGHT;
        gfx_blit(cx, cy, win->client_w, win->client_h, win->canvas);
    }
}

/* Draw mouse cursor at current position */
static void draw_cursor(void) {
    int16_t mx, my;
    mouse_get_position(&mx, &my);

    for (int cy = 0; cy < CURSOR_H; cy++) {
        for (int cx = 0; cx < CURSOR_W; cx++) {
            char ch = cursor_data[cy][cx];
            if (ch == 'X') {
                gfx_set_pixel(mx + cx, my + cy, 0xFF000000); /* Black outline */
            } else if (ch == '#') {
                gfx_set_pixel(mx + cx, my + cy, 0xFFFFFFFF); /* White fill */
            }
            /* '.' = transparent, skip */
        }
    }
}

void comp_render(void) {
    /* 1. Desktop background */
    draw_desktop();

    /* 2. Windows (back to front) */
    uint32_t count;
    window_t **wlist = wm_get_window_list(&count);
    for (uint32_t i = 0; i < count; i++) {
        /* Let each window paint its canvas first */
        if (wlist[i]->on_paint) {
            wlist[i]->on_paint(wlist[i]);
        }
        draw_window(wlist[i]);
    }

    /* 3. Mouse cursor (always on top) */
    draw_cursor();

    /* 4. Flip to screen */
    gfx_flip();
}
