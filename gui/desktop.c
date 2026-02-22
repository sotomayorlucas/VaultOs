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
#include "../kernel/proc/scheduler.h"
#include "../kernel/mm/pmm.h"
#include "../kernel/crypto/random.h"
#include "../kernel/cap/cap_table.h"

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
        uint32_t text_color = wlist[i]->minimized ? 0xFF666666 : 0xFFCCCCCC;
        gfx_fill_rect(bx, ty + 2, 120, TASKBAR_HEIGHT - 4, bg);
        gfx_draw_rect(bx, ty + 2, 120, TASKBAR_HEIGHT - 4, border);

        /* Truncate title to fit */
        char title[16];
        strncpy(title, wlist[i]->title, 14);
        title[14] = '\0';
        gfx_draw_text(bx + 4, ty + 6, title, text_color, bg);
        bx += 124;
    }

    /* Right side: security indicators + uptime + heap */
    uint64_t ms = pit_get_uptime_ms();
    char status[80];
    snprintf(status, sizeof(status), "%llu.%llus  %lluKB",
             ms / 1000, (ms % 1000) / 100, (uint64_t)heap_used() / 1024);
    int16_t sx = sw - (int16_t)(strlen(status) * FONT_WIDTH) - 8;
    gfx_draw_text(sx, ty + 6, status, 0xFF808080, TASKBAR_BG);

    /* Security badges left of status */
    const char *rng_tag = random_hw_available() ? "RNG:HW" : "RNG:SW";
    uint32_t rng_color = random_hw_available() ? 0xFF00CC66 : 0xFFCCCC00;
    int16_t badge_x = sx - (int16_t)(strlen(rng_tag) * FONT_WIDTH) - 12;
    gfx_draw_text(badge_x, ty + 6, rng_tag, rng_color, TASKBAR_BG);

    badge_x -= 4 * FONT_WIDTH + 8;
    gfx_draw_text(badge_x, ty + 6, "ENC", 0xFF00CC66, TASKBAR_BG);
}

/* ---- Menu ---- */
#define MENU_ITEMS 17
static const char *menu_labels[MENU_ITEMS] = {
    "Query Console",        /* 0  */
    "Table Browser",        /* 1  */
    "Data Grid",            /* 2  */
    "---",                  /* 3  separator */
    "VaultPad Editor",      /* 4  */
    "Calculator",           /* 5  */
    "Object Inspector",     /* 6  */
    "---",                  /* 7  separator */
    "Security Dashboard",   /* 8  */
    "Audit Log",            /* 9  */
    "Capability Manager",   /* 10 */
    "Object Manager",       /* 11 */
    "---",                  /* 12 separator */
    "Process Manager",      /* 13 */
    "System Status",        /* 14 */
    "---",                  /* 15 separator */
    "Exit to TUI"           /* 16 */
};

static bool menu_is_separator(int idx) {
    return (idx >= 0 && idx < MENU_ITEMS && menu_labels[idx][0] == '-');
}

static void draw_menu(void) {
    if (!menu_open) return;

    uint16_t sh = gfx_height();
    int16_t mx = 2;
    int16_t my = sh - TASKBAR_HEIGHT - MENU_ITEMS * 24 - 4;
    uint16_t mw = 180;

    gfx_fill_rect(mx, my, mw, MENU_ITEMS * 24 + 4, 0xFF1A1A2E);
    gfx_draw_rect(mx, my, mw, MENU_ITEMS * 24 + 4, 0xFF555577);

    for (int i = 0; i < MENU_ITEMS; i++) {
        int16_t iy = my + 2 + i * 24;
        if (menu_is_separator(i)) {
            /* Draw horizontal line separator */
            gfx_draw_hline(mx + 4, iy + 12, mw - 8, 0xFF444466);
        } else {
            gfx_draw_text(mx + 8, iy + 4, menu_labels[i], 0xFF00DDAA, 0xFF1A1A2E);
        }
    }
}

/* App forward declarations */
static void open_query_console(void);
static void open_table_browser(void);
static void open_process_manager(void);
static void open_system_status(void);
static void open_security_dashboard(void);
static void open_audit_viewer(void);
static void open_cap_manager(void);
static void open_object_manager(void);
static void open_data_grid(void);
static void open_vaultpad(void);
static void open_calculator(void);
static void open_object_inspector(void);

/* External: monitor_entry defined in shell_main.c */
extern void monitor_entry(void);

static int menu_hit_test(int16_t mx, int16_t my) {
    uint16_t sh = gfx_height();
    int16_t menu_x = 2;
    int16_t menu_y = sh - TASKBAR_HEIGHT - MENU_ITEMS * 24 - 4;

    if (mx < menu_x || mx >= menu_x + 180) return -1;
    if (my < menu_y || my >= menu_y + MENU_ITEMS * 24 + 4) return -1;

    int idx = (my - menu_y - 2) / 24;
    if (idx >= 0 && idx < MENU_ITEMS && !menu_is_separator(idx)) return idx;
    return -1;
}

/* ---- Query Console Application ---- */
static widget_t *qc_textbox;
static widget_t *qc_listview;
static char qc_lv_data[LISTVIEW_MAX_ITEMS][LISTVIEW_ITEM_MAX];

static const char *qc_templates[] = {
    "SHOW TABLES",
    "SELECT * FROM SystemTable",
    "SELECT * FROM ProcessTable",
    "SELECT * FROM CapabilityTable",
    "DESCRIBE SystemTable",
    "DESCRIBE ProcessTable",
    NULL
};
static int qc_template_idx = 0;

static void qc_template_btn(widget_t *w, window_t *win) {
    (void)w; (void)win;
    if (!qc_templates[qc_template_idx]) qc_template_idx = 0;
    textbox_set_text(qc_textbox, qc_templates[qc_template_idx]);
    qc_template_idx++;
    if (!qc_templates[qc_template_idx]) qc_template_idx = 0;
}

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
    window_t *win = wm_create_window("Query Console", 100, 60, 540, 400,
                                      qc_event, qc_paint);
    if (!win) return;

    qc_textbox = widget_create_textbox(4, 4, win->client_w - 160, 24);
    qc_textbox->focused = true;
    widget_add(win, qc_textbox);

    widget_t *btn = widget_create_button(win->client_w - 152, 4, 68, 24,
                                          "Execute", qc_execute_btn);
    widget_add(win, btn);

    widget_t *tbtn = widget_create_button(win->client_w - 80, 4, 76, 24,
                                            "Template", qc_template_btn);
    widget_add(win, tbtn);

    qc_listview = widget_create_listview(4, 34, win->client_w - 8, win->client_h - 40);
    qc_listview->lv_items = qc_lv_data;
    widget_add(win, qc_listview);
}

/* ---- Helper: populate listview from query result ---- */
static void populate_listview_from_result(widget_t *lv, query_result_t *result) {
    listview_clear(lv);

    if (result->error_code) {
        listview_add_item(lv, result->error_msg);
        return;
    }

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
        listview_add_item(lv, line);
    }

    char summary[64];
    snprintf(summary, sizeof(summary), "-- %u row(s) --", result->row_count);
    listview_add_item(lv, summary);
}

/* ---- Table Browser Application ---- */
static widget_t *tb_table_list;
static widget_t *tb_detail_list;
static widget_t *tb_search_box;
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

static void tb_view_all_btn(widget_t *w, window_t *win) {
    (void)w; (void)win;
    int16_t sel = listview_get_selected(tb_table_list);
    if (sel < 0) return;

    table_schema_t *s = db_get_schema_by_id((uint32_t)sel);
    if (!s) return;

    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT * FROM %s", s->name);

    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    query_result_t *result = db_execute(sql, pid);
    populate_listview_from_result(tb_detail_list, result);
    db_result_free(result);
}

static void tb_search_btn_cb(widget_t *w, window_t *win) {
    (void)w; (void)win;
    int16_t sel = listview_get_selected(tb_table_list);
    if (sel < 0) return;

    table_schema_t *s = db_get_schema_by_id((uint32_t)sel);
    if (!s) return;

    const char *query = textbox_get_text(tb_search_box);
    if (strlen(query) == 0) return;

    /* Parse "col=val" from search box */
    char col[64] = "", val[128] = "";
    const char *eq = strchr(query, '=');
    if (!eq) {
        listview_clear(tb_detail_list);
        listview_add_item(tb_detail_list, "Search format: column=value");
        return;
    }
    size_t clen = (size_t)(eq - query);
    if (clen >= sizeof(col)) clen = sizeof(col) - 1;
    memcpy(col, query, clen);
    col[clen] = '\0';
    strncpy(val, eq + 1, sizeof(val) - 1);
    val[sizeof(val) - 1] = '\0';

    /* Build SQL */
    char sql[512];
    /* Check if value is numeric */
    bool numeric = true;
    const char *vp = val;
    if (*vp == '-') vp++;
    if (!*vp) numeric = false;
    while (*vp) { if (!isdigit(*vp)) { numeric = false; break; } vp++; }

    if (numeric) {
        snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE %s = %s", s->name, col, val);
    } else {
        snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE %s = '%s'", s->name, col, val);
    }

    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    query_result_t *result = db_execute(sql, pid);
    populate_listview_from_result(tb_detail_list, result);
    db_result_free(result);
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
    window_t *win = wm_create_window("Table Browser", 150, 80, 600, 400,
                                      tb_event, tb_paint);
    if (!win) return;

    /* Top row: Refresh, View All, label */
    widget_t *btn = widget_create_button(4, 2, 72, 22, "Refresh", tb_refresh_btn);
    widget_add(win, btn);

    widget_t *vbtn = widget_create_button(80, 2, 72, 22, "View All", tb_view_all_btn);
    widget_add(win, vbtn);

    widget_t *lbl = widget_create_label(158, 5, "Tables", 0xFFFFCC00, 0xFF1A1A2E);
    widget_add(win, lbl);

    /* Search row (right side top) */
    tb_search_box = widget_create_textbox(190, 2, win->client_w - 270, 22);
    widget_add(win, tb_search_box);

    widget_t *sbtn = widget_create_button(win->client_w - 76, 2, 72, 22, "Search", tb_search_btn_cb);
    widget_add(win, sbtn);

    /* Left: table list */
    tb_table_list = widget_create_listview(4, 28, 180, win->client_h - 34);
    tb_table_list->lv_items = tb_tl_data;
    tb_table_list->on_select = tb_on_table_select;
    widget_add(win, tb_table_list);

    /* Right: detail list */
    tb_detail_list = widget_create_listview(190, 28, win->client_w - 196, win->client_h - 34);
    tb_detail_list->lv_items = tb_dl_data;
    widget_add(win, tb_detail_list);

    /* Populate */
    tb_refresh_btn(NULL, win);
}

