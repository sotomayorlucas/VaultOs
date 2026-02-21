#include "desktop.h"
#include "graphics.h"
#include "event.h"
#include "window.h"
#include "compositor.h"
#include "widgets.h"
#include "../kernel/drivers/mouse.h"
#include "../kernel/drivers/keyboard.h"
#include "../kernel/drivers/framebuffer.h"
#include "../kernel/drivers/font.h"
#include "../kernel/mm/heap.h"
#include "../kernel/db/database.h"
#include "../kernel/arch/x86_64/pit.h"
#include "../kernel/arch/x86_64/cpu.h"
#include "../kernel/lib/printf.h"
#include "../kernel/lib/string.h"
#include "../kernel/proc/process.h"

/* ---- Taskbar ---- */
#define TASKBAR_HEIGHT 28
#define TASKBAR_BG     0xFF16162E
#define TASKBAR_BORDER 0xFF333355

/* Menu state */
static bool menu_open = false;

/* ---- Taskbar drawing ---- */
static void draw_taskbar(void) {
    uint16_t sw = gfx_width();
    uint16_t sh = gfx_height();
    int16_t ty = sh - TASKBAR_HEIGHT;

    /* Background */
    gfx_fill_rect(0, ty, sw, TASKBAR_HEIGHT, TASKBAR_BG);
    gfx_draw_hline(0, ty, sw, TASKBAR_BORDER);

    /* VaultOS button */
    gfx_fill_rect(2, ty + 2, 80, TASKBAR_HEIGHT - 4, 0xFF1A3366);
    gfx_draw_rect(2, ty + 2, 80, TASKBAR_HEIGHT - 4, 0xFF4466AA);
    gfx_draw_text(10, ty + 6, "VaultOS", 0xFFFFCC00, 0xFF1A3366);

    /* Window buttons */
    uint32_t count;
    window_t **wlist = wm_get_window_list(&count);
    int16_t bx = 90;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t bg = wlist[i]->focused ? 0xFF2A4466 : 0xFF1A1A3A;
        uint32_t border = wlist[i]->focused ? 0xFF5588CC : 0xFF444466;
        gfx_fill_rect(bx, ty + 2, 120, TASKBAR_HEIGHT - 4, bg);
        gfx_draw_rect(bx, ty + 2, 120, TASKBAR_HEIGHT - 4, border);

        /* Truncate title to fit */
        char title[16];
        strncpy(title, wlist[i]->title, 14);
        title[14] = '\0';
        gfx_draw_text(bx + 4, ty + 6, title, 0xFFCCCCCC, bg);
        bx += 124;
    }

    /* Right side: uptime + heap */
    uint64_t ms = pit_get_uptime_ms();
    char status[48];
    snprintf(status, sizeof(status), "%llu.%llus  %lluKB",
             ms / 1000, (ms % 1000) / 100, (uint64_t)heap_used() / 1024);
    int16_t sx = sw - (int16_t)(strlen(status) * FONT_WIDTH) - 8;
    gfx_draw_text(sx, ty + 6, status, 0xFF808080, TASKBAR_BG);
}

/* ---- Menu ---- */
#define MENU_ITEMS 4
static const char *menu_labels[MENU_ITEMS] = {
    "Query Console",
    "Table Browser",
    "System Status",
    "Exit to TUI"
};

static void draw_menu(void) {
    if (!menu_open) return;

    uint16_t sh = gfx_height();
    int16_t mx = 2;
    int16_t my = sh - TASKBAR_HEIGHT - MENU_ITEMS * 24 - 4;
    uint16_t mw = 160;

    gfx_fill_rect(mx, my, mw, MENU_ITEMS * 24 + 4, 0xFF1A1A2E);
    gfx_draw_rect(mx, my, mw, MENU_ITEMS * 24 + 4, 0xFF555577);

    for (int i = 0; i < MENU_ITEMS; i++) {
        int16_t iy = my + 2 + i * 24;
        gfx_draw_text(mx + 8, iy + 4, menu_labels[i], 0xFF00DDAA, 0xFF1A1A2E);
    }
}

/* App forward declarations */
static void open_query_console(void);
static void open_table_browser(void);
static void open_system_status(void);

static int menu_hit_test(int16_t mx, int16_t my) {
    uint16_t sh = gfx_height();
    int16_t menu_x = 2;
    int16_t menu_y = sh - TASKBAR_HEIGHT - MENU_ITEMS * 24 - 4;

    if (mx < menu_x || mx >= menu_x + 160) return -1;
    if (my < menu_y || my >= menu_y + MENU_ITEMS * 24 + 4) return -1;

    int idx = (my - menu_y - 2) / 24;
    if (idx >= 0 && idx < MENU_ITEMS) return idx;
    return -1;
}

