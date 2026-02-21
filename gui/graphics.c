#include "graphics.h"
#include "../kernel/drivers/framebuffer.h"
#include "../kernel/drivers/font.h"
#include "../kernel/mm/heap.h"
#include "../kernel/lib/string.h"
#include "../kernel/lib/printf.h"

/* Framebuffer info - cached from framebuffer driver */
static uint32_t *fb_base;
static uint32_t  fb_width;
static uint32_t  fb_height;
static uint32_t  fb_pitch_bytes;  /* Bytes per scanline */

/* Back buffer */
static uint32_t *backbuf;
static uint32_t  bb_pitch;  /* Pixels per scanline (== fb_width for now) */

/* Access framebuffer internals */
extern bool fb_ready;

void gfx_init(void) {
    /* Get raw framebuffer parameters */
    fb_get_raw(&fb_base, &fb_pitch_bytes, &fb_width, &fb_height);

    bb_pitch = fb_width;

    /* Allocate back buffer */
    size_t bb_size = (size_t)fb_width * fb_height * sizeof(uint32_t);
    backbuf = (uint32_t *)kmalloc(bb_size);
    if (!backbuf) {
        kprintf("[GFX] Failed to allocate back buffer (%llu bytes)\n", (uint64_t)bb_size);
        return;
    }

    /* Clear to black */
    memset(backbuf, 0, bb_size);

    kprintf("[GFX] Initialized: %ux%u, backbuffer at 0x%llx\n",
            fb_width, fb_height, (uint64_t)backbuf);
}

void gfx_flip(void) {
    if (!backbuf || !fb_base) return;

    /* Copy back buffer to framebuffer, row by row (pitch may differ) */
    for (uint32_t y = 0; y < fb_height; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)fb_base + y * fb_pitch_bytes);
        uint32_t *src = backbuf + y * bb_pitch;
        memcpy(dst, src, fb_width * sizeof(uint32_t));
    }
}

void gfx_flip_rect(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    if (!backbuf || !fb_base) return;

    /* Clamp to screen */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int16_t)fb_width)  w = fb_width - x;
    if (y + h > (int16_t)fb_height) h = fb_height - y;
    if (w == 0 || h == 0) return;

    for (uint16_t row = 0; row < h; row++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)fb_base + (y + row) * fb_pitch_bytes) + x;
        uint32_t *src = backbuf + (y + row) * bb_pitch + x;
        memcpy(dst, src, w * sizeof(uint32_t));
    }
}

void gfx_set_pixel(int16_t x, int16_t y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb_width || (uint32_t)y >= fb_height) return;
    backbuf[y * bb_pitch + x] = color;
}

void gfx_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint32_t color) {
    /* Clamp */
    int16_t x0 = x < 0 ? 0 : x;
    int16_t y0 = y < 0 ? 0 : y;
    int16_t x1 = x + w;
    int16_t y1 = y + h;
    if (x1 > (int16_t)fb_width)  x1 = fb_width;
    if (y1 > (int16_t)fb_height) y1 = fb_height;
    if (x0 >= x1 || y0 >= y1) return;

    for (int16_t row = y0; row < y1; row++) {
        uint32_t *p = backbuf + row * bb_pitch + x0;
        for (int16_t col = x0; col < x1; col++) {
            *p++ = color;
        }
    }
}

void gfx_draw_hline(int16_t x, int16_t y, uint16_t w, uint32_t color) {
    if (y < 0 || (uint32_t)y >= fb_height) return;
    int16_t x0 = x < 0 ? 0 : x;
    int16_t x1 = x + w;
    if (x1 > (int16_t)fb_width) x1 = fb_width;
    if (x0 >= x1) return;

    uint32_t *p = backbuf + y * bb_pitch + x0;
    for (int16_t i = x0; i < x1; i++) *p++ = color;
}

void gfx_draw_vline(int16_t x, int16_t y, uint16_t h, uint32_t color) {
    if (x < 0 || (uint32_t)x >= fb_width) return;
    int16_t y0 = y < 0 ? 0 : y;
    int16_t y1 = y + h;
    if (y1 > (int16_t)fb_height) y1 = fb_height;
    if (y0 >= y1) return;

    for (int16_t row = y0; row < y1; row++) {
        backbuf[row * bb_pitch + x] = color;
    }
}

void gfx_draw_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint32_t color) {
    gfx_draw_hline(x, y, w, color);
    gfx_draw_hline(x, y + h - 1, w, color);
    gfx_draw_vline(x, y, h, color);
    gfx_draw_vline(x + w - 1, y, h, color);
}

void gfx_draw_char(int16_t x, int16_t y, char ch, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = font_8x16[(uint8_t)ch];

    for (int gy = 0; gy < FONT_HEIGHT; gy++) {
        int16_t py = y + gy;
        if (py < 0 || (uint32_t)py >= fb_height) continue;
        uint8_t bits = glyph[gy];
        for (int gx = 0; gx < FONT_WIDTH; gx++) {
            int16_t px = x + gx;
            if (px < 0 || (uint32_t)px >= fb_width) continue;
            backbuf[py * bb_pitch + px] = (bits & (0x80 >> gx)) ? fg : bg;
        }
    }
}

void gfx_draw_text(int16_t x, int16_t y, const char *str, uint32_t fg, uint32_t bg) {
    while (*str) {
        gfx_draw_char(x, y, *str, fg, bg);
        x += FONT_WIDTH;
        str++;
    }
}

int gfx_text_width(const char *str) {
    return (int)strlen(str) * FONT_WIDTH;
}

void gfx_blit(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint32_t *pixels) {
    for (uint16_t row = 0; row < h; row++) {
        int16_t py = y + row;
        if (py < 0 || (uint32_t)py >= fb_height) { pixels += w; continue; }
        for (uint16_t col = 0; col < w; col++) {
            int16_t px = x + col;
            if (px >= 0 && (uint32_t)px < fb_width) {
                backbuf[py * bb_pitch + px] = *pixels;
            }
            pixels++;
        }
    }
}

void gfx_blit_masked(int16_t x, int16_t y, uint16_t w, uint16_t h,
                      const uint32_t *pixels, uint32_t mask_color) {
    for (uint16_t row = 0; row < h; row++) {
        int16_t py = y + row;
        if (py < 0 || (uint32_t)py >= fb_height) { pixels += w; continue; }
        for (uint16_t col = 0; col < w; col++) {
            int16_t px = x + col;
            if (px >= 0 && (uint32_t)px < fb_width && *pixels != mask_color) {
                backbuf[py * bb_pitch + px] = *pixels;
            }
            pixels++;
        }
    }
}

uint16_t gfx_width(void) { return (uint16_t)fb_width; }
uint16_t gfx_height(void) { return (uint16_t)fb_height; }
uint32_t *gfx_backbuffer(void) { return backbuf; }
uint32_t gfx_pitch(void) { return bb_pitch; }
