#ifndef VAULTOS_LINE_EDITOR_H
#define VAULTOS_LINE_EDITOR_H

#include "../kernel/lib/types.h"

#define LINE_MAX 512

/* Initialize line editor (call once) */
void line_editor_init(void);

/* Read a line with full editing support (blocking).
 * out_fkey: if non-NULL, set to F-key code that terminated input (0 if Enter). */
char *line_read(char *buf, size_t buf_size, uint8_t *out_fkey);

#endif /* VAULTOS_LINE_EDITOR_H */