/* ---- Query Console Application ---- */
static widget_t *qc_textbox;
static widget_t *qc_listview;
static char qc_lv_data[LISTVIEW_MAX_ITEMS][LISTVIEW_ITEM_MAX];

static void qc_execute_btn(widget_t *w, window_t *win) {
    (void)w;
    const char *sql = textbox_get_text(qc_textbox);
    if (strlen(sql) == 0) return;

    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    query_result_t *result = db_execute(sql, pid);

    listview_clear(qc_listview);

    if (result->error_code) {
        listview_add_item(qc_listview, result->error_msg);
    } else if (result->schema && result->row_count == 0 && result->schema->column_count > 0) {
        /* DESCRIBE result - show columns */
        char line[LISTVIEW_ITEM_MAX];
        for (uint32_t c = 0; c < result->schema->column_count; c++) {
            const char *tname;
            switch (result->schema->columns[c].type) {
            case COL_U64: tname = "U64"; break;
            case COL_I64: tname = "I64"; break;
            case COL_STR: tname = "STR"; break;
            case COL_BLOB: tname = "BLOB"; break;
            case COL_BOOL: tname = "BOOL"; break;
            case COL_U32: tname = "U32"; break;
            case COL_U8:  tname = "U8"; break;
            default: tname = "?"; break;
            }
            snprintf(line, sizeof(line), "%-20s %s%s",
                     result->schema->columns[c].name, tname,
                     result->schema->columns[c].primary_key ? " PK" : "");
            listview_add_item(qc_listview, line);
        }
    } else {
        /* Show result rows */
        for (uint32_t r = 0; r < result->row_count; r++) {
            char line[LISTVIEW_ITEM_MAX] = "";
            record_t *row = &result->rows[r];
            for (uint32_t f = 0; f < row->field_count && f < 6; f++) {
                char val[48];
                switch (row->fields[f].type) {
                case COL_U64:
                    snprintf(val, sizeof(val), "%llu", (unsigned long long)row->fields[f].u64_val);
                    break;
                case COL_I64:
                    snprintf(val, sizeof(val), "%lld", (long long)row->fields[f].i64_val);
                    break;
                case COL_U32:
                    snprintf(val, sizeof(val), "%u", row->fields[f].u32_val);
                    break;
                case COL_BOOL:
                    snprintf(val, sizeof(val), "%s", row->fields[f].bool_val ? "true" : "false");
                    break;
                case COL_STR:
                    snprintf(val, sizeof(val), "%.40s", row->fields[f].str_val.data);
                    break;
                default:
                    snprintf(val, sizeof(val), "...");
                    break;
                }
                if (f > 0) strncat(line, " | ", sizeof(line) - strlen(line) - 1);
                strncat(line, val, sizeof(line) - strlen(line) - 1);
            }
            listview_add_item(qc_listview, line);
        }
        char summary[64];
        snprintf(summary, sizeof(summary), "-- %u row(s) --", result->row_count);
        listview_add_item(qc_listview, summary);
    }

    db_result_free(result);
}

static void qc_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);
    widget_draw_all(win);
}

static void qc_event(window_t *win, gui_event_t *ev) {
    if (ev->type == EVT_CLOSE) {
        wm_destroy_window(win->id);
        return;
    }
    widget_dispatch(win, ev);
}

static void open_query_console(void) {
    window_t *win = wm_create_window("Query Console", 100, 60, 500, 400,
                                      qc_event, qc_paint);
    if (!win) return;

    qc_textbox = widget_create_textbox(4, 4, win->client_w - 80, 24);
    qc_textbox->focused = true;
    widget_add(win, qc_textbox);

    widget_t *btn = widget_create_button(win->client_w - 72, 4, 68, 24,
                                          "Execute", qc_execute_btn);
    widget_add(win, btn);

    qc_listview = widget_create_listview(4, 34, win->client_w - 8, win->client_h - 40);
    qc_listview->lv_items = qc_lv_data;
    widget_add(win, qc_listview);
}

/* ---- Table Browser Application ---- */
static widget_t *tb_table_list;
static widget_t *tb_detail_list;
static char tb_tl_data[LISTVIEW_MAX_ITEMS][LISTVIEW_ITEM_MAX];
static char tb_dl_data[LISTVIEW_MAX_ITEMS][LISTVIEW_ITEM_MAX];