/* ---- Process Manager Application ---- */
static widget_t *pm_listview;
static char pm_lv_data[LISTVIEW_MAX_ITEMS][LISTVIEW_ITEM_MAX];

static void pm_refresh(widget_t *w, window_t *win) {
    (void)w; (void)win;
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    query_result_t *result = db_execute("SELECT * FROM ProcessTable", pid);
    populate_listview_from_result(pm_listview, result);
    db_result_free(result);
}

static void pm_spawn_monitor(widget_t *w, window_t *win) {
    (void)w; (void)win;
    process_t *p = process_create("monitor", monitor_entry);
    if (p) {
        scheduler_add(p);
    }
    /* Refresh list to show the new process */
    pm_refresh(NULL, win);
}

static void pm_kill_selected(widget_t *w, window_t *win) {
    (void)w; (void)win;
    int16_t sel = listview_get_selected(pm_listview);
    if (sel < 0) return;

    /* Query ProcessTable to get the PID of the selected row */
    uint64_t my_pid = 0;
    process_t *cur = process_get_current();
    if (cur) my_pid = cur->pid;

    query_result_t *result = db_execute("SELECT * FROM ProcessTable", my_pid);
    if (!result || (uint32_t)sel >= result->row_count) {
        if (result) db_result_free(result);
        return;
    }

    uint64_t target_pid = result->rows[sel].fields[0].u64_val;
    db_result_free(result);

    /* Don't kill ourselves (the shell process) */
    if (target_pid == my_pid) return;

    process_t *victim = process_get_by_pid(target_pid);
    if (victim) {
        process_exit(victim, -1);
    }
    pm_refresh(NULL, win);
}

static void pm_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);
    widget_draw_all(win);
}

static void pm_event(window_t *win, gui_event_t *ev) {
    if (ev->type == EVT_CLOSE) {
        wm_destroy_window(win->id);
        return;
    }
    widget_dispatch(win, ev);
}

static void open_process_manager(void) {
    window_t *win = wm_create_window("Process Manager", 120, 70, 480, 340,
                                      pm_event, pm_paint);
    if (!win) return;

    widget_t *rbtn = widget_create_button(4, 2, 72, 22, "Refresh", pm_refresh);
    widget_add(win, rbtn);

    widget_t *sbtn = widget_create_button(80, 2, 110, 22, "Spawn Monitor", pm_spawn_monitor);
    widget_add(win, sbtn);

    widget_t *kbtn = widget_create_button(194, 2, 72, 22, "Kill", pm_kill_selected);
    widget_add(win, kbtn);

    pm_listview = widget_create_listview(4, 28, win->client_w - 8, win->client_h - 34);
    pm_listview->lv_items = pm_lv_data;
    widget_add(win, pm_listview);

    /* Initial populate */
    pm_refresh(NULL, win);
}

/* ---- System Status Application ---- */
#define SS_LABEL_COUNT 12
static widget_t *ss_labels[SS_LABEL_COUNT];

static void ss_update_label(int idx, const char *text) {
    strncpy(ss_labels[idx]->text, text, WIDGET_TEXT_MAX - 1);
    ss_labels[idx]->text[WIDGET_TEXT_MAX - 1] = '\0';
    ss_labels[idx]->w = (int16_t)(strlen(ss_labels[idx]->text) * FONT_WIDTH);
}

static void ss_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);

    char buf[64];

    uint64_t ms = pit_get_uptime_ms();
    snprintf(buf, sizeof(buf), "Uptime:       %llu.%llus", ms / 1000, (ms % 1000) / 100);
    ss_update_label(1, buf);

    snprintf(buf, sizeof(buf), "Heap Used:    %llu bytes", (uint64_t)heap_used());
    ss_update_label(2, buf);

    snprintf(buf, sizeof(buf), "Heap Free:    %llu bytes", (uint64_t)heap_free());
    ss_update_label(3, buf);

    snprintf(buf, sizeof(buf), "PMM Pages:    %llu / %llu",
             (unsigned long long)pmm_get_free_pages(),
             (unsigned long long)pmm_get_total_pages());
    ss_update_label(4, buf);

    snprintf(buf, sizeof(buf), "Tables:       %u (encrypted)", db_get_table_count());
    ss_update_label(5, buf);

    snprintf(buf, sizeof(buf), "Capabilities: %llu active",
             (unsigned long long)cap_table_count());
    ss_update_label(6, buf);

    snprintf(buf, sizeof(buf), "RNG Source:   %s",
             random_hw_available() ? "Hardware (RDRAND)" : "Software (xorshift128+)");
    ss_update_label(7, buf);
    ss_labels[7]->fg = random_hw_available() ? 0xFF00CC66 : 0xFFCCCC00;

    snprintf(buf, sizeof(buf), "Encryption:   AES-128-CBC + HMAC-SHA256");
    ss_update_label(8, buf);

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
    window_t *win = wm_create_window("System Status", 200, 100, 420, 340,
                                      ss_event, ss_paint);
    if (!win) return;

    uint32_t fg = 0xFF00DDAA;
    uint32_t bg = 0xFF1A1A2E;
    int16_t y = 10;
    int16_t dy = 22;

    ss_labels[0] = widget_create_label(12, y, "VaultOS System Status", 0xFFFFCC00, bg);
    widget_add(win, ss_labels[0]);
    y += dy + 6;

    ss_labels[1] = widget_create_label(12, y, "Uptime:       ...", fg, bg);
    widget_add(win, ss_labels[1]);
    y += dy;

    ss_labels[2] = widget_create_label(12, y, "Heap Used:    ...", fg, bg);
    widget_add(win, ss_labels[2]);
    y += dy;

    ss_labels[3] = widget_create_label(12, y, "Heap Free:    ...", fg, bg);
    widget_add(win, ss_labels[3]);
    y += dy;

    ss_labels[4] = widget_create_label(12, y, "PMM Pages:    ...", fg, bg);
    widget_add(win, ss_labels[4]);
    y += dy;

    ss_labels[5] = widget_create_label(12, y, "Tables:       ...", fg, bg);
    widget_add(win, ss_labels[5]);
    y += dy;

    ss_labels[6] = widget_create_label(12, y, "Capabilities: ...", fg, bg);
    widget_add(win, ss_labels[6]);
    y += dy;

    ss_labels[7] = widget_create_label(12, y, "RNG Source:   ...", fg, bg);
    widget_add(win, ss_labels[7]);
    y += dy;

    ss_labels[8] = widget_create_label(12, y, "Encryption:   ...", 0xFF00CC66, bg);
    widget_add(win, ss_labels[8]);
    y += dy + 6;

    ss_labels[9] = widget_create_label(12, y, "CPU: x86-64-v2 (Haswell+)", 0xFF808080, bg);
    widget_add(win, ss_labels[9]);
}

/* ---- Security Dashboard Application ---- */
#define SD_LABEL_COUNT 16
static widget_t *sd_labels[SD_LABEL_COUNT];

static void sd_update_label(int idx, const char *text) {
    strncpy(sd_labels[idx]->text, text, WIDGET_TEXT_MAX - 1);
    sd_labels[idx]->text[WIDGET_TEXT_MAX - 1] = '\0';
    sd_labels[idx]->w = (int16_t)(strlen(sd_labels[idx]->text) * FONT_WIDTH);
}

static void sd_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);

    /* Update dynamic labels */
    char buf[64];

    /* Update table SEALED status labels (indices 3..8 for 6 tables) */
    uint32_t tc = db_get_table_count();
    for (uint32_t i = 0; i < tc && i < 6; i++) {
        table_schema_t *s = db_get_schema_by_id(i);
        if (s) {
            snprintf(buf, sizeof(buf), "  %-18s SEALED", s->name);
            sd_update_label(3 + i, buf);
        }
    }

    /* Capabilities */
    snprintf(buf, sizeof(buf), "Active Tokens: %llu",
             (unsigned long long)cap_table_count());
    sd_update_label(11, buf);

    /* RNG */
    snprintf(buf, sizeof(buf), "RNG: %s",
             random_hw_available() ? "Hardware (RDRAND)" : "Software (xorshift128+)");
    sd_update_label(12, buf);
    sd_labels[12]->fg = random_hw_available() ? 0xFF00CC66 : 0xFFCCCC00;

    /* Audit */
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;
    query_result_t *ar = db_execute("SELECT * FROM AuditTable", pid);
    uint32_t audit_count = ar ? ar->row_count : 0;
    if (ar) db_result_free(ar);
    snprintf(buf, sizeof(buf), "Audit Events: %u logged", audit_count);
    sd_update_label(13, buf);

    widget_draw_all(win);
}

static void sd_event(window_t *win, gui_event_t *ev) {
    if (ev->type == EVT_CLOSE) {
        wm_destroy_window(win->id);
        return;
    }
    widget_dispatch(win, ev);
}

static void sd_open_audit(widget_t *w, window_t *win) {
    (void)w; (void)win;
    open_audit_viewer();
}

static void open_security_dashboard(void) {
    window_t *win = wm_create_window("Security Dashboard", 80, 50, 500, 380,
                                      sd_event, sd_paint);
    if (!win) return;

    uint32_t bg = 0xFF1A1A2E;
    int16_t y = 8;

    sd_labels[0] = widget_create_label(12, y, "ENCRYPTION STATUS", 0xFFFFCC00, bg);
    widget_add(win, sd_labels[0]);
    y += 24;

    sd_labels[1] = widget_create_label(12, y, "Algorithm: AES-128-CBC", 0xFF00CC66, bg);
    widget_add(win, sd_labels[1]);
    y += 18;

    sd_labels[2] = widget_create_label(12, y, "Key Derivation: HMAC-SHA256", 0xFF00CC66, bg);
    widget_add(win, sd_labels[2]);
    y += 24;

    /* 6 table labels (indices 3..8) */
    for (int i = 0; i < 6; i++) {
        sd_labels[3 + i] = widget_create_label(12, y, "  ...", 0xFF00CC66, bg);
        widget_add(win, sd_labels[3 + i]);
        y += 18;
    }
    y += 8;

    /* Right column header */
    sd_labels[9] = widget_create_label(12, y, "SECURITY TOKENS", 0xFFFFCC00, bg);
    widget_add(win, sd_labels[9]);
    y += 22;

    sd_labels[10] = widget_create_label(12, y, "Capability System: HMAC-sealed", 0xFF00DDAA, bg);
    widget_add(win, sd_labels[10]);
    y += 18;

    sd_labels[11] = widget_create_label(12, y, "Active Tokens: ...", 0xFF00DDAA, bg);
    widget_add(win, sd_labels[11]);
    y += 22;

    sd_labels[12] = widget_create_label(12, y, "RNG: ...", 0xFF00CC66, bg);
    widget_add(win, sd_labels[12]);
    y += 18;

    sd_labels[13] = widget_create_label(12, y, "Audit Events: ...", 0xFF00DDAA, bg);
    widget_add(win, sd_labels[13]);
    y += 28;

    /* View Audit Log button */
    widget_t *abtn = widget_create_button(12, y, 140, 24, "View Audit Log", sd_open_audit);
    widget_add(win, abtn);
}

