#ifndef VAULTOS_HISTORY_H
#define VAULTOS_HISTORY_H

#include "../kernel/lib/types.h"

#define HISTORY_SIZE 32
#define HISTORY_LINE_MAX 512

void        history_init(void);
void        history_add(const char *line);
const char *history_prev(void);       /* Navigate backward (Up arrow) */
const char *history_next(void);       /* Navigate forward (Down arrow) */
void        history_reset_nav(void);  /* Reset navigation to end */

#endif /* VAULTOS_HISTORY_H */