static void tb_on_table_select(widget_t *w, window_t *win, int16_t index) {
    (void)w; (void)win;
    if (index < 0) return;

    table_schema_t *schema = db_get_schema_by_id((uint32_t)index);
    if (!schema) return;

    listview_clear(tb_detail_list);

    /* Header */
    char line[LISTVIEW_ITEM_MAX];
    snprintf(line, sizeof(line), "Table: %s (%u columns)%s",
             schema->name, schema->column_count,
             schema->encrypted ? " [ENCRYPTED]" : "");
    listview_add_item(tb_detail_list, line);
    listview_add_item(tb_detail_list, "---");

    /* Columns */
    for (uint32_t c = 0; c < schema->column_count; c++) {
        const char *tname;
        switch (schema->columns[c].type) {
        case COL_U64: tname = "U64"; break;
        case COL_I64: tname = "I64"; break;
        case COL_STR: tname = "STR"; break;
        case COL_BLOB: tname = "BLOB"; break;
        case COL_BOOL: tname = "BOOL"; break;
        case COL_U32: tname = "U32"; break;
        case COL_U8:  tname = "U8"; break;
        default: tname = "?"; break;
        }
        snprintf(line, sizeof(line), "  %-20s %-6s%s%s",
                 schema->columns[c].name, tname,
                 schema->columns[c].primary_key ? " PK" : "",
                 schema->columns[c].not_null ? " NOT NULL" : "");
        listview_add_item(tb_detail_list, line);
    }
}

static void tb_refresh_btn(widget_t *w, window_t *win) {
    (void)w; (void)win;
    listview_clear(tb_table_list);
    uint32_t count = db_get_table_count();
    for (uint32_t i = 0; i < count; i++) {
        table_schema_t *s = db_get_schema_by_id(i);
        if (s) listview_add_item(tb_table_list, s->name);
    }
}

static void tb_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);
    widget_draw_all(win);
}

static void tb_event(window_t *win, gui_event_t *ev) {
    if (ev->type == EVT_CLOSE) {
        wm_destroy_window(win->id);
        return;
    }
    widget_dispatch(win, ev);
}

static void open_table_browser(void) {
    window_t *win = wm_create_window("Table Browser", 150, 80, 550, 380,
                                      tb_event, tb_paint);
    if (!win) return;

    /* Left: table list */
    tb_table_list = widget_create_listview(4, 28, 180, win->client_h - 34);
    tb_table_list->lv_items = tb_tl_data;
    tb_table_list->on_select = tb_on_table_select;
    widget_add(win, tb_table_list);

    /* Right: detail list */
    tb_detail_list = widget_create_listview(190, 28, win->client_w - 196, win->client_h - 34);
    tb_detail_list->lv_items = tb_dl_data;
    widget_add(win, tb_detail_list);

    /* Refresh button */
    widget_t *btn = widget_create_button(4, 2, 80, 22, "Refresh", tb_refresh_btn);
    widget_add(win, btn);

    /* Label */
    widget_t *lbl = widget_create_label(90, 5, "Tables", 0xFFFFCC00, 0xFF1A1A2E);
    widget_add(win, lbl);

    /* Populate */
    tb_refresh_btn(NULL, win);
}

/* ---- System Status Application ---- */
static widget_t *ss_labels[6];

static void ss_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);

    /* Update dynamic labels */
    uint64_t ms = pit_get_uptime_ms();
    char buf[64];

    snprintf(buf, sizeof(buf), "Uptime:     %llu.%llus", ms / 1000, (ms % 1000) / 100);
    strncpy(ss_labels[1]->text, buf, WIDGET_TEXT_MAX - 1);
    ss_labels[1]->w = (int16_t)(strlen(buf) * FONT_WIDTH);

    snprintf(buf, sizeof(buf), "Heap Used:  %llu bytes", (uint64_t)heap_used());
    strncpy(ss_labels[2]->text, buf, WIDGET_TEXT_MAX - 1);
    ss_labels[2]->w = (int16_t)(strlen(buf) * FONT_WIDTH);

    snprintf(buf, sizeof(buf), "Heap Free:  %llu bytes", (uint64_t)heap_free());
    strncpy(ss_labels[3]->text, buf, WIDGET_TEXT_MAX - 1);
    ss_labels[3]->w = (int16_t)(strlen(buf) * FONT_WIDTH);

    snprintf(buf, sizeof(buf), "Tables:     %u", db_get_table_count());
    strncpy(ss_labels[4]->text, buf, WIDGET_TEXT_MAX - 1);
    ss_labels[4]->w = (int16_t)(strlen(buf) * FONT_WIDTH);

    widget_draw_all(win);
}