/* ---- Audit Log Viewer Application ---- */
static widget_t *al_listview;
static widget_t *al_filter_box;
static char al_lv_data[LISTVIEW_MAX_ITEMS][LISTVIEW_ITEM_MAX];

static void al_refresh(widget_t *w, window_t *win) {
    (void)w; (void)win;

    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    query_result_t *ar = db_execute("SELECT * FROM AuditTable", pid);
    listview_clear(al_listview);

    if (!ar || ar->row_count == 0) {
        listview_add_item(al_listview, "No audit events.");
        if (ar) db_result_free(ar);
        return;
    }

    const char *filter = textbox_get_text(al_filter_box);
    size_t flen = strlen(filter);

    for (uint32_t r = 0; r < ar->row_count; r++) {
        uint64_t ts   = ar->rows[r].fields[1].u64_val;
        uint64_t apid = ar->rows[r].fields[2].u64_val;
        const char *action = ar->rows[r].fields[3].str_val.data;
        const char *result = ar->rows[r].fields[5].str_val.data;

        /* Apply filter if set */
        if (flen > 0) {
            if (strncasecmp(action, filter, flen) != 0 &&
                strncasecmp(result, filter, flen) != 0)
                continue;
        }

        uint64_t secs = ts / 1000;
        uint64_t mins = secs / 60;
        secs %= 60;

        char line[LISTVIEW_ITEM_MAX];
        snprintf(line, sizeof(line), "[%02llu:%02llu] %-10s PID:%llu %s",
                 (unsigned long long)mins, (unsigned long long)secs,
                 action, (unsigned long long)apid, result);
        listview_add_item(al_listview, line);
    }

    char summary[64];
    snprintf(summary, sizeof(summary), "-- %d entries --", al_listview->lv_count);
    listview_add_item(al_listview, summary);

    db_result_free(ar);
}

static void al_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);
    widget_draw_all(win);
}

static void al_event(window_t *win, gui_event_t *ev) {
    if (ev->type == EVT_CLOSE) {
        wm_destroy_window(win->id);
        return;
    }
    widget_dispatch(win, ev);
}

static void open_audit_viewer(void) {
    window_t *win = wm_create_window("Audit Log", 140, 60, 580, 400,
                                      al_event, al_paint);
    if (!win) return;

    al_filter_box = widget_create_textbox(4, 4, win->client_w - 160, 24);
    widget_add(win, al_filter_box);

    widget_t *rbtn = widget_create_button(win->client_w - 152, 4, 72, 24,
                                           "Refresh", al_refresh);
    widget_add(win, rbtn);

    widget_t *lbl = widget_create_label(win->client_w - 76, 8, "Filter", 0xFF808080, 0xFF1A1A2E);
    widget_add(win, lbl);

    al_listview = widget_create_listview(4, 34, win->client_w - 8, win->client_h - 40);
    al_listview->lv_items = al_lv_data;
    widget_add(win, al_listview);

    /* Initial load */
    al_refresh(NULL, win);
}

/* ---- Capability Manager Application ---- */
static widget_t *cm_listview;
static char cm_lv_data[LISTVIEW_MAX_ITEMS][LISTVIEW_ITEM_MAX];

/* Decode rights bitmask to human-readable string */
static void decode_rights(uint32_t rights, char *buf, size_t buf_size) {
    buf[0] = '\0';
    if (rights & 0x01) strncat(buf, "R", buf_size - strlen(buf) - 1);
    if (rights & 0x02) strncat(buf, "W", buf_size - strlen(buf) - 1);
    if (rights & 0x04) strncat(buf, "X", buf_size - strlen(buf) - 1);
    if (rights & 0x08) strncat(buf, "D", buf_size - strlen(buf) - 1);
    if (rights & 0x10) strncat(buf, "G", buf_size - strlen(buf) - 1);
    if (rights & 0x20) strncat(buf, "V", buf_size - strlen(buf) - 1);
    if (buf[0] == '\0') strncpy(buf, "NONE", buf_size - 1);
}

static void cm_refresh(widget_t *w, window_t *win) {
    (void)w; (void)win;
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    query_result_t *result = db_execute("SELECT * FROM CapabilityTable", pid);
    listview_clear(cm_listview);

    if (!result || result->row_count == 0) {
        listview_add_item(cm_listview, "No capabilities found.");
        if (result) db_result_free(result);
        return;
    }

    /* Header line */
    listview_add_item(cm_listview, "  CAP_ID  OBJ_ID  PID    RIGHTS  STATUS");

    for (uint32_t r = 0; r < result->row_count; r++) {
        /* 0=cap_id, 1=object_id, 2=owner_pid, 3=rights, 4=parent_id, 5=revoked, 6=created */
        uint64_t cap_id  = result->rows[r].fields[0].u64_val;
        uint64_t obj_id  = result->rows[r].fields[1].u64_val;
        uint64_t own_pid = result->rows[r].fields[2].u64_val;
        uint32_t rights  = result->rows[r].fields[3].u32_val;
        bool     revoked = result->rows[r].fields[5].bool_val;

        char rights_str[16];
        decode_rights(rights, rights_str, sizeof(rights_str));

        char line[LISTVIEW_ITEM_MAX];
        snprintf(line, sizeof(line), "  %-8llu %-7llu %-6llu %-7s %s",
                 (unsigned long long)cap_id,
                 (unsigned long long)obj_id,
                 (unsigned long long)own_pid,
                 rights_str,
                 revoked ? "REVOKED" : "ACTIVE");
        listview_add_item(cm_listview, line);
    }

    char summary[64];
    snprintf(summary, sizeof(summary), "-- %u capability(s) --", result->row_count);
    listview_add_item(cm_listview, summary);

    db_result_free(result);
}

static void cm_revoke_btn(widget_t *w, window_t *win) {
    (void)w;
    int16_t sel = listview_get_selected(cm_listview);
    if (sel <= 0) return; /* 0 = header row */

    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    /* Query capabilities to get cap_id of selected entry */
    query_result_t *result = db_execute("SELECT * FROM CapabilityTable", pid);
    if (!result) return;

    uint32_t data_idx = (uint32_t)(sel - 1); /* -1 for header row */
    if (data_idx >= result->row_count) {
        db_result_free(result);
        return;
    }

    uint64_t cap_id = result->rows[data_idx].fields[0].u64_val;
    db_result_free(result);

    char sql[128];
    snprintf(sql, sizeof(sql), "REVOKE %llu", (unsigned long long)cap_id);
    query_result_t *rr = db_execute(sql, pid);
    if (rr) db_result_free(rr);

    cm_refresh(NULL, win);
}

static void cm_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);
    widget_draw_all(win);
}

static void cm_event(window_t *win, gui_event_t *ev) {
    if (ev->type == EVT_CLOSE) {
        wm_destroy_window(win->id);
        return;
    }
    widget_dispatch(win, ev);
}

static void open_cap_manager(void) {
    window_t *win = wm_create_window("Capability Manager", 100, 70, 560, 380,
                                      cm_event, cm_paint);
    if (!win) return;

    widget_t *rbtn = widget_create_button(4, 2, 72, 22, "Refresh", cm_refresh);
    widget_add(win, rbtn);

    widget_t *vbtn = widget_create_button(80, 2, 100, 22, "Revoke Sel.", cm_revoke_btn);
    widget_add(win, vbtn);

    cm_listview = widget_create_listview(4, 28, win->client_w - 8, win->client_h - 34);
    cm_listview->lv_items = cm_lv_data;
    widget_add(win, cm_listview);

    cm_refresh(NULL, win);
}

/* ---- Object Manager Application ---- */
static widget_t *om_listview;
static char om_lv_data[LISTVIEW_MAX_ITEMS][LISTVIEW_ITEM_MAX];

static void om_refresh(widget_t *w, window_t *win) {
    (void)w; (void)win;
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    query_result_t *result = db_execute("SELECT * FROM ObjectTable", pid);
    listview_clear(om_listview);

    if (!result || result->row_count == 0) {
        listview_add_item(om_listview, "No objects found.");
        if (result) db_result_free(result);
        return;
    }

    /* Header line */
    listview_add_item(om_listview, "  TYPE       NAME             DATA");

    for (uint32_t r = 0; r < result->row_count; r++) {
        /* 0=obj_id, 1=name, 2=type, 3=data, 4=owner_pid, 5=size, 6=created */
        const char *name = result->rows[r].fields[1].str_val.data;
        const char *type = result->rows[r].fields[2].str_val.data;
        const char *data = result->rows[r].fields[3].str_val.data;

        char preview[32];
        strncpy(preview, data, 28);
        preview[28] = '\0';
        if (strlen(data) > 28) strncat(preview, "...", sizeof(preview) - strlen(preview) - 1);

        char line[LISTVIEW_ITEM_MAX];
        snprintf(line, sizeof(line), "  %-10s %-16s %s", type, name, preview);
        listview_add_item(om_listview, line);
    }

    char summary[64];
    snprintf(summary, sizeof(summary), "-- %u object(s) --", result->row_count);
    listview_add_item(om_listview, summary);

    db_result_free(result);
}

static void om_delete_btn(widget_t *w, window_t *win) {
    (void)w;
    int16_t sel = listview_get_selected(om_listview);
    if (sel <= 0) return; /* 0 = header row */

    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    query_result_t *result = db_execute("SELECT * FROM ObjectTable", pid);
    if (!result) return;

    uint32_t data_idx = (uint32_t)(sel - 1);
    if (data_idx >= result->row_count) {
        db_result_free(result);
        return;
    }

    const char *name = result->rows[data_idx].fields[1].str_val.data;

    char sql[256];
    snprintf(sql, sizeof(sql), "DELETE FROM ObjectTable WHERE name = '%s'", name);
    db_result_free(result);

    query_result_t *dr = db_execute(sql, pid);
    if (dr) db_result_free(dr);

    om_refresh(NULL, win);
}

