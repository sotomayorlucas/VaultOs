#include "shell.h"
#include "line_editor.h"
#include "display.h"
#include "tui.h"
#include "history.h"
#include "../kernel/db/database.h"
#include "../kernel/drivers/framebuffer.h"
#include "../kernel/drivers/keyboard.h"
#include "../kernel/lib/printf.h"
#include "../kernel/lib/string.h"
#include "../kernel/mm/heap.h"
#include "../kernel/arch/x86_64/pit.h"
#include "../kernel/proc/process.h"
#include "../gui/desktop.h"

static void handle_query(const char *line) {
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    query_result_t *result = db_execute(line, pid);
    display_result(result);
    db_result_free(result);
}

static void cmd_help(void) {
    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  VaultOS Query Commands\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  SHOW TABLES                           List all tables\n");
    kprintf("  DESCRIBE <table>                      Show table schema\n");
    kprintf("  SELECT * FROM <table> [WHERE ...]     Query data\n");
    kprintf("  INSERT INTO <table> (cols) VALUES ...  Insert data\n");
    kprintf("  DELETE FROM <table> [WHERE ...]        Delete data\n");
    kprintf("  UPDATE <table> SET col=val [WHERE ...] Update data\n");
    kprintf("  GRANT <rights> ON <obj> TO <pid>       Grant capability\n");
    kprintf("  REVOKE <cap_id>                        Revoke capability\n");
    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  Editing\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  Left/Right   Move cursor         Up/Down   Command history\n");
    kprintf("  Home/End     Jump to start/end   Tab       Auto-complete\n");
    kprintf("  Escape       Clear line           Ctrl+L   Clear screen\n");
    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  GUI Mode\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  GUI                                   Launch graphical desktop\n\n");
}

static void cmd_status(void) {
    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  VaultOS System Status\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    uint64_t ms = pit_get_uptime_ms();
    kprintf("  Uptime:    %llu.%llus\n", ms / 1000, (ms % 1000) / 100);
    kprintf("  Heap used: %llu bytes\n", (uint64_t)heap_used());
    kprintf("  Heap free: %llu bytes\n", (uint64_t)heap_free());
    kprintf("  Tables:    %u\n\n", db_get_table_count());
}

static void clear_content(void) {
    const tui_layout_t *lay = tui_get_layout();
    fb_clear_rows(lay->content_top, lay->content_bottom);
    fb_set_cursor(0, lay->content_top);
}

static void print_banner(void) {
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("\n");
    kprintf("  \\    /  /\\  | | |  |_ /\\  /__ \n");
    kprintf("   \\  /  /--\\ | | |  |  /--\\ __/ \n");
    kprintf("    \\/                            \n");
    kprintf("\n");
    fb_set_color(COLOR_CYAN, COLOR_VOS_BG);
    kprintf("  \"Everything is a database. All data is confidential.\"\n");
    fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
    kprintf("  Type HELP or press F1 for commands. Tab for auto-complete.\n\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
}

static void print_prompt(void) {
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("vaultos");
    fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
    kprintf("> ");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
}

void shell_main(void) {
    char line_buf[LINE_MAX];
    uint8_t fkey = 0;

    /* Initialize TUI subsystems */
    history_init();
    line_editor_init();
    tui_init();

    print_banner();

    while (true) {
        print_prompt();
        char *line = line_read(line_buf, sizeof(line_buf), &fkey);

        /* Handle F-key shortcuts */
        if (fkey) {
            switch (fkey) {
            case KEY_F1:
                cmd_help();
                break;
            case KEY_F2:
                handle_query("SHOW TABLES");
                break;
            case KEY_F3:
                cmd_status();
                break;
            case KEY_F5:
                clear_content();
                break;
            default:
                break;
            }
            continue;
        }

        if (!line || strlen(line) == 0) continue;

        /* Built-in commands */
        if (strcasecmp(line, "clear") == 0) {
            clear_content();
            continue;
        }
        if (strcasecmp(line, "help") == 0) {
            cmd_help();
            continue;
        }
        if (strcasecmp(line, "status") == 0) {
            cmd_status();
            continue;
        }
        if (strcasecmp(line, "gui") == 0) {
            gui_main();
            /* Re-init TUI after returning from GUI */
            tui_init();
            print_banner();
            continue;
        }

        /* Execute as SQL query */
        handle_query(line);
    }
}
