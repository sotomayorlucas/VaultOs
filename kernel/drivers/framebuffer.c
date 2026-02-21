#include "framebuffer.h"
#include "font.h"
#include "../mm/memory_layout.h"
#include "../lib/string.h"

bool fb_ready = false;

static struct {
    uint32_t *base;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;      /* bytes per scanline */
    uint32_t  cursor_x;
    uint32_t  cursor_y;
    uint32_t  cols;
    uint32_t  rows;
    uint32_t  fg_color;
    uint32_t  bg_color;
    uint32_t  pixel_format; /* 0=BGRA, 1=RGBA */
} fb;

static uint32_t make_pixel(uint32_t argb) {
    if (fb.pixel_format == 0) {
        /* BGRA (most common for UEFI GOP) - ARGB is already compatible */
        return argb;
    }
    /* RGBA: swap R and B */
    uint8_t a = (argb >> 24) & 0xFF;
    uint8_t r = (argb >> 16) & 0xFF;
    uint8_t g = (argb >> 8) & 0xFF;
    uint8_t b = argb & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

static void fb_draw_char(uint32_t col, uint32_t row, char c) {
    const uint8_t *glyph = font_8x16[(uint8_t)c];
    uint32_t x_base = col * FONT_WIDTH;
    uint32_t y_base = row * FONT_HEIGHT;
    uint32_t fg = make_pixel(fb.fg_color);
    uint32_t bg = make_pixel(fb.bg_color);

    for (int y = 0; y < FONT_HEIGHT; y++) {
        uint32_t *pixel_row = (uint32_t *)((uint8_t *)fb.base +
                               (y_base + y) * fb.pitch);
        uint8_t bits = glyph[y];
        for (int x = 0; x < FONT_WIDTH; x++) {
            pixel_row[x_base + x] = (bits & (0x80 >> x)) ? fg : bg;
        }
    }
}

void fb_init(BootInfo *boot_info) {
    if (!boot_info->fb_base) return;

    fb.base = (uint32_t *)FRAMEBUF_VBASE;
    fb.width = boot_info->fb_width;
    fb.height = boot_info->fb_height;
    fb.pitch = boot_info->fb_pitch;
    fb.pixel_format = boot_info->fb_pixel_format;
    fb.cols = fb.width / FONT_WIDTH;
    fb.rows = fb.height / FONT_HEIGHT;
    fb.cursor_x = 0;
    fb.cursor_y = 0;
    fb.fg_color = COLOR_VOS_FG;
    fb.bg_color = COLOR_VOS_BG;

    fb_clear();
    fb_ready = true;
}

void fb_clear(void) {
    uint32_t bg = make_pixel(fb.bg_color);
    uint32_t total_pixels = fb.width * fb.height;
    /* Clear using pitch-aware loop */
    for (uint32_t y = 0; y < fb.height; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb.base + y * fb.pitch);
        for (uint32_t x = 0; x < fb.width; x++) {
            row[x] = bg;
        }
    }
    fb.cursor_x = 0;
    fb.cursor_y = 0;
    (void)total_pixels;
}

void fb_scroll(void) {
    /* Move all rows up by one */
    uint32_t row_bytes = fb.pitch * FONT_HEIGHT;
    uint32_t total_rows_bytes = (fb.rows - 1) * row_bytes;

    memmove(fb.base, (uint8_t *)fb.base + row_bytes, total_rows_bytes);

    /* Clear the last row */
    uint32_t bg = make_pixel(fb.bg_color);
    uint8_t *last_row = (uint8_t *)fb.base + (fb.rows - 1) * row_bytes;
    for (uint32_t y = 0; y < FONT_HEIGHT; y++) {
        uint32_t *pixel_row = (uint32_t *)(last_row + y * fb.pitch);
        for (uint32_t x = 0; x < fb.width; x++) {
            pixel_row[x] = bg;
        }
    }
}

void fb_putchar(char c) {
    if (!fb_ready) return;

    switch (c) {
    case '\n':
        fb.cursor_x = 0;
        fb.cursor_y++;
        break;
    case '\r':
        fb.cursor_x = 0;
        break;
    case '\t':
        fb.cursor_x = (fb.cursor_x + 4) & ~3;
        break;
    case '\b':
        if (fb.cursor_x > 0) {
            fb.cursor_x--;
            fb_draw_char(fb.cursor_x, fb.cursor_y, ' ');
        }
        break;
    default:
        if (c >= 0x20) {
            fb_draw_char(fb.cursor_x, fb.cursor_y, c);
            fb.cursor_x++;
        }
        break;
    }

    /* Handle line wrap */
    if (fb.cursor_x >= fb.cols) {
        fb.cursor_x = 0;
        fb.cursor_y++;
    }

    /* Handle scroll */
    if (fb.cursor_y >= fb.rows) {
        fb_scroll();
        fb.cursor_y = fb.rows - 1;
    }
}

void fb_write(const char *str) {
    while (*str) fb_putchar(*str++);
}

void fb_set_color(uint32_t fg, uint32_t bg) {
    fb.fg_color = fg;
    fb.bg_color = bg;
}

void fb_set_cursor(uint32_t col, uint32_t row) {
    fb.cursor_x = col;
    fb.cursor_y = row;
}

void fb_get_cursor(uint32_t *col, uint32_t *row) {
    if (col) *col = fb.cursor_x;
    if (row) *row = fb.cursor_y;
}

uint32_t fb_get_cols(void) { return fb.cols; }
uint32_t fb_get_rows(void) { return fb.rows; }
