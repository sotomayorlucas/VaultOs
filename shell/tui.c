#include "tui.h"
#include "../kernel/drivers/framebuffer.h"
#include "../kernel/lib/printf.h"
#include "../kernel/lib/string.h"
#include "../kernel/mm/heap.h"
#include "../kernel/mm/pmm.h"
#include "../kernel/arch/x86_64/pit.h"
#include "../kernel/db/database.h"
#include "../kernel/crypto/random.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/proc/process.h"

static tui_layout_t layout;

/* Draw a string at a specific row/col position using fb_draw_cell */
static void tui_draw_string(uint32_t col, uint32_t row,
                             const char *str, uint32_t fg, uint32_t bg) {
    while (*str && col < layout.total_cols) {
        fb_draw_cell(col, row, *str, fg, bg);
        col++;
        str++;
    }
}

/* Fill remaining columns in a row with spaces */
static void tui_fill_row(uint32_t col, uint32_t row, uint32_t fg, uint32_t bg) {
    while (col < layout.total_cols) {
        fb_draw_cell(col, row, ' ', fg, bg);
        col++;
    }
}

void tui_draw_status_bar(void) {
    uint32_t fg_label = COLOR_GRAY;
    uint32_t fg_value = COLOR_VOS_HL;
    uint32_t bg = COLOR_DKGRAY;
    char buf[64];

    /* ── Row 0: Title │ Uptime │ PID:N name │ Procs: N ── */
    uint32_t col = 0;
    tui_draw_string(col, 0, " VaultOS v0.1.0", COLOR_WHITE, bg);
    col = 16;

    fb_draw_cell(col, 0, '\xB3', fg_label, bg);
    col++;

    uint64_t ms = pit_get_uptime_ms();
    uint64_t secs = ms / 1000;
    uint64_t mins = secs / 60;
    secs %= 60;
    snprintf(buf, sizeof(buf), " Up: %llu:%02llu ", mins, secs);
    tui_draw_string(col, 0, buf, fg_value, bg);
    col += strlen(buf);

    fb_draw_cell(col, 0, '\xB3', fg_label, bg);
    col++;

    process_t *cur = process_get_current();
    if (cur) {
        snprintf(buf, sizeof(buf), " PID:%llu %s ",
                 (unsigned long long)cur->pid, cur->name);
        /* Truncate if too long */
        if (strlen(buf) > 20) { buf[19] = ' '; buf[20] = '\0'; }
        tui_draw_string(col, 0, buf, COLOR_CYAN, bg);
        col += strlen(buf);
    }

    fb_draw_cell(col, 0, '\xB3', fg_label, bg);
    col++;

    snprintf(buf, sizeof(buf), " Caps: %llu ",
             (unsigned long long)cap_table_count());
    tui_draw_string(col, 0, buf, fg_value, bg);
    col += strlen(buf);

    tui_fill_row(col, 0, fg_label, bg);

    /* ── Row 1: Heap │ PMM │ Tables │ [RNG:xx] AES-128 ── */
    col = 0;
    tui_draw_string(col, 1, " Heap:", fg_label, bg);
    col = 6;

    size_t used = heap_used();
    size_t free_mem = heap_free();
    if (used >= 1024 * 1024)
        snprintf(buf, sizeof(buf), "%lluMB", (uint64_t)used / (1024 * 1024));
    else if (used >= 1024)
        snprintf(buf, sizeof(buf), "%lluKB", (uint64_t)used / 1024);
    else
        snprintf(buf, sizeof(buf), "%lluB", (uint64_t)used);
    tui_draw_string(col, 1, buf, fg_value, bg);
    col += strlen(buf);

    tui_draw_string(col, 1, "/", fg_label, bg);
    col++;

    size_t total = used + free_mem;
    if (total >= 1024 * 1024)
        snprintf(buf, sizeof(buf), "%lluMB ", (uint64_t)total / (1024 * 1024));
    else
        snprintf(buf, sizeof(buf), "%lluKB ", (uint64_t)total / 1024);
    tui_draw_string(col, 1, buf, fg_value, bg);
    col += strlen(buf);

    fb_draw_cell(col, 1, '\xB3', fg_label, bg);
    col++;

    uint64_t pmm_free = pmm_get_free_pages();
    uint64_t pmm_total = pmm_get_total_pages();
    snprintf(buf, sizeof(buf), " PMM:%llu/%llu ",
             (unsigned long long)pmm_free, (unsigned long long)pmm_total);
    tui_draw_string(col, 1, buf, fg_value, bg);
    col += strlen(buf);

    fb_draw_cell(col, 1, '\xB3', fg_label, bg);
    col++;

    snprintf(buf, sizeof(buf), " Tabs:%u ", db_get_table_count());
    tui_draw_string(col, 1, buf, fg_value, bg);
    col += strlen(buf);

    fb_draw_cell(col, 1, '\xB3', fg_label, bg);
    col++;

    if (random_hw_available()) {
        tui_draw_string(col, 1, " RNG:HW", COLOR_GREEN, bg);
        col += 7;
    } else {
        tui_draw_string(col, 1, " RNG:SW", COLOR_YELLOW, bg);
        col += 7;
    }
    tui_draw_string(col, 1, " AES-128 ", COLOR_GREEN, bg);
    col += 9;

    tui_fill_row(col, 1, fg_label, bg);
}

void tui_draw_fkey_bar(void) {
    uint32_t bg = COLOR_DKGRAY;
    uint32_t row = layout.total_rows - 1;

    /* F-key labels */
    struct { const char *key; const char *label; } fkeys[] = {
        {"F1", "Help"},
        {"F2", "Tables"},
        {"F3", "Status"},
        {"F4", "Security"},
        {"F5", "Clear"},
        {"F6", "Audit"},
    };

    uint32_t col = 0;
    fb_draw_cell(col, row, ' ', COLOR_WHITE, bg);
    col++;

    for (int i = 0; i < 6; i++) {
        /* F-key name in highlight */
        tui_draw_string(col, row, fkeys[i].key, COLOR_VOS_HL, bg);
        col += strlen(fkeys[i].key);

        /* Colon + label in white */
        tui_draw_string(col, row, ":", COLOR_GRAY, bg);
        col++;
        tui_draw_string(col, row, fkeys[i].label, COLOR_WHITE, bg);
        col += strlen(fkeys[i].label);

        /* Space separator */
        tui_draw_string(col, row, "  ", COLOR_WHITE, bg);
        col += 2;
    }

    tui_fill_row(col, row, COLOR_WHITE, bg);
}

void tui_init(void) {
    layout.total_cols = fb_get_cols();
    layout.total_rows = fb_get_rows();

    /* Degrade gracefully for very small screens */
    if (layout.total_rows < 10) {
        /* No TUI chrome, use full screen */
        layout.content_top = 0;
        layout.content_bottom = layout.total_rows;
    } else {
        layout.content_top = TUI_STATUS_ROWS;
        layout.content_bottom = layout.total_rows - TUI_FKEY_ROWS;
    }
    layout.content_rows = layout.content_bottom - layout.content_top;

    /* Configure framebuffer content region */
    fb_set_content_region(layout.content_top, layout.content_bottom);

    /* Clear and position cursor */
    fb_clear_rows(layout.content_top, layout.content_bottom);
    fb_set_cursor(0, layout.content_top);

    /* Draw TUI chrome */
    tui_draw_status_bar();
    tui_draw_fkey_bar();
}

void tui_refresh_status(void) {
    tui_draw_status_bar();
}

const tui_layout_t *tui_get_layout(void) {
    return &layout;
}