static void om_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);
    widget_draw_all(win);
}

static void om_event(window_t *win, gui_event_t *ev) {
    if (ev->type == EVT_CLOSE) {
        wm_destroy_window(win->id);
        return;
    }
    widget_dispatch(win, ev);
}

static void open_object_manager(void) {
    window_t *win = wm_create_window("Object Manager", 120, 80, 520, 380,
                                      om_event, om_paint);
    if (!win) return;

    widget_t *rbtn = widget_create_button(4, 2, 72, 22, "Refresh", om_refresh);
    widget_add(win, rbtn);

    widget_t *dbtn = widget_create_button(80, 2, 100, 22, "Delete Sel.", om_delete_btn);
    widget_add(win, dbtn);

    om_listview = widget_create_listview(4, 28, win->client_w - 8, win->client_h - 34);
    om_listview->lv_items = om_lv_data;
    widget_add(win, om_listview);

    om_refresh(NULL, win);
}

/* ---- Calculator Application ---- */
#define CALC_WIN_W   260
#define CALC_WIN_H   340
#define CALC_BTN_W   48
#define CALC_BTN_H   36
#define CALC_BTN_PAD 4
#define CALC_ROWS    5
#define CALC_COLS    4

static char    calc_display[32];
static int64_t calc_value;
static int64_t calc_operand;
static char    calc_op;
static bool    calc_new_input;
static char    calc_input[20];
static int     calc_input_len;

static const char calc_buttons[CALC_ROWS][CALC_COLS] = {
    { 'C', '/', '*', '<' },
    { '7', '8', '9', '-' },
    { '4', '5', '6', '+' },
    { '1', '2', '3', '=' },
    { '0', ' ', ' ', '=' },
};

static void calc_execute_pending(void) {
    switch (calc_op) {
    case '+': calc_value += calc_operand; break;
    case '-': calc_value -= calc_operand; break;
    case '*': calc_value *= calc_operand; break;
    case '/':
        if (calc_operand != 0) calc_value /= calc_operand;
        else { strcpy(calc_display, "Error: /0"); return; }
        break;
    }
    snprintf(calc_display, sizeof(calc_display), "%lld", (long long)calc_value);
}

static void calc_handle_button(char btn) {
    if (btn >= '0' && btn <= '9') {
        if (calc_new_input) {
            calc_input_len = 0;
            calc_new_input = false;
        }
        if (calc_input_len < 18) {
            calc_input[calc_input_len++] = btn;
            calc_input[calc_input_len] = '\0';
        }
        strncpy(calc_display, calc_input, sizeof(calc_display) - 1);
    } else if (btn == 'C') {
        calc_value = 0;
        calc_operand = 0;
        calc_op = '\0';
        calc_input_len = 0;
        calc_input[0] = '\0';
        strcpy(calc_display, "0");
        calc_new_input = true;
    } else if (btn == '<') {
        if (calc_input_len > 0) {
            calc_input[--calc_input_len] = '\0';
            if (calc_input_len == 0) strcpy(calc_display, "0");
            else strncpy(calc_display, calc_input, sizeof(calc_display) - 1);
        }
    } else if (btn == '+' || btn == '-' || btn == '*' || btn == '/') {
        int64_t val = (int64_t)atoi(calc_input);
        if (calc_op) {
            calc_operand = val;
            calc_execute_pending();
        } else {
            calc_value = val;
        }
        calc_op = btn;
        calc_new_input = true;
    } else if (btn == '=') {
        calc_operand = (int64_t)atoi(calc_input);
        if (calc_op) calc_execute_pending();
        else calc_value = calc_operand;
        calc_op = '\0';
        calc_new_input = true;
        snprintf(calc_display, sizeof(calc_display), "%lld", (long long)calc_value);
        strncpy(calc_input, calc_display, sizeof(calc_input) - 1);
        calc_input_len = (int)strlen(calc_input);
    }
}

static void calc_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);
    uint16_t cw = win->client_w, ch = win->client_h;

    #define CPIX(px, py, color) do { \
        if ((px) >= 0 && (px) < cw && (py) >= 0 && (py) < ch) \
            win->canvas[(py) * cw + (px)] = (color); \
    } while(0)
    #define CHLINE(x0, y0, width, color) do { \
        for (int16_t _i = 0; _i < (width); _i++) CPIX((x0)+_i, y0, color); \
    } while(0)
    #define CFILL(rx, ry, rw, rh, color) do { \
        for (int16_t _r = 0; _r < (rh); _r++) CHLINE(rx, (ry)+_r, rw, color); \
    } while(0)
    #define CTEXT(tx, ty, str, fg_c, bg_c) do { \
        const char *_s = (str); int16_t _x = (tx); \
        while (*_s) { \
            const uint8_t *_g = font_8x16[(uint8_t)*_s]; \
            for (int _gy = 0; _gy < FONT_HEIGHT; _gy++) { \
                uint8_t _bits = _g[_gy]; \
                for (int _gx = 0; _gx < FONT_WIDTH; _gx++) { \
                    CPIX(_x + _gx, (ty) + _gy, \
                         (_bits & (0x80 >> _gx)) ? (fg_c) : (bg_c)); \
                } \
            } \
            _x += FONT_WIDTH; _s++; \
        } \
    } while(0)

    /* Display area */
    int16_t disp_x = 8, disp_y = 8;
    int16_t disp_w = cw - 16, disp_h = 44;
    CFILL(disp_x, disp_y, disp_w, disp_h, 0xFF0A0A1A);
    /* Border */
    CHLINE(disp_x, disp_y, disp_w, 0xFF555577);
    CHLINE(disp_x, disp_y + disp_h - 1, disp_w, 0xFF555577);
    for (int16_t r = 0; r < disp_h; r++) {
        CPIX(disp_x, disp_y + r, 0xFF555577);
        CPIX(disp_x + disp_w - 1, disp_y + r, 0xFF555577);
    }
    /* Right-aligned display text */
    int16_t text_w = (int16_t)(strlen(calc_display) * FONT_WIDTH);
    int16_t text_x = disp_x + disp_w - text_w - 8;
    int16_t text_y = disp_y + (disp_h - FONT_HEIGHT) / 2;
    CTEXT(text_x, text_y, calc_display, 0xFFFFCC00, 0xFF0A0A1A);

    /* Operator indicator */
    if (calc_op) {
        char op_str[2] = { calc_op, '\0' };
        CTEXT(disp_x + 6, text_y, op_str, 0xFF808080, 0xFF0A0A1A);
    }

    /* Button grid */
    int16_t grid_y = disp_y + disp_h + CALC_BTN_PAD + 4;
    for (int r = 0; r < CALC_ROWS; r++) {
        int16_t bx = 8;
        for (int c = 0; c < CALC_COLS; c++) {
            char ch_btn = calc_buttons[r][c];
            if (ch_btn == ' ') { bx += CALC_BTN_W + CALC_BTN_PAD; continue; }

            /* Button width: '0' in last row spans 3 columns */
            int16_t bw = CALC_BTN_W;
            if (r == 4 && c == 0)
                bw = CALC_BTN_W * 3 + CALC_BTN_PAD * 2;

            /* Button colors */
            uint32_t btn_bg, btn_fg;
            if (ch_btn >= '0' && ch_btn <= '9') {
                btn_bg = 0xFF2A2A4A; btn_fg = 0xFFFFFFFF;
            } else if (ch_btn == 'C') {
                btn_bg = 0xFF663333; btn_fg = 0xFFFFAAAA;
            } else if (ch_btn == '=') {
                btn_bg = 0xFF1A4444; btn_fg = 0xFF00DDAA;
            } else if (ch_btn == '<') {
                btn_bg = 0xFF333355; btn_fg = 0xFFCCCCCC;
            } else {
                btn_bg = 0xFF333355; btn_fg = 0xFFFFCC00;
            }

            /* Draw button rect */
            CFILL(bx, grid_y, bw, CALC_BTN_H, btn_bg);
            /* Border */
            CHLINE(bx, grid_y, bw, 0xFF555577);
            CHLINE(bx, grid_y + CALC_BTN_H - 1, bw, 0xFF555577);
            for (int16_t br = 0; br < CALC_BTN_H; br++) {
                CPIX(bx, grid_y + br, 0xFF555577);
                CPIX(bx + bw - 1, grid_y + br, 0xFF555577);
            }

            /* Centered character */
            char btn_str[2] = { ch_btn, '\0' };
            int16_t tx = bx + (bw - FONT_WIDTH) / 2;
            int16_t ty = grid_y + (CALC_BTN_H - FONT_HEIGHT) / 2;
            CTEXT(tx, ty, btn_str, btn_fg, btn_bg);

            bx += bw + CALC_BTN_PAD;
        }
        grid_y += CALC_BTN_H + CALC_BTN_PAD;
    }

    #undef CPIX
    #undef CHLINE
    #undef CFILL
    #undef CTEXT
}

static void calc_event(window_t *win, gui_event_t *ev) {
    if (ev->type == EVT_CLOSE) {
        wm_destroy_window(win->id);
        return;
    }

    if (ev->type == EVT_KEY_DOWN) {
        uint8_t key = ev->key;
        if (key >= '0' && key <= '9') calc_handle_button(key);
        else if (key == '+' || key == '-' || key == '*' || key == '/') calc_handle_button(key);
        else if (key == '\n' || key == '=') calc_handle_button('=');
        else if (key == '\b') calc_handle_button('<');
        else if (key == 'c' || key == 'C') calc_handle_button('C');
    }

    if (ev->type == EVT_MOUSE_DOWN) {
        /* Hit-test button grid */
        int16_t disp_h_total = 8 + 44 + CALC_BTN_PAD + 4;
        int row = (ev->mouse_y - disp_h_total) / (CALC_BTN_H + CALC_BTN_PAD);
        int col = (ev->mouse_x - 8) / (CALC_BTN_W + CALC_BTN_PAD);
        if (row >= 0 && row < CALC_ROWS && col >= 0 && col < CALC_COLS) {
            char btn = calc_buttons[row][col];
            if (btn != ' ') calc_handle_button(btn);
        }
    }
}

static void open_calculator(void) {
    /* Reset state */
    strcpy(calc_display, "0");
    calc_value = 0;
    calc_operand = 0;
    calc_op = '\0';
    calc_new_input = true;
    calc_input_len = 0;
    calc_input[0] = '\0';

    wm_create_window("Calculator", 200, 80, CALC_WIN_W, CALC_WIN_H,
                      calc_event, calc_paint);
}

