#ifndef VAULTOS_COMPOSITOR_H
#define VAULTOS_COMPOSITOR_H

#include "../kernel/lib/types.h"

/* Initialize compositor */
void comp_init(void);

/* Mark a screen region as dirty (needs redraw) */
void comp_mark_dirty(int16_t x, int16_t y, uint16_t w, uint16_t h);

/* Mark the entire screen as dirty */
void comp_mark_all_dirty(void);

/* Render all dirty regions and flip to framebuffer */
void comp_render(void);

/* Set desktop background color */
void comp_set_bg_color(uint32_t top_color, uint32_t bottom_color);

#endif /* VAULTOS_COMPOSITOR_H */
