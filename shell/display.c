#include "display.h"
#include "tui.h"
#include "../kernel/lib/printf.h"
#include "../kernel/lib/string.h"
#include "../kernel/drivers/framebuffer.h"
#include "../kernel/drivers/keyboard.h"

#define MAX_COL_WIDTH 30

/* Box-drawing characters (CP437) */
#define BOX_H     '\xC4'  /* horizontal line */
#define BOX_V     '\xB3'  /* vertical line */
#define BOX_TL    '\xDA'  /* top-left corner */
#define BOX_TR    '\xBF'  /* top-right corner */
#define BOX_BL    '\xC0'  /* bottom-left corner */
#define BOX_BR    '\xD9'  /* bottom-right corner */
#define BOX_LT    '\xC3'  /* left T-junction */
#define BOX_RT    '\xB4'  /* right T-junction */
#define BOX_TT    '\xC2'  /* top T-junction */
#define BOX_BT    '\xC1'  /* bottom T-junction */
#define BOX_CROSS '\xC5'  /* cross */

/* Viewer buffer for interactive paging */
#define VIEWER_MAX_LINES 512
#define VIEWER_LINE_LEN  256

static char viewer_lines[VIEWER_MAX_LINES][VIEWER_LINE_LEN];
static uint32_t viewer_line_count = 0;

static void viewer_add_line(const char *line) {
    if (viewer_line_count >= VIEWER_MAX_LINES) return;
    strncpy(viewer_lines[viewer_line_count], line, VIEWER_LINE_LEN - 1);
    viewer_lines[viewer_line_count][VIEWER_LINE_LEN - 1] = '\0';
    viewer_line_count++;
}

static void field_to_str(const field_value_t *field, char *buf, size_t buf_size) {
    switch (field->type) {
    case COL_U64:
        snprintf(buf, buf_size, "%llu", field->u64_val);
        break;
    case COL_I64:
        snprintf(buf, buf_size, "%lld", field->i64_val);
        break;
    case COL_U32:
        snprintf(buf, buf_size, "%u", field->u32_val);
        break;
    case COL_U8:
        snprintf(buf, buf_size, "%u", field->u8_val);
        break;
    case COL_STR:
        strncpy(buf, field->str_val.data, buf_size - 1);
        buf[buf_size - 1] = '\0';
        break;
    case COL_BOOL:
        strncpy(buf, field->bool_val ? "true" : "false", buf_size - 1);
        break;
    case COL_BLOB:
        snprintf(buf, buf_size, "<blob %u bytes>", field->blob_val.length);
        break;
    default:
        strncpy(buf, "?", buf_size - 1);
        break;
    }
}

/* Build a horizontal line into buffer */
static void build_hline(char *out, size_t out_size, uint32_t *widths,
                          uint32_t col_count, char left, char mid, char right) {
    size_t pos = 0;
    for (uint32_t c = 0; c < col_count; c++) {
        if (pos < out_size - 1) out[pos++] = (c == 0) ? left : mid;
        for (uint32_t w = 0; w < widths[c] + 2 && pos < out_size - 1; w++)
            out[pos++] = BOX_H;
    }
    if (pos < out_size - 1) out[pos++] = right;
    out[pos] = '\0';
}

/* Build a data row into buffer */
static void build_data_row(char *out, size_t out_size, const char fields[][MAX_COL_WIDTH + 1],
                             uint32_t *widths, uint32_t col_count) {
    size_t pos = 0;
    for (uint32_t c = 0; c < col_count; c++) {
        if (pos < out_size - 1) out[pos++] = BOX_V;
        if (pos < out_size - 1) out[pos++] = ' ';
        size_t flen = strlen(fields[c]);
        for (size_t i = 0; i < flen && pos < out_size - 1; i++)
            out[pos++] = fields[c][i];
        int pad = (int)widths[c] - (int)flen;
        for (int i = 0; i < pad && pos < out_size - 1; i++)
            out[pos++] = ' ';
        if (pos < out_size - 1) out[pos++] = ' ';
    }
    if (pos < out_size - 1) out[pos++] = BOX_V;
    out[pos] = '\0';
}