static void ss_event(window_t *win, gui_event_t *ev) {
    if (ev->type == EVT_CLOSE) {
        wm_destroy_window(win->id);
        return;
    }
    widget_dispatch(win, ev);
}

static void open_system_status(void) {
    window_t *win = wm_create_window("System Status", 200, 120, 340, 220,
                                      ss_event, ss_paint);
    if (!win) return;

    uint32_t fg = 0xFF00DDAA;
    uint32_t bg = 0xFF1A1A2E;

    ss_labels[0] = widget_create_label(12, 10, "VaultOS System Status", 0xFFFFCC00, bg);
    widget_add(win, ss_labels[0]);

    ss_labels[1] = widget_create_label(12, 40, "Uptime:     ...", fg, bg);
    widget_add(win, ss_labels[1]);

    ss_labels[2] = widget_create_label(12, 60, "Heap Used:  ...", fg, bg);
    widget_add(win, ss_labels[2]);

    ss_labels[3] = widget_create_label(12, 80, "Heap Free:  ...", fg, bg);
    widget_add(win, ss_labels[3]);

    ss_labels[4] = widget_create_label(12, 100, "Tables:     ...", fg, bg);
    widget_add(win, ss_labels[4]);

    ss_labels[5] = widget_create_label(12, 130, "CPU: x86-64-v2 (Haswell+)", 0xFF808080, bg);
    widget_add(win, ss_labels[5]);
}

/* ---- Main GUI Loop ---- */
static volatile bool gui_running = true;

void gui_main(void) {
    kprintf("[GUI] Starting desktop...\n");

    /* Expand heap for back buffer + window canvases */
    if (heap_expand(15 * 1024 * 1024) < 0) {
        kprintf("[GUI] Failed to expand heap, aborting\n");
        return;
    }

    /* Initialize subsystems */
    gfx_init();
    mouse_init();
    mouse_set_bounds(gfx_width(), gfx_height());
    event_init();
    wm_init();
    comp_init();

    /* Disable TUI content region (we own the framebuffer now) */
    fb_set_content_region(0, 0);

    kprintf("[GUI] Desktop ready\n");

    gui_running = true;

    while (gui_running) {
        /* Pump events from hardware */
        event_pump();

        /* Process events */
        gui_event_t ev;
        while (event_poll(&ev)) {
            /* Check taskbar clicks */
            if (ev.type == EVT_MOUSE_DOWN) {
                uint16_t sh = gfx_height();
                int16_t ty = sh - TASKBAR_HEIGHT;

                if (ev.mouse_y >= ty) {
                    /* Click in taskbar */
                    if (ev.mouse_x >= 2 && ev.mouse_x < 82) {
                        /* VaultOS button */
                        menu_open = !menu_open;
                        continue;
                    }

                    /* Window buttons */
                    uint32_t count;
                    window_t **wlist = wm_get_window_list(&count);
                    int16_t bx = 90;
                    for (uint32_t i = 0; i < count; i++) {
                        if (ev.mouse_x >= bx && ev.mouse_x < bx + 120) {
                            wm_bring_to_front(wlist[i]->id);
                            break;
                        }
                        bx += 124;
                    }
                    continue;
                }

                /* Menu click */
                if (menu_open) {
                    int idx = menu_hit_test(ev.mouse_x, ev.mouse_y);
                    menu_open = false;
                    if (idx >= 0) {
                        switch (idx) {
                        case 0: open_query_console(); break;
                        case 1: open_table_browser(); break;
                        case 2: open_system_status(); break;
                        case 3: gui_running = false; break;
                        }
                        continue;
                    }
                }
            }

            /* Close menu on any click outside */
            if (ev.type == EVT_MOUSE_DOWN && menu_open) {
                menu_open = false;
            }

            /* Forward to window manager */
            wm_dispatch_event(&ev);
        }

        /* Render */
        comp_render();
        draw_taskbar();
        draw_menu();
        /* Flip taskbar area (compositor already flipped the rest) */
        gfx_flip_rect(0, gfx_height() - TASKBAR_HEIGHT - MENU_ITEMS * 24 - 10,
                       gfx_width(), TASKBAR_HEIGHT + MENU_ITEMS * 24 + 10);

        /* Idle */
        hlt();
    }

    /* Return to TUI: re-init framebuffer state */
    kprintf("[GUI] Returning to TUI...\n");
    fb_clear();
}