/* ---- VaultPad Text Editor ---- */
#define VP_MAX_LINES  128
#define VP_LINE_MAX   255
#define VP_WIN_W      640
#define VP_WIN_H      440
#define VP_GUTTER_W   40
#define VP_TOOLBAR_H  28
#define VP_STATUS_H   20

static char vp_lines[VP_MAX_LINES][VP_LINE_MAX + 1];
static int  vp_line_count;
static int  vp_cursor_row;
static int  vp_cursor_col;
static int  vp_scroll_row;
static bool vp_modified;
static char vp_doc_name[64];
static bool vp_show_open;

static widget_t *vp_name_box;
static widget_t *vp_file_list;
static char vp_fl_data[LISTVIEW_MAX_ITEMS][LISTVIEW_ITEM_MAX];

/* Escape single quotes for SQL: ' → '' */
static void vp_escape_sql(const char *src, char *dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 2; i++) {
        if (src[i] == '\'') { dst[j++] = '\''; dst[j++] = '\''; }
        else dst[j++] = src[i];
    }
    dst[j] = '\0';
}

static void vp_save_doc(widget_t *w, window_t *win) {
    (void)w;
    const char *name = textbox_get_text(vp_name_box);
    if (strlen(name) == 0) return;
    strncpy(vp_doc_name, name, sizeof(vp_doc_name) - 1);

    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    char sql[600];
    snprintf(sql, sizeof(sql),
        "DELETE FROM ObjectTable WHERE name = '%s' AND type = 'document'", vp_doc_name);
    query_result_t *r = db_execute(sql, pid);
    if (r) db_result_free(r);

    char escaped[VP_LINE_MAX * 2 + 1];
    for (int i = 0; i < vp_line_count; i++) {
        vp_escape_sql(vp_lines[i], escaped, sizeof(escaped));
        snprintf(sql, sizeof(sql),
            "INSERT INTO ObjectTable (name, type, data) VALUES ('%s', 'document', '%s')",
            vp_doc_name, escaped);
        r = db_execute(sql, pid);
        if (r) db_result_free(r);
    }

    vp_modified = false;
    (void)win;
}

static void vp_load_doc(const char *name) {
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    char sql[512];
    snprintf(sql, sizeof(sql),
        "SELECT * FROM ObjectTable WHERE name = '%s' AND type = 'document'", name);
    query_result_t *result = db_execute(sql, pid);

    memset(vp_lines, 0, sizeof(vp_lines));
    vp_line_count = 0;

    if (result && !result->error_code && result->row_count > 0) {
        for (uint32_t i = 0; i < result->row_count && (int)i < VP_MAX_LINES; i++) {
            strncpy(vp_lines[vp_line_count], result->rows[i].fields[3].str_val.data,
                    VP_LINE_MAX);
            vp_line_count++;
        }
    }
    if (result) db_result_free(result);
    if (vp_line_count == 0) { vp_line_count = 1; vp_lines[0][0] = '\0'; }

    strncpy(vp_doc_name, name, sizeof(vp_doc_name) - 1);
    textbox_set_text(vp_name_box, vp_doc_name);
    vp_cursor_row = 0;
    vp_cursor_col = 0;
    vp_scroll_row = 0;
    vp_modified = false;
}

static void vp_new_doc(widget_t *w, window_t *win) {
    (void)w; (void)win;
    memset(vp_lines, 0, sizeof(vp_lines));
    vp_line_count = 1;
    vp_cursor_row = 0;
    vp_cursor_col = 0;
    vp_scroll_row = 0;
    vp_modified = false;
    vp_doc_name[0] = '\0';
    textbox_set_text(vp_name_box, "untitled");
    vp_show_open = false;
}

static void vp_open_dialog(widget_t *w, window_t *win) {
    (void)w; (void)win;
    /* Populate file list with unique document names */
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    query_result_t *result = db_execute(
        "SELECT * FROM ObjectTable WHERE type = 'document'", pid);
    listview_clear(vp_file_list);

    if (result && result->row_count > 0) {
        char seen[64][64];
        int seen_count = 0;
        for (uint32_t i = 0; i < result->row_count; i++) {
            const char *n = result->rows[i].fields[1].str_val.data;
            bool found = false;
            for (int j = 0; j < seen_count; j++) {
                if (strcmp(seen[j], n) == 0) { found = true; break; }
            }
            if (!found && seen_count < 64) {
                strncpy(seen[seen_count], n, 63);
                seen[seen_count][63] = '\0';
                listview_add_item(vp_file_list, n);
                seen_count++;
            }
        }
    }
    if (result) db_result_free(result);

    vp_show_open = true;
}

static void vp_file_selected(widget_t *w, window_t *win, int16_t index) {
    (void)win;
    if (index < 0 || index >= w->lv_count) return;
    vp_load_doc(w->lv_items[index]);
    vp_show_open = false;
}

static void vp_ensure_visible(int visible_rows) {
    if (vp_cursor_row < vp_scroll_row)
        vp_scroll_row = vp_cursor_row;
    if (vp_cursor_row >= vp_scroll_row + visible_rows)
        vp_scroll_row = vp_cursor_row - visible_rows + 1;
    if (vp_scroll_row < 0) vp_scroll_row = 0;
}

static void vp_insert_char(char ch) {
    char *line = vp_lines[vp_cursor_row];
    int len = (int)strlen(line);
    if (len >= VP_LINE_MAX - 1) return;
    memmove(&line[vp_cursor_col + 1], &line[vp_cursor_col], len - vp_cursor_col + 1);
    line[vp_cursor_col] = ch;
    vp_cursor_col++;
    vp_modified = true;
}

static void vp_insert_newline(void) {
    if (vp_line_count >= VP_MAX_LINES) return;
    for (int i = vp_line_count; i > vp_cursor_row + 1; i--)
        memcpy(vp_lines[i], vp_lines[i - 1], VP_LINE_MAX + 1);
    vp_line_count++;
    char *cur = vp_lines[vp_cursor_row];
    strcpy(vp_lines[vp_cursor_row + 1], &cur[vp_cursor_col]);
    cur[vp_cursor_col] = '\0';
    vp_cursor_row++;
    vp_cursor_col = 0;
    vp_modified = true;
}

static void vp_backspace(void) {
    if (vp_cursor_col > 0) {
        char *line = vp_lines[vp_cursor_row];
        int len = (int)strlen(line);
        memmove(&line[vp_cursor_col - 1], &line[vp_cursor_col], len - vp_cursor_col + 1);
        vp_cursor_col--;
        vp_modified = true;
    } else if (vp_cursor_row > 0) {
        int prev_len = (int)strlen(vp_lines[vp_cursor_row - 1]);
        int cur_len = (int)strlen(vp_lines[vp_cursor_row]);
        if (prev_len + cur_len < VP_LINE_MAX) {
            strcat(vp_lines[vp_cursor_row - 1], vp_lines[vp_cursor_row]);
            for (int i = vp_cursor_row; i < vp_line_count - 1; i++)
                memcpy(vp_lines[i], vp_lines[i + 1], VP_LINE_MAX + 1);
            vp_line_count--;
            vp_cursor_row--;
            vp_cursor_col = prev_len;
            vp_modified = true;
        }
    }
}

static void vp_delete_forward(void) {
    char *line = vp_lines[vp_cursor_row];
    int len = (int)strlen(line);
    if (vp_cursor_col < len) {
        memmove(&line[vp_cursor_col], &line[vp_cursor_col + 1], len - vp_cursor_col);
        vp_modified = true;
    } else if (vp_cursor_row < vp_line_count - 1) {
        int next_len = (int)strlen(vp_lines[vp_cursor_row + 1]);
        if (len + next_len < VP_LINE_MAX) {
            strcat(line, vp_lines[vp_cursor_row + 1]);
            for (int i = vp_cursor_row + 1; i < vp_line_count - 1; i++)
                memcpy(vp_lines[i], vp_lines[i + 1], VP_LINE_MAX + 1);
            vp_line_count--;
            vp_modified = true;
        }
    }
}

static void vp_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);
    widget_draw_all(win);

    uint16_t cw = win->client_w, ch = win->client_h;

    #define CPIX(px, py, color) do { \
        if ((px) >= 0 && (px) < cw && (py) >= 0 && (py) < ch) \
            win->canvas[(py) * cw + (px)] = (color); \
    } while(0)
    #define CHLINE(x0, y0, width, color) do { \
        for (int16_t _i = 0; _i < (width); _i++) CPIX((x0)+_i, y0, color); \
    } while(0)
    #define CFILL(rx, ry, rw, rh, color) do { \
        for (int16_t _r = 0; _r < (rh); _r++) CHLINE(rx, (ry)+_r, rw, color); \
    } while(0)
    #define CTEXT(tx, ty, str, fg_c, bg_c) do { \
        const char *_s = (str); int16_t _x = (tx); \
        while (*_s) { \
            const uint8_t *_g = font_8x16[(uint8_t)*_s]; \
            for (int _gy = 0; _gy < FONT_HEIGHT; _gy++) { \
                uint8_t _bits = _g[_gy]; \
                for (int _gx = 0; _gx < FONT_WIDTH; _gx++) { \
                    CPIX(_x + _gx, (ty) + _gy, \
                         (_bits & (0x80 >> _gx)) ? (fg_c) : (bg_c)); \
                } \
            } \
            _x += FONT_WIDTH; _s++; \
        } \
    } while(0)

    /* Separator below toolbar */
    CHLINE(0, VP_TOOLBAR_H, cw, 0xFF555577);

    /* Editor area */
    int16_t edit_y = VP_TOOLBAR_H + 2;
    int16_t edit_h = ch - VP_TOOLBAR_H - VP_STATUS_H - 4;
    int visible_rows = edit_h / FONT_HEIGHT;
    vp_ensure_visible(visible_rows);

    for (int i = 0; i < visible_rows; i++) {
        int line_idx = vp_scroll_row + i;
        if (line_idx >= vp_line_count) break;
        int16_t ly = edit_y + i * FONT_HEIGHT;

        /* Line number */
        char num[6];
        snprintf(num, sizeof(num), "%3d", line_idx + 1);
        CTEXT(2, ly, num, 0xFF666688, 0xFF1A1A2E);

        /* Gutter separator */
        for (int16_t gy = 0; gy < FONT_HEIGHT; gy++)
            CPIX(VP_GUTTER_W - 2, ly + gy, 0xFF333355);

        /* Line text */
        int16_t text_x = VP_GUTTER_W;
        int max_chars = (cw - VP_GUTTER_W) / FONT_WIDTH;
        char *line = vp_lines[line_idx];
        int len = (int)strlen(line);
        int draw_len = len < max_chars ? len : max_chars;
        for (int c = 0; c < draw_len; c++) {
            char ch_str[2] = { line[c], '\0' };
            CTEXT(text_x + c * FONT_WIDTH, ly, ch_str, 0xFFCCCCCC, 0xFF1A1A2E);
        }

        /* Cursor */
        if (line_idx == vp_cursor_row) {
            int16_t cx = text_x + vp_cursor_col * FONT_WIDTH;
            for (int16_t cy = 0; cy < FONT_HEIGHT; cy++) {
                CPIX(cx, ly + cy, 0xFFFFCC00);
                CPIX(cx + 1, ly + cy, 0xFFFFCC00);
            }
        }
    }

    /* Status bar */
    CHLINE(0, ch - VP_STATUS_H, cw, 0xFF555577);
    char status[80];
    snprintf(status, sizeof(status), " Ln %d, Col %d | %s | %d lines | Ctrl+S Save",
             vp_cursor_row + 1, vp_cursor_col + 1,
             vp_modified ? "Modified" : "Saved", vp_line_count);
    CTEXT(4, ch - VP_STATUS_H + 2, status, 0xFF808080, 0xFF1A1A2E);

    /* Open dialog overlay */
    if (vp_show_open) {
        int16_t dx = 80, dy = 60;
        int16_t dw = cw - 160, dh = ch - 120;
        CFILL(dx, dy, dw, dh, 0xFF0A0A1A);
        CHLINE(dx, dy, dw, 0xFF555577);
        CHLINE(dx, dy + dh - 1, dw, 0xFF555577);
        for (int16_t r = 0; r < dh; r++) {
            CPIX(dx, dy + r, 0xFF555577);
            CPIX(dx + dw - 1, dy + r, 0xFF555577);
        }
        CTEXT(dx + 8, dy + 4, "Open Document (click to select)", 0xFFFFCC00, 0xFF0A0A1A);
    }

    #undef CPIX
    #undef CHLINE
    #undef CFILL
    #undef CTEXT
}

