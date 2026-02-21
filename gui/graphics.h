#ifndef VAULTOS_GRAPHICS_H
#define VAULTOS_GRAPHICS_H

#include "../kernel/lib/types.h"

/* Initialize graphics subsystem (allocates back buffer) */
void gfx_init(void);

/* Flip back buffer to framebuffer (full screen) */
void gfx_flip(void);

/* Flip a rectangular region from back buffer to framebuffer */
void gfx_flip_rect(int16_t x, int16_t y, uint16_t w, uint16_t h);

/* Drawing primitives - all draw to back buffer */
void gfx_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint32_t color);
void gfx_draw_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint32_t color);
void gfx_draw_hline(int16_t x, int16_t y, uint16_t w, uint32_t color);
void gfx_draw_vline(int16_t x, int16_t y, uint16_t h, uint32_t color);
void gfx_set_pixel(int16_t x, int16_t y, uint32_t color);

/* Text rendering */
void gfx_draw_char(int16_t x, int16_t y, char ch, uint32_t fg, uint32_t bg);
void gfx_draw_text(int16_t x, int16_t y, const char *str, uint32_t fg, uint32_t bg);
int  gfx_text_width(const char *str);

/* Bitmap blitting */
void gfx_blit(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint32_t *pixels);
void gfx_blit_masked(int16_t x, int16_t y, uint16_t w, uint16_t h,
                      const uint32_t *pixels, uint32_t mask_color);

/* Screen dimensions */
uint16_t gfx_width(void);
uint16_t gfx_height(void);

/* Get raw back buffer pointer (for direct compositor access) */
uint32_t *gfx_backbuffer(void);
uint32_t  gfx_pitch(void);

#endif /* VAULTOS_GRAPHICS_H */
