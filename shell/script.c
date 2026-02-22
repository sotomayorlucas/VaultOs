#include "script.h"
#include "friendly.h"
#include "line_editor.h"
#include "display.h"
#include "../kernel/db/database.h"
#include "../kernel/proc/process.h"
#include "../kernel/arch/x86_64/pit.h"
#include "../kernel/arch/x86_64/cpu.h"
#include "../kernel/lib/printf.h"
#include "../kernel/lib/string.h"
#include "../kernel/drivers/framebuffer.h"

int script_save(const char *name) {
    char sql[FRIENDLY_SQL_MAX];
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    /* Delete existing script with this name */
    snprintf(sql, sizeof(sql),
        "DELETE FROM ObjectTable WHERE name = '%s' AND type = 'script'", name);
    query_result_t *r = db_execute(sql, pid);
    db_result_free(r);

    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  Script '%s' -- enter commands, type END to finish:\n", name);
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);

    char line_buf[LINE_MAX];
    uint8_t fkey = 0;
    int line_num = 0;

    while (line_num < SCRIPT_MAX_LINES) {
        fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
        kprintf("  %2d> ", line_num + 1);
        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);

        char *line = line_read(line_buf, sizeof(line_buf), &fkey);
        if (fkey) continue;
        if (!line || strlen(line) == 0) continue;

        if (strcasecmp(line, "end") == 0) break;

        /* Insert this line */
        snprintf(sql, sizeof(sql),
            "INSERT INTO ObjectTable (name, type, data) VALUES ('%s', 'script', '%s')",
            name, line);
        r = db_execute(sql, pid);
        db_result_free(r);
        line_num++;
    }

    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  Script '%s' saved (%d lines)\n", name, line_num);
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    return line_num;
}

int script_run(const char *name) {
    char sql[FRIENDLY_SQL_MAX];
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    snprintf(sql, sizeof(sql),
        "SELECT * FROM ObjectTable WHERE name = '%s' AND type = 'script'", name);
    query_result_t *result = db_execute(sql, pid);

    if (!result || result->row_count == 0) {
        if (result) db_result_free(result);
        fb_set_color(COLOR_RED, COLOR_VOS_BG);
        kprintf("  Script '%s' not found\n", name);
        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
        return -1;
    }

    /* Lines are already in obj_id order from btree_scan */
    int executed = 0;
    for (uint32_t i = 0; i < result->row_count; i++) {
        /* data is column index 3 in ObjectTable schema */
        const char *cmd = result->rows[i].fields[3].str_val.data;

        fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
        kprintf("  [%d] ", executed + 1);
        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
        kprintf("%s\n", cmd);

        /* Translate friendly → SQL, or pass through as raw SQL */
        char sql_buf[FRIENDLY_SQL_MAX];
        const char *to_exec;
        if (friendly_translate(cmd, sql_buf, sizeof(sql_buf))) {
            to_exec = sql_buf;
        } else {
            to_exec = cmd;
        }

        query_result_t *line_result = db_execute(to_exec, pid);
        display_result(line_result);
        db_result_free(line_result);
        executed++;
    }

    db_result_free(result);

    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  Script '%s' complete (%d lines)\n", name, executed);
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    return executed;
}

void script_list(void) {
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    query_result_t *result = db_execute(
        "SELECT * FROM ObjectTable WHERE type = 'script'", pid);

    if (!result || result->row_count == 0) {
        kprintf("  No scripts found\n");
        if (result) db_result_free(result);
        return;
    }

    /* Collect unique names */
    char names[32][MAX_STR_LEN + 1];
    int counts[32];
    int name_count = 0;

    for (uint32_t i = 0; i < result->row_count; i++) {
        const char *n = result->rows[i].fields[1].str_val.data; /* name column */
        bool found = false;
        for (int j = 0; j < name_count; j++) {
            if (strcmp(names[j], n) == 0) {
                counts[j]++;
                found = true;
                break;
            }
        }
        if (!found && name_count < 32) {
            strncpy(names[name_count], n, MAX_STR_LEN);
            names[name_count][MAX_STR_LEN] = '\0';
            counts[name_count] = 1;
            name_count++;
        }
    }

    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("\n  Scripts:\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    for (int i = 0; i < name_count; i++) {
        kprintf("    %-20s (%d lines)\n", names[i], counts[i]);
    }
    kprintf("\n");

    db_result_free(result);
}

void script_cat(const char *name) {
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    /* Try as script first */
    char sql[FRIENDLY_SQL_MAX];
    snprintf(sql, sizeof(sql),
        "SELECT * FROM ObjectTable WHERE name = '%s' AND type = 'script'", name);
    query_result_t *result = db_execute(sql, pid);

    if (result && result->row_count > 0) {
        /* Script: show numbered lines */
        fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
        kprintf("\n  Script: %s (%u lines)\n", name, result->row_count);
        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
        for (uint32_t i = 0; i < result->row_count; i++) {
            fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
            kprintf("  %2u ", i + 1);
            fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
            kprintf("%s\n", result->rows[i].fields[3].str_val.data);
        }
        kprintf("\n");
        db_result_free(result);
        return;
    }
    if (result) db_result_free(result);

    /* Not a script — try as generic object */
    snprintf(sql, sizeof(sql),
        "SELECT * FROM ObjectTable WHERE name = '%s'", name);
    result = db_execute(sql, pid);

    if (!result || result->row_count == 0) {
        kprintf("  Object '%s' not found\n", name);
        if (result) db_result_free(result);
        return;
    }

    for (uint32_t i = 0; i < result->row_count; i++) {
        const char *type = result->rows[i].fields[2].str_val.data;
        const char *data = result->rows[i].fields[3].str_val.data;
        fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
        kprintf("  [%s] ", type);
        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
        kprintf("%s\n", data);
    }

    db_result_free(result);
}

void script_process_entry(void) {
    process_t *self = process_get_current();
    if (!self) {
        for (;;) hlt();
    }

    /* Extract script name from process name: "script:<name>" */
    const char *name = self->name + 7; /* skip "script:" */
    script_run(name);

    process_exit(self, 0);
    for (;;) hlt();
}