/* Interactive pager for viewing buffered lines */
static void viewer_pager(void) {
    const tui_layout_t *lay = tui_get_layout();
    uint32_t visible = lay->content_rows - 1; /* Reserve 1 row for status */
    if (visible == 0) visible = 1;

    uint32_t offset = 0;
    uint32_t max_offset = (viewer_line_count > visible)
                          ? viewer_line_count - visible : 0;

    while (1) {
        /* Draw visible lines */
        uint32_t row = lay->content_top;
        uint32_t max_cols = lay->total_cols;

        for (uint32_t i = 0; i < visible && row < lay->content_bottom - 1; i++, row++) {
            uint32_t li = offset + i;
            uint32_t col = 0;
            if (li < viewer_line_count) {
                const char *line = viewer_lines[li];
                while (*line && col < max_cols) {
                    fb_draw_cell(col, row, *line, COLOR_VOS_FG, COLOR_VOS_BG);
                    col++;
                    line++;
                }
            }
            /* Clear rest of row */
            while (col < max_cols) {
                fb_draw_cell(col, row, ' ', COLOR_VOS_FG, COLOR_VOS_BG);
                col++;
            }
        }

        /* Draw status bar at bottom of content */
        {
            uint32_t srow = lay->content_bottom - 1;
            char status[128];
            snprintf(status, sizeof(status),
                     " Lines %u-%u of %u (Up/Down/PgUp/PgDn to scroll, Q to exit) ",
                     offset + 1,
                     (offset + visible < viewer_line_count) ? offset + visible : viewer_line_count,
                     viewer_line_count);
            uint32_t col = 0;
            const char *s = status;
            while (*s && col < max_cols) {
                fb_draw_cell(col, srow, *s, COLOR_VOS_BG, COLOR_VOS_HL);
                col++;
                s++;
            }
            while (col < max_cols) {
                fb_draw_cell(col, srow, ' ', COLOR_VOS_BG, COLOR_VOS_HL);
                col++;
            }
        }

        /* Wait for key */
        char key = keyboard_getchar();
        uint8_t k = (uint8_t)key;

        switch (k) {
        case KEY_UP:
            if (offset > 0) offset--;
            break;
        case KEY_DOWN:
            if (offset < max_offset) offset++;
            break;
        case KEY_PGUP:
            offset = (offset >= visible) ? offset - visible : 0;
            break;
        case KEY_PGDN:
            offset = (offset + visible <= max_offset) ? offset + visible : max_offset;
            break;
        case KEY_HOME:
            offset = 0;
            break;
        case KEY_END:
            offset = max_offset;
            break;
        case 'q': case 'Q': case 0x1B: /* Q or Escape */
            /* Clear content area and return */
            fb_clear_rows(lay->content_top, lay->content_bottom);
            fb_set_cursor(0, lay->content_top);
            return;
        }
    }
}

