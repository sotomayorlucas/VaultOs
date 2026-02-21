#ifndef VAULTOS_TUI_H
#define VAULTOS_TUI_H

#include "../kernel/lib/types.h"

#define TUI_STATUS_ROWS   2   /* Top 2 rows for status bar */
#define TUI_FKEY_ROWS     1   /* Bottom 1 row for F-key bar */

typedef struct {
    uint32_t total_cols;
    uint32_t total_rows;
    uint32_t content_top;     /* First row for content (= TUI_STATUS_ROWS) */
    uint32_t content_bottom;  /* Last row for content (exclusive) */
    uint32_t content_rows;    /* content_bottom - content_top */
} tui_layout_t;

void tui_init(void);
void tui_draw_status_bar(void);
void tui_draw_fkey_bar(void);
void tui_refresh_status(void);

const tui_layout_t *tui_get_layout(void);

#endif /* VAULTOS_TUI_H */
