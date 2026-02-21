#ifndef VAULTOS_LINE_EDITOR_H
#define VAULTOS_LINE_EDITOR_H

#include "../kernel/lib/types.h"

#define LINE_MAX 512

/* Read a line from keyboard input (blocking) */
char *line_read(char *buf, size_t buf_size);

#endif /* VAULTOS_LINE_EDITOR_H */