static void vp_event(window_t *win, gui_event_t *ev) {
    if (ev->type == EVT_CLOSE) {
        wm_destroy_window(win->id);
        return;
    }

    /* If open dialog is showing, only allow widget interactions */
    if (vp_show_open) {
        if (ev->type == EVT_KEY_DOWN && ev->key == 0x1B) { /* Esc */
            vp_show_open = false;
            return;
        }
        widget_dispatch(win, ev);
        return;
    }

    if (ev->type == EVT_KEY_DOWN) {
        uint8_t key = ev->key;

        /* Ctrl shortcuts */
        if (keyboard_ctrl_held()) {
            if (key == 's' || key == 'S') { vp_save_doc(NULL, win); return; }
            if (key == 'q' || key == 'Q') { wm_destroy_window(win->id); return; }
            if (key == 'n' || key == 'N') { vp_new_doc(NULL, win); return; }
            return;
        }

        int visible_rows = (win->client_h - VP_TOOLBAR_H - VP_STATUS_H - 4) / FONT_HEIGHT;

        switch (key) {
        case KEY_UP:
            if (vp_cursor_row > 0) {
                vp_cursor_row--;
                int len = (int)strlen(vp_lines[vp_cursor_row]);
                if (vp_cursor_col > len) vp_cursor_col = len;
            }
            break;
        case KEY_DOWN:
            if (vp_cursor_row < vp_line_count - 1) {
                vp_cursor_row++;
                int len = (int)strlen(vp_lines[vp_cursor_row]);
                if (vp_cursor_col > len) vp_cursor_col = len;
            }
            break;
        case KEY_LEFT:
            if (vp_cursor_col > 0) vp_cursor_col--;
            else if (vp_cursor_row > 0) {
                vp_cursor_row--;
                vp_cursor_col = (int)strlen(vp_lines[vp_cursor_row]);
            }
            break;
        case KEY_RIGHT: {
            int len = (int)strlen(vp_lines[vp_cursor_row]);
            if (vp_cursor_col < len) vp_cursor_col++;
            else if (vp_cursor_row < vp_line_count - 1) {
                vp_cursor_row++;
                vp_cursor_col = 0;
            }
            break;
        }
        case KEY_HOME:
            vp_cursor_col = 0;
            break;
        case KEY_END:
            vp_cursor_col = (int)strlen(vp_lines[vp_cursor_row]);
            break;
        case KEY_PGUP:
            vp_cursor_row -= visible_rows;
            if (vp_cursor_row < 0) vp_cursor_row = 0;
            { int len = (int)strlen(vp_lines[vp_cursor_row]);
              if (vp_cursor_col > len) vp_cursor_col = len; }
            break;
        case KEY_PGDN:
            vp_cursor_row += visible_rows;
            if (vp_cursor_row >= vp_line_count) vp_cursor_row = vp_line_count - 1;
            { int len = (int)strlen(vp_lines[vp_cursor_row]);
              if (vp_cursor_col > len) vp_cursor_col = len; }
            break;
        case '\n':
            vp_insert_newline();
            break;
        case '\b':
            vp_backspace();
            break;
        case KEY_DELETE:
            vp_delete_forward();
            break;
        case '\t':
            for (int s = 0; s < 4; s++) vp_insert_char(' ');
            break;
        default:
            if (key >= 0x20 && key < 0x7F)
                vp_insert_char((char)key);
            break;
        }
        return;
    }

    /* Mouse click in editor area: position cursor */
    if (ev->type == EVT_MOUSE_DOWN) {
        int16_t my = ev->mouse_y;
        int16_t mx = ev->mouse_x;
        if (my > VP_TOOLBAR_H && my < (int16_t)(win->client_h - VP_STATUS_H)) {
            int row = (my - VP_TOOLBAR_H - 2) / FONT_HEIGHT + vp_scroll_row;
            int col = (mx - VP_GUTTER_W) / FONT_WIDTH;
            if (row >= 0 && row < vp_line_count) {
                vp_cursor_row = row;
                int len = (int)strlen(vp_lines[vp_cursor_row]);
                vp_cursor_col = col < 0 ? 0 : (col > len ? len : col);
            }
            return;
        }
    }

    widget_dispatch(win, ev);
}

static void open_vaultpad(void) {
    memset(vp_lines, 0, sizeof(vp_lines));
    vp_line_count = 1;
    vp_cursor_row = 0;
    vp_cursor_col = 0;
    vp_scroll_row = 0;
    vp_modified = false;
    vp_doc_name[0] = '\0';
    vp_show_open = false;

    window_t *win = wm_create_window("VaultPad Editor", 60, 40, VP_WIN_W, VP_WIN_H,
                                      vp_event, vp_paint);
    if (!win) return;

    vp_name_box = widget_create_textbox(4, 4, 280, 22);
    textbox_set_text(vp_name_box, "untitled");
    widget_add(win, vp_name_box);

    widget_t *nbtn = widget_create_button(290, 4, 52, 22, "New", vp_new_doc);
    widget_add(win, nbtn);

    widget_t *obtn = widget_create_button(346, 4, 52, 22, "Open", vp_open_dialog);
    widget_add(win, obtn);

    widget_t *sbtn = widget_create_button(402, 4, 52, 22, "Save", vp_save_doc);
    widget_add(win, sbtn);

    vp_file_list = widget_create_listview(100, 80, 440, 280);
    vp_file_list->lv_items = vp_fl_data;
    vp_file_list->on_select = vp_file_selected;
    widget_add(win, vp_file_list);
}

/* ---- Data Grid Application ---- */
#define DG_MAX_COLS    8
#define DG_MAX_ROWS    128
#define DG_CELL_MAX    32
#define DG_WIN_W       700
#define DG_WIN_H       460
#define DG_TABLE_LIST_W 140

static char    dg_cells[DG_MAX_ROWS][DG_MAX_COLS][DG_CELL_MAX];
static column_type_t dg_col_types[DG_MAX_COLS];
static char    dg_col_names[DG_MAX_COLS][MAX_COLUMN_NAME];
static int     dg_col_widths[DG_MAX_COLS];
static int     dg_row_count;
static int     dg_col_count;
static int     dg_scroll_row;
static int     dg_sel_row;
static char    dg_table_name[MAX_TABLE_NAME];
static char    dg_tl_data[LISTVIEW_MAX_ITEMS][LISTVIEW_ITEM_MAX];
static widget_t *dg_table_list;
static widget_t *dg_status_lbl;

static void dg_load_table(int table_id) {
    table_schema_t *schema = db_get_schema_by_id((uint32_t)table_id);
    if (!schema) return;

    strncpy(dg_table_name, schema->name, MAX_TABLE_NAME - 1);
    dg_col_count = (int)schema->column_count;
    if (dg_col_count > DG_MAX_COLS) dg_col_count = DG_MAX_COLS;

    for (int c = 0; c < dg_col_count; c++) {
        strncpy(dg_col_names[c], schema->columns[c].name, MAX_COLUMN_NAME - 1);
        dg_col_types[c] = schema->columns[c].type;
        int name_len = (int)strlen(dg_col_names[c]);
        int min_w = name_len > 6 ? name_len : 6;
        dg_col_widths[c] = min_w * FONT_WIDTH + 12;
    }

    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT * FROM %s", schema->name);
    query_result_t *result = db_execute(sql, pid);

    dg_row_count = 0;
    memset(dg_cells, 0, sizeof(dg_cells));
    if (result && !result->error_code) {
        for (uint32_t r = 0; r < result->row_count && (int)r < DG_MAX_ROWS; r++) {
            for (int c = 0; c < dg_col_count; c++) {
                field_value_t *f = &result->rows[r].fields[c];
                switch (f->type) {
                case COL_U64:
                    snprintf(dg_cells[dg_row_count][c], DG_CELL_MAX,
                             "%llu", (unsigned long long)f->u64_val);
                    break;
                case COL_I64:
                    snprintf(dg_cells[dg_row_count][c], DG_CELL_MAX,
                             "%lld", (long long)f->i64_val);
                    break;
                case COL_U32:
                    snprintf(dg_cells[dg_row_count][c], DG_CELL_MAX,
                             "%u", (unsigned)f->u32_val);
                    break;
                case COL_STR:
                    strncpy(dg_cells[dg_row_count][c], f->str_val.data, DG_CELL_MAX - 1);
                    break;
                case COL_BOOL:
                    strcpy(dg_cells[dg_row_count][c], f->bool_val ? "true" : "false");
                    break;
                default:
                    strcpy(dg_cells[dg_row_count][c], "...");
                    break;
                }
            }
            dg_row_count++;
        }
    }
    if (result) db_result_free(result);

    dg_sel_row = -1;
    dg_scroll_row = 0;

    char status[80];
    snprintf(status, sizeof(status), "%s | %d rows, %d cols",
             dg_table_name, dg_row_count, dg_col_count);
    strncpy(dg_status_lbl->text, status, WIDGET_TEXT_MAX - 1);
}

