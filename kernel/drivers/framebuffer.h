#ifndef VAULTOS_FRAMEBUFFER_H
#define VAULTOS_FRAMEBUFFER_H

#include "../lib/types.h"
#include "../../include/vaultos/boot_info.h"

/* Default colors (ARGB) */
#define COLOR_BLACK   0xFF000000
#define COLOR_WHITE   0xFFFFFFFF
#define COLOR_GREEN   0xFF00FF00
#define COLOR_CYAN    0xFF00FFFF
#define COLOR_RED     0xFFFF0000
#define COLOR_YELLOW  0xFFFFFF00
#define COLOR_BLUE    0xFF0000FF
#define COLOR_GRAY    0xFF808080
#define COLOR_DKGRAY  0xFF404040

/* VaultOS brand colors */
#define COLOR_VOS_BG  0xFF0A0A1A   /* Dark blue-black */
#define COLOR_VOS_FG  0xFF00DDAA   /* Teal/cyan */
#define COLOR_VOS_HL  0xFFFFCC00   /* Gold highlight */

extern bool fb_ready;

void fb_init(BootInfo *boot_info);
void fb_putchar(char c);
void fb_write(const char *str);
void fb_clear(void);
void fb_scroll(void);
void fb_set_color(uint32_t fg, uint32_t bg);
void fb_set_cursor(uint32_t col, uint32_t row);
void fb_get_cursor(uint32_t *col, uint32_t *row);
uint32_t fb_get_cols(void);
uint32_t fb_get_rows(void);

/* TUI support: direct cell rendering without moving cursor or changing global colors */
void fb_draw_cell(uint32_t col, uint32_t row, char ch, uint32_t fg, uint32_t bg);

/* Clear a specific range of rows [start_row, end_row) */
void fb_clear_rows(uint32_t start_row, uint32_t end_row);

/* Set the scrollable content region. Scroll and wrap only affect rows [top, bottom).
 * Set both to 0 to use full screen (default behavior). */
void fb_set_content_region(uint32_t top_row, uint32_t bottom_row);

/* Get raw framebuffer parameters for GUI back buffer */
void fb_get_raw(uint32_t **base, uint32_t *pitch_bytes,
                uint32_t *width, uint32_t *height);

#endif /* VAULTOS_FRAMEBUFFER_H */
