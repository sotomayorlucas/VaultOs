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
    uint32_t  content_top;    /* First row of scrollable content (0 = full screen) */
    uint32_t  content_bottom; /* Last row of scrollable content, exclusive (0 = full screen) */
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

static void fb_draw_char_internal(uint32_t col, uint32_t row, char c,
                                   uint32_t fg_pixel, uint32_t bg_pixel) {
    const uint8_t *glyph = font_8x16[(uint8_t)c];
    uint32_t x_base = col * FONT_WIDTH;
    uint32_t y_base = row * FONT_HEIGHT;

    for (int y = 0; y < FONT_HEIGHT; y++) {
        uint32_t *pixel_row = (uint32_t *)((uint8_t *)fb.base +
                               (y_base + y) * fb.pitch);
        uint8_t bits = glyph[y];
        for (int x = 0; x < FONT_WIDTH; x++) {
            pixel_row[x_base + x] = (bits & (0x80 >> x)) ? fg_pixel : bg_pixel;
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
    fb.content_top = 0;
    fb.content_bottom = 0;

    fb_clear();
    fb_ready = true;
}

void fb_clear(void) {
    uint32_t bg = make_pixel(fb.bg_color);
    for (uint32_t y = 0; y < fb.height; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb.base + y * fb.pitch);
        for (uint32_t x = 0; x < fb.width; x++) {
            row[x] = bg;
        }
    }
    fb.cursor_x = 0;
    fb.cursor_y = fb.content_top;
}

void fb_clear_rows(uint32_t start_row, uint32_t end_row) {
    uint32_t bg = make_pixel(fb.bg_color);
    for (uint32_t r = start_row; r < end_row && r < fb.rows; r++) {
        uint32_t y_base = r * FONT_HEIGHT;
        for (uint32_t y = 0; y < FONT_HEIGHT; y++) {
            uint32_t *pixel_row = (uint32_t *)((uint8_t *)fb.base +
                                   (y_base + y) * fb.pitch);
            for (uint32_t x = 0; x < fb.width; x++) {
                pixel_row[x] = bg;
            }
        }
    }
}

void fb_scroll(void) {
    uint32_t top = fb.content_top;
    uint32_t bot = fb.content_bottom ? fb.content_bottom : fb.rows;
    uint32_t row_bytes = fb.pitch * FONT_HEIGHT;

    /* Move rows [top+1 .. bot-1] up by one */
    uint8_t *dst = (uint8_t *)fb.base + top * row_bytes;
    uint8_t *src = dst + row_bytes;
    uint32_t move_bytes = (bot - top - 1) * row_bytes;
    memmove(dst, src, move_bytes);

    /* Clear last row in region */
    uint32_t bg = make_pixel(fb.bg_color);
    uint8_t *last = (uint8_t *)fb.base + (bot - 1) * row_bytes;
    for (uint32_t y = 0; y < FONT_HEIGHT; y++) {
        uint32_t *pixel_row = (uint32_t *)(last + y * fb.pitch);
        for (uint32_t x = 0; x < fb.width; x++) {
            pixel_row[x] = bg;
        }
    }
}

void fb_putchar(char c) {
    if (!fb_ready) return;

    uint32_t fg = make_pixel(fb.fg_color);
    uint32_t bg = make_pixel(fb.bg_color);
    uint32_t bot = fb.content_bottom ? fb.content_bottom : fb.rows;

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
            fb_draw_char_internal(fb.cursor_x, fb.cursor_y, ' ', fg, bg);
        }
        break;
    default:
        if ((uint8_t)c >= 0x20) {
            fb_draw_char_internal(fb.cursor_x, fb.cursor_y, c, fg, bg);
            fb.cursor_x++;
        }
        break;
    }

    /* Handle line wrap */
    if (fb.cursor_x >= fb.cols) {
        fb.cursor_x = 0;
        fb.cursor_y++;
    }

    /* Handle scroll within content region */
    if (fb.cursor_y >= bot) {
        fb_scroll();
        fb.cursor_y = bot - 1;
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

void fb_draw_cell(uint32_t col, uint32_t row, char ch, uint32_t fg, uint32_t bg) {
    if (!fb_ready || col >= fb.cols || row >= fb.rows) return;
    fb_draw_char_internal(col, row, ch, make_pixel(fg), make_pixel(bg));
}

void fb_set_content_region(uint32_t top_row, uint32_t bottom_row) {
    fb.content_top = top_row;
    fb.content_bottom = bottom_row;
}

void fb_get_raw(uint32_t **base, uint32_t *pitch_bytes,
                uint32_t *width, uint32_t *height) {
    if (base) *base = fb.base;
    if (pitch_bytes) *pitch_bytes = fb.pitch;
    if (width) *width = fb.width;
    if (height) *height = fb.height;
}