static void dg_table_selected(widget_t *w, window_t *win, int16_t index) {
    (void)w; (void)win;
    if (index >= 0 && index < (int16_t)db_get_table_count())
        dg_load_table(index);
}

static void dg_refresh(widget_t *w, window_t *win) {
    (void)w; (void)win;
    /* Reload current table */
    for (uint32_t i = 0; i < db_get_table_count(); i++) {
        table_schema_t *s = db_get_schema_by_id(i);
        if (s && strcmp(s->name, dg_table_name) == 0) {
            dg_load_table((int)i);
            return;
        }
    }
}

static void dg_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);
    widget_draw_all(win);

    uint16_t cw = win->client_w, ch = win->client_h;

    #define CPIX(px, py, color) do { \
        if ((px) >= 0 && (px) < cw && (py) >= 0 && (py) < ch) \
            win->canvas[(py) * cw + (px)] = (color); \
    } while(0)
    #define CHLINE(x0, y0, width, color) do { \
        for (int16_t _i = 0; _i < (width); _i++) CPIX((x0)+_i, y0, color); \
    } while(0)
    #define CFILL(rx, ry, rw, rh, color) do { \
        for (int16_t _r = 0; _r < (rh); _r++) CHLINE(rx, (ry)+_r, rw, color); \
    } while(0)
    #define CTEXT(tx, ty, str, fg_c, bg_c) do { \
        const char *_s = (str); int16_t _x = (tx); \
        while (*_s) { \
            const uint8_t *_g = font_8x16[(uint8_t)*_s]; \
            for (int _gy = 0; _gy < FONT_HEIGHT; _gy++) { \
                uint8_t _bits = _g[_gy]; \
                for (int _gx = 0; _gx < FONT_WIDTH; _gx++) { \
                    CPIX(_x + _gx, (ty) + _gy, \
                         (_bits & (0x80 >> _gx)) ? (fg_c) : (bg_c)); \
                } \
            } \
            _x += FONT_WIDTH; _s++; \
        } \
    } while(0)

    if (dg_col_count == 0) {
        CTEXT(DG_TABLE_LIST_W + 20, 40, "Select a table", 0xFF808080, 0xFF1A1A2E);
        goto dg_done;
    }

    /* Grid area */
    int16_t grid_x = DG_TABLE_LIST_W + 4;
    int16_t grid_y = 28;
    int16_t row_h = FONT_HEIGHT + 4;

    /* Column headers */
    int16_t cx = grid_x + 32; /* after row-number gutter */
    for (int c = 0; c < dg_col_count; c++) {
        if (cx >= cw - 4) break;
        int16_t col_w = (int16_t)dg_col_widths[c];
        CFILL(cx, grid_y, col_w, row_h, 0xFF1A2244);
        CTEXT(cx + 4, grid_y + 2, dg_col_names[c], 0xFFFFCC00, 0xFF1A2244);
        /* Vertical separator */
        for (int16_t ry = 0; ry < row_h; ry++) CPIX(cx + col_w - 1, grid_y + ry, 0xFF444466);
        cx += col_w;
    }
    /* Header bottom line */
    CHLINE(grid_x, grid_y + row_h, cw - grid_x - 4, 0xFF555577);

    /* Row number header */
    CFILL(grid_x, grid_y, 32, row_h, 0xFF1A2244);
    CTEXT(grid_x + 4, grid_y + 2, "#", 0xFFFFCC00, 0xFF1A2244);

    /* Data rows */
    int16_t data_y = grid_y + row_h + 1;
    int visible_rows = (ch - data_y - 24) / row_h;

    for (int r = 0; r < visible_rows && (r + dg_scroll_row) < dg_row_count; r++) {
        int data_row = r + dg_scroll_row;
        int16_t ry = data_y + r * row_h;
        bool selected = (data_row == dg_sel_row);

        uint32_t row_bg = selected ? 0xFF1A3366
                        : (data_row & 1) ? 0xFF10102A : 0xFF0A0A1A;

        /* Row number */
        char rnum[6];
        snprintf(rnum, sizeof(rnum), "%d", data_row + 1);
        CFILL(grid_x, ry, 32, row_h, row_bg);
        CTEXT(grid_x + 4, ry + 2, rnum, 0xFF666688, row_bg);

        /* Cells */
        cx = grid_x + 32;
        for (int c = 0; c < dg_col_count; c++) {
            if (cx >= cw - 4) break;
            int16_t col_w = (int16_t)dg_col_widths[c];
            CFILL(cx, ry, col_w, row_h, row_bg);

            uint32_t cell_fg;
            switch (dg_col_types[c]) {
            case COL_U64: case COL_I64: case COL_U32: case COL_U8:
                cell_fg = 0xFF66CCFF; break;
            case COL_STR:
                cell_fg = 0xFF00DDAA; break;
            case COL_BOOL:
                cell_fg = 0xFFFFCC00; break;
            default:
                cell_fg = 0xFFCCCCCC; break;
            }

            /* Truncate text to fit column */
            char cell[DG_CELL_MAX];
            strncpy(cell, dg_cells[data_row][c], DG_CELL_MAX - 1);
            cell[DG_CELL_MAX - 1] = '\0';
            int max_chars = (col_w - 8) / FONT_WIDTH;
            if ((int)strlen(cell) > max_chars && max_chars > 2) {
                cell[max_chars - 1] = '.';
                cell[max_chars] = '\0';
            }

            CTEXT(cx + 4, ry + 2, cell, cell_fg, row_bg);
            /* Vertical separator */
            for (int16_t vr = 0; vr < row_h; vr++) CPIX(cx + col_w - 1, ry + vr, 0xFF333344);
            cx += col_w;
        }

        /* Horizontal grid line */
        CHLINE(grid_x, ry + row_h - 1, cw - grid_x - 4, 0xFF222233);
    }

dg_done:
    #undef CPIX
    #undef CHLINE
    #undef CFILL
    #undef CTEXT
}

static void dg_event(window_t *win, gui_event_t *ev) {
    if (ev->type == EVT_CLOSE) {
        wm_destroy_window(win->id);
        return;
    }

    if (ev->type == EVT_KEY_DOWN) {
        int16_t row_h = FONT_HEIGHT + 4;
        int visible_rows = (win->client_h - 28 - row_h - 1 - 24) / row_h;
        switch (ev->key) {
        case KEY_UP:
            if (dg_sel_row > 0) dg_sel_row--;
            if (dg_sel_row < dg_scroll_row) dg_scroll_row = dg_sel_row;
            break;
        case KEY_DOWN:
            if (dg_sel_row < dg_row_count - 1) dg_sel_row++;
            if (dg_sel_row >= dg_scroll_row + visible_rows)
                dg_scroll_row = dg_sel_row - visible_rows + 1;
            break;
        case KEY_PGUP:
            dg_sel_row -= visible_rows;
            if (dg_sel_row < 0) dg_sel_row = 0;
            dg_scroll_row = dg_sel_row;
            break;
        case KEY_PGDN:
            dg_sel_row += visible_rows;
            if (dg_sel_row >= dg_row_count) dg_sel_row = dg_row_count - 1;
            if (dg_sel_row >= dg_scroll_row + visible_rows)
                dg_scroll_row = dg_sel_row - visible_rows + 1;
            break;
        }
        return;
    }

    /* Mouse click in grid area: select row */
    if (ev->type == EVT_MOUSE_DOWN && ev->mouse_x > DG_TABLE_LIST_W) {
        int16_t data_y = 28 + FONT_HEIGHT + 4 + 1;
        int16_t row_h = FONT_HEIGHT + 4;
        if (ev->mouse_y >= data_y) {
            int row = (ev->mouse_y - data_y) / row_h + dg_scroll_row;
            if (row >= 0 && row < dg_row_count) dg_sel_row = row;
            return;
        }
    }

    widget_dispatch(win, ev);
}

static void open_data_grid(void) {
    dg_row_count = 0;
    dg_col_count = 0;
    dg_sel_row = -1;
    dg_scroll_row = 0;
    dg_table_name[0] = '\0';

    window_t *win = wm_create_window("Data Grid", 40, 30, DG_WIN_W, DG_WIN_H,
                                      dg_event, dg_paint);
    if (!win) return;

    widget_t *rbtn = widget_create_button(4, 2, 72, 22, "Refresh", dg_refresh);
    widget_add(win, rbtn);

    dg_status_lbl = widget_create_label(80, 6, "Select a table", 0xFF808080, 0xFF1A1A2E);
    widget_add(win, dg_status_lbl);

    dg_table_list = widget_create_listview(4, 28, DG_TABLE_LIST_W - 8, win->client_h - 34);
    dg_table_list->lv_items = dg_tl_data;
    dg_table_list->on_select = dg_table_selected;
    widget_add(win, dg_table_list);

    /* Populate table list */
    uint32_t tc = db_get_table_count();
    for (uint32_t i = 0; i < tc; i++) {
        table_schema_t *s = db_get_schema_by_id(i);
        if (s) listview_add_item(dg_table_list, s->name);
    }
}

/* ---- Object Inspector / Hex Viewer ---- */
#define OI_WIN_W   620
#define OI_WIN_H   420
#define OI_LIST_W  160

static char     oi_obj_name[MAX_STR_LEN + 1];
static char     oi_obj_type[MAX_STR_LEN + 1];
static char     oi_obj_data[MAX_STR_LEN + 1];
static uint64_t oi_obj_id;
static uint64_t oi_obj_owner;
static uint64_t oi_obj_created;
static int      oi_hex_scroll;
static bool     oi_has_selection;
static char     oi_ol_data[LISTVIEW_MAX_ITEMS][LISTVIEW_ITEM_MAX];
static widget_t *oi_obj_list;
static widget_t *oi_filter_box;

