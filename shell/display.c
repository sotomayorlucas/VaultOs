#include "display.h"
#include "../kernel/lib/printf.h"
#include "../kernel/lib/string.h"
#include "../kernel/drivers/framebuffer.h"

#define MAX_COL_WIDTH 30

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

static void print_horizontal_line(uint32_t *widths, uint32_t col_count) {
    for (uint32_t c = 0; c < col_count; c++) {
        kprintf("+");
        for (uint32_t w = 0; w < widths[c] + 2; w++) kprintf("-");
    }
    kprintf("+\n");
}

static void print_padded(const char *str, uint32_t width) {
    kprintf(" %s", str);
    int pad = (int)width - (int)strlen(str);
    for (int i = 0; i < pad; i++) kprintf(" ");
    kprintf(" ");
}

void display_result(query_result_t *result) {
    if (!result) {
        kprintf("Error: NULL result\n");
        return;
    }

    /* If there's an error message and no rows, show message */
    if (result->error_code != 0 && result->row_count == 0) {
        fb_set_color(COLOR_RED, COLOR_VOS_BG);
        kprintf("Error: %s\n", result->error_msg);
        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
        return;
    }

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

    /* Check data widths */
    for (uint32_t r = 0; r < result->row_count; r++) {
        for (uint32_t c = 0; c < col_count; c++) {
            char buf[MAX_COL_WIDTH + 1];
            field_to_str(&result->rows[r].fields[c], buf, sizeof(buf));
            uint32_t w = (uint32_t)strlen(buf);
            if (w > widths[c]) widths[c] = w;
            if (widths[c] > MAX_COL_WIDTH) widths[c] = MAX_COL_WIDTH;
        }
    }

    /* Print header */
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    print_horizontal_line(widths, col_count);
    for (uint32_t c = 0; c < col_count; c++) {
        kprintf("|");
        print_padded(schema->columns[c].name, widths[c]);
    }
    kprintf("|\n");
    print_horizontal_line(widths, col_count);
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);

    /* Print rows */
    for (uint32_t r = 0; r < result->row_count; r++) {
        for (uint32_t c = 0; c < col_count; c++) {
            char buf[MAX_COL_WIDTH + 1];
            field_to_str(&result->rows[r].fields[c], buf, sizeof(buf));
            kprintf("|");
            print_padded(buf, widths[c]);
        }
        kprintf("|\n");
    }

    print_horizontal_line(widths, col_count);
    kprintf("%u row(s) returned\n", result->row_count);
}