void display_result(query_result_t *result) {
    if (!result) {
        kprintf("Error: NULL result\n");
        return;
    }

    /* Error message */
    if (result->error_code != 0 && result->row_count == 0) {
        fb_set_color(COLOR_RED, COLOR_VOS_BG);
        kprintf("Error: %s\n", result->error_msg);
        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
        return;
    }

    /* Status message without rows */
    if (result->row_count == 0 && result->error_msg[0]) {
        fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
        kprintf("%s\n", result->error_msg);
        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
        return;
    }

    if (result->row_count == 0) {
        kprintf("0 rows returned\n");
        return;
    }

    table_schema_t *schema = result->schema;
    if (!schema) {
        kprintf("(%u rows, no schema for display)\n", result->row_count);
        return;
    }

    uint32_t col_count = schema->column_count;
    if (col_count > MAX_COLUMNS) col_count = MAX_COLUMNS;

    /* Calculate column widths */
    uint32_t widths[MAX_COLUMNS];
    for (uint32_t c = 0; c < col_count; c++) {
        widths[c] = (uint32_t)strlen(schema->columns[c].name);
        if (widths[c] < 4) widths[c] = 4;
    }
    for (uint32_t r = 0; r < result->row_count; r++) {
        for (uint32_t c = 0; c < col_count; c++) {
            char buf[MAX_COL_WIDTH + 1];
            field_to_str(&result->rows[r].fields[c], buf, sizeof(buf));
            uint32_t w = (uint32_t)strlen(buf);
            if (w > widths[c]) widths[c] = w;
            if (widths[c] > MAX_COL_WIDTH) widths[c] = MAX_COL_WIDTH;
        }
    }

    /* Total lines: top border + header + mid border + rows + bottom border + summary */
    uint32_t total_lines = 3 + result->row_count + 1 + 1;
    const tui_layout_t *lay = tui_get_layout();
    bool use_pager = (total_lines > lay->content_rows);

    if (use_pager) {
        /* Build all lines into viewer buffer */
        viewer_line_count = 0;
        char line[VIEWER_LINE_LEN];

        build_hline(line, sizeof(line), widths, col_count, BOX_TL, BOX_TT, BOX_TR);
        viewer_add_line(line);

        /* Header row */
        char fields[MAX_COLUMNS][MAX_COL_WIDTH + 1];
        for (uint32_t c = 0; c < col_count; c++) {
            strncpy(fields[c], schema->columns[c].name, MAX_COL_WIDTH);
            fields[c][MAX_COL_WIDTH] = '\0';
        }
        build_data_row(line, sizeof(line), fields, widths, col_count);
        viewer_add_line(line);

        build_hline(line, sizeof(line), widths, col_count, BOX_LT, BOX_CROSS, BOX_RT);
        viewer_add_line(line);

        /* Data rows */
        for (uint32_t r = 0; r < result->row_count; r++) {
            for (uint32_t c = 0; c < col_count; c++)
                field_to_str(&result->rows[r].fields[c], fields[c], sizeof(fields[c]));
            build_data_row(line, sizeof(line), fields, widths, col_count);
            viewer_add_line(line);
        }

        build_hline(line, sizeof(line), widths, col_count, BOX_BL, BOX_BT, BOX_BR);
        viewer_add_line(line);

        snprintf(line, sizeof(line), "%u row(s) returned", result->row_count);
        viewer_add_line(line);

        viewer_pager();
    } else {
        /* Direct output with box-drawing */
        char line[VIEWER_LINE_LEN];

        fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
        build_hline(line, sizeof(line), widths, col_count, BOX_TL, BOX_TT, BOX_TR);
        kprintf("%s\n", line);

        /* Header */
        char fields[MAX_COLUMNS][MAX_COL_WIDTH + 1];
        for (uint32_t c = 0; c < col_count; c++) {
            strncpy(fields[c], schema->columns[c].name, MAX_COL_WIDTH);
            fields[c][MAX_COL_WIDTH] = '\0';
        }
        build_data_row(line, sizeof(line), fields, widths, col_count);
        kprintf("%s\n", line);

        build_hline(line, sizeof(line), widths, col_count, BOX_LT, BOX_CROSS, BOX_RT);
        kprintf("%s\n", line);
        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);

        /* Data rows */
        for (uint32_t r = 0; r < result->row_count; r++) {
            for (uint32_t c = 0; c < col_count; c++)
                field_to_str(&result->rows[r].fields[c], fields[c], sizeof(fields[c]));
            build_data_row(line, sizeof(line), fields, widths, col_count);
            kprintf("%s\n", line);
        }

        build_hline(line, sizeof(line), widths, col_count, BOX_BL, BOX_BT, BOX_BR);
        kprintf("%s\n", line);
        kprintf("%u row(s) returned\n", result->row_count);
    }
}