static void oi_refresh(widget_t *w, window_t *win) {
    (void)w; (void)win;
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    const char *filter = textbox_get_text(oi_filter_box);
    size_t flen = strlen(filter);

    query_result_t *result = db_execute("SELECT * FROM ObjectTable", pid);
    listview_clear(oi_obj_list);

    if (result && result->row_count > 0) {
        for (uint32_t i = 0; i < result->row_count; i++) {
            const char *name = result->rows[i].fields[1].str_val.data;
            const char *type = result->rows[i].fields[2].str_val.data;
            if (flen > 0 &&
                strncasecmp(name, filter, flen) != 0 &&
                strncasecmp(type, filter, flen) != 0)
                continue;

            char item[LISTVIEW_ITEM_MAX];
            snprintf(item, sizeof(item), "[%s] %s", type, name);
            listview_add_item(oi_obj_list, item);
        }
    }
    if (result) db_result_free(result);
}

static void oi_filter_btn(widget_t *w, window_t *win) {
    oi_refresh(w, win);
}

static void oi_on_select(widget_t *w, window_t *win, int16_t index) {
    (void)win;
    if (index < 0 || index >= w->lv_count) return;

    /* Parse name from "[type] name" format */
    const char *item = w->lv_items[index];
    const char *bracket = strchr(item, ']');
    if (!bracket) return;
    const char *name = bracket + 2; /* skip "] " */

    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    char sql[512];
    snprintf(sql, sizeof(sql), "SELECT * FROM ObjectTable WHERE name = '%s'", name);
    query_result_t *result = db_execute(sql, pid);

    if (result && result->row_count > 0) {
        record_t *row = &result->rows[0];
        oi_obj_id = row->fields[0].u64_val;
        strncpy(oi_obj_name, row->fields[1].str_val.data, MAX_STR_LEN);
        strncpy(oi_obj_type, row->fields[2].str_val.data, MAX_STR_LEN);
        strncpy(oi_obj_data, row->fields[3].str_val.data, MAX_STR_LEN);
        oi_obj_owner = row->fields[4].u64_val;
        oi_obj_created = row->fields[6].u64_val;
        oi_hex_scroll = 0;
        oi_has_selection = true;
    }
    if (result) db_result_free(result);
}

static void oi_paint(window_t *win) {
    wm_clear_canvas(win, 0xFF1A1A2E);
    widget_draw_all(win);

    if (!oi_has_selection) return;

    uint16_t cw = win->client_w, ch = win->client_h;

    #define CPIX(px, py, color) do { \
        if ((px) >= 0 && (px) < cw && (py) >= 0 && (py) < ch) \
            win->canvas[(py) * cw + (px)] = (color); \
    } while(0)
    #define CHLINE(x0, y0, width, color) do { \
        for (int16_t _i = 0; _i < (width); _i++) CPIX((x0)+_i, y0, color); \
    } while(0)
    #define CTEXT(tx, ty, str, fg_c, bg_c) do { \
        const char *_s = (str); int16_t _x = (tx); \
        while (*_s) { \
            const uint8_t *_g = font_8x16[(uint8_t)*_s]; \
            for (int _gy = 0; _gy < FONT_HEIGHT; _gy++) { \
                uint8_t _bits = _g[_gy]; \
                for (int _gx = 0; _gx < FONT_WIDTH; _gx++) { \
                    CPIX(_x + _gx, (ty) + _gy, \
                         (_bits & (0x80 >> _gx)) ? (fg_c) : (bg_c)); \
                } \
            } \
            _x += FONT_WIDTH; _s++; \
        } \
    } while(0)

    int16_t px = OI_LIST_W + 8;
    int16_t py = 28;

    /* Metadata section */
    CTEXT(px, py, "METADATA", 0xFFFFCC00, 0xFF1A1A2E);
    py += 22;

    char buf[128];
    snprintf(buf, sizeof(buf), "ID:      %llu", (unsigned long long)oi_obj_id);
    CTEXT(px, py, buf, 0xFF00DDAA, 0xFF1A1A2E);
    py += FONT_HEIGHT + 2;

    snprintf(buf, sizeof(buf), "Name:    %s", oi_obj_name);
    CTEXT(px, py, buf, 0xFF00DDAA, 0xFF1A1A2E);
    py += FONT_HEIGHT + 2;

    snprintf(buf, sizeof(buf), "Type:    %s", oi_obj_type);
    CTEXT(px, py, buf, 0xFF00DDAA, 0xFF1A1A2E);
    py += FONT_HEIGHT + 2;

    snprintf(buf, sizeof(buf), "Owner:   PID %llu", (unsigned long long)oi_obj_owner);
    CTEXT(px, py, buf, 0xFF00DDAA, 0xFF1A1A2E);
    py += FONT_HEIGHT + 2;

    uint64_t secs = oi_obj_created / 1000;
    uint64_t mins = secs / 60; secs %= 60;
    snprintf(buf, sizeof(buf), "Created: %02llu:%02llu",
             (unsigned long long)mins, (unsigned long long)secs);
    CTEXT(px, py, buf, 0xFF00DDAA, 0xFF1A1A2E);
    py += FONT_HEIGHT + 6;

    /* Separator */
    CHLINE(px, py, cw - px - 8, 0xFF555577);
    py += 8;

    /* Hex dump header */
    CTEXT(px, py, "HEX DUMP", 0xFFFFCC00, 0xFF1A1A2E);
    py += 22;

    int data_len = (int)strlen(oi_obj_data);
    int bytes_per_row = 8;
    int total_rows = (data_len + bytes_per_row - 1) / bytes_per_row;
    if (total_rows == 0) total_rows = 1;
    int visible_rows = (ch - py - 4) / FONT_HEIGHT;

    for (int r = 0; r < visible_rows && (r + oi_hex_scroll) < total_rows; r++) {
        int offset = (r + oi_hex_scroll) * bytes_per_row;
        int16_t ry = py + r * FONT_HEIGHT;

        /* Offset */
        snprintf(buf, sizeof(buf), "%04X ", offset);
        CTEXT(px, ry, buf, 0xFFFFCC00, 0xFF1A1A2E);

        /* Hex bytes */
        int16_t hx = px + 5 * FONT_WIDTH + 4;
        char hex_str[4];
        for (int b = 0; b < bytes_per_row; b++) {
            if (offset + b < data_len) {
                snprintf(hex_str, sizeof(hex_str), "%02X ",
                         (uint8_t)oi_obj_data[offset + b]);
                CTEXT(hx, ry, hex_str, 0xFF00DDAA, 0xFF1A1A2E);
            } else {
                CTEXT(hx, ry, "   ", 0xFF333344, 0xFF1A1A2E);
            }
            hx += 3 * FONT_WIDTH;
            if (b == 3) hx += FONT_WIDTH; /* gap at midpoint */
        }

        /* ASCII representation */
        int16_t ax = hx + FONT_WIDTH;
        CTEXT(ax, ry, "|", 0xFF555577, 0xFF1A1A2E);
        ax += FONT_WIDTH;
        for (int b = 0; b < bytes_per_row && offset + b < data_len; b++) {
            char c = oi_obj_data[offset + b];
            char asc[2] = { (c >= 0x20 && c < 0x7F) ? c : '.', '\0' };
            uint32_t afg = (c >= 0x20 && c < 0x7F) ? 0xFFCCCCCC : 0xFF555555;
            CTEXT(ax, ry, asc, afg, 0xFF1A1A2E);
            ax += FONT_WIDTH;
        }
        CTEXT(ax, ry, "|", 0xFF555577, 0xFF1A1A2E);
    }

    if (data_len == 0) {
        CTEXT(px, py, "(empty)", 0xFF666666, 0xFF1A1A2E);
    }

    #undef CPIX
    #undef CHLINE
    #undef CTEXT
}

static void oi_event(window_t *win, gui_event_t *ev) {
    if (ev->type == EVT_CLOSE) {
        wm_destroy_window(win->id);
        return;
    }

    if (ev->type == EVT_KEY_DOWN) {
        int data_len = (int)strlen(oi_obj_data);
        int bytes_per_row = 8;
        int total_rows = (data_len + bytes_per_row - 1) / bytes_per_row;
        int visible_rows = (win->client_h - 200) / FONT_HEIGHT;

        if (ev->key == KEY_DOWN || ev->key == KEY_PGDN) {
            int step = (ev->key == KEY_PGDN) ? visible_rows : 1;
            oi_hex_scroll += step;
            if (oi_hex_scroll > total_rows - visible_rows)
                oi_hex_scroll = total_rows - visible_rows;
            if (oi_hex_scroll < 0) oi_hex_scroll = 0;
            return;
        }
        if (ev->key == KEY_UP || ev->key == KEY_PGUP) {
            int step = (ev->key == KEY_PGUP) ? visible_rows : 1;
            oi_hex_scroll -= step;
            if (oi_hex_scroll < 0) oi_hex_scroll = 0;
            return;
        }
    }

    widget_dispatch(win, ev);
}

static void open_object_inspector(void) {
    oi_has_selection = false;
    oi_hex_scroll = 0;
    oi_obj_name[0] = '\0';

    window_t *win = wm_create_window("Object Inspector", 80, 50, OI_WIN_W, OI_WIN_H,
                                      oi_event, oi_paint);
    if (!win) return;

    widget_t *rbtn = widget_create_button(4, 2, 72, 22, "Refresh", oi_refresh);
    widget_add(win, rbtn);

    oi_filter_box = widget_create_textbox(80, 2, 180, 22);
    widget_add(win, oi_filter_box);

    widget_t *fbtn = widget_create_button(264, 2, 60, 22, "Filter", oi_filter_btn);
    widget_add(win, fbtn);

    oi_obj_list = widget_create_listview(4, 28, OI_LIST_W - 8, win->client_h - 34);
    oi_obj_list->lv_items = oi_ol_data;
    oi_obj_list->on_select = oi_on_select;
    widget_add(win, oi_obj_list);

    oi_refresh(NULL, win);
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
                            if (wlist[i]->minimized) {
                                /* Restore minimized window */
                                wlist[i]->visible = true;
                                wlist[i]->minimized = false;
                            }
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
                        case 0:  open_query_console(); break;
                        case 1:  open_table_browser(); break;
                        case 2:  open_data_grid(); break;
                        case 4:  open_vaultpad(); break;
                        case 5:  open_calculator(); break;
                        case 6:  open_object_inspector(); break;
                        case 8:  open_security_dashboard(); break;
                        case 9:  open_audit_viewer(); break;
                        case 10: open_cap_manager(); break;
                        case 11: open_object_manager(); break;
                        case 13: open_process_manager(); break;
                        case 14: open_system_status(); break;
                        case 16: gui_running = false; break;
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
