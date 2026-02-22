#include "shell.h"
#include "line_editor.h"
#include "display.h"
#include "tui.h"
#include "history.h"
#include "friendly.h"
#include "script.h"
#include "../kernel/db/database.h"
#include "../kernel/drivers/framebuffer.h"
#include "../kernel/drivers/keyboard.h"
#include "../kernel/lib/printf.h"
#include "../kernel/lib/string.h"
#include "../kernel/mm/heap.h"
#include "../kernel/mm/pmm.h"
#include "../kernel/arch/x86_64/pit.h"
#include "../kernel/arch/x86_64/cpu.h"
#include "../kernel/proc/process.h"
#include "../kernel/proc/scheduler.h"
#include "../kernel/crypto/random.h"
#include "../kernel/cap/cap_table.h"
#include "../gui/desktop.h"

static void handle_query(const char *line) {
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    query_result_t *result = db_execute(line, pid);
    display_result(result);
    db_result_free(result);
}

/* ---- Monitor process: logs system stats to AuditTable every 5s ---- */
void monitor_entry(void) {
    process_t *self = process_get_current();
    if (!self) { for (;;) hlt(); }

    char sql[FRIENDLY_SQL_MAX];
    while (true) {
        uint64_t ms = pit_get_uptime_ms();
        uint64_t used = (uint64_t)heap_used();
        uint64_t free_mem = (uint64_t)heap_free();

        snprintf(sql, sizeof(sql),
            "INSERT INTO AuditTable (event, actor_pid, detail) "
            "VALUES ('monitor', %llu, 'up=%llus heap=%llu/%llu')",
            (unsigned long long)self->pid,
            (unsigned long long)(ms / 1000),
            (unsigned long long)used,
            (unsigned long long)(used + free_mem));
        db_execute(sql, self->pid);

        /* Sleep ~5 seconds (yield in a loop) */
        uint64_t target = pit_get_uptime_ms() + 5000;
        while (pit_get_uptime_ms() < target) {
            hlt();
        }
    }
}

/* ---- Built-in program registry ---- */
typedef struct {
    const char *name;
    void (*entry)(void);
    const char *description;
} builtin_program_t;

static const builtin_program_t builtin_programs[] = {
    { "monitor", monitor_entry,        "System stats logger" },
    { NULL,      NULL,                  NULL                  }
};

static void cmd_help(void) {
    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  Quick Commands\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  tables                          List all tables\n");
    kprintf("  show <table>                    View all rows\n");
    kprintf("  info <table>                    Show table structure\n");
    kprintf("  find <table> col=value          Search rows\n");
    kprintf("  add <table> col=val col=val     Insert a row\n");
    kprintf("  del <table> col=value           Delete matching rows\n");
    kprintf("  set <table> col=val where k=v   Update rows\n");
    kprintf("  count <table>                   Count rows\n");
    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  Objects\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  create <type> <name> [content]  Create an object\n");
    kprintf("  open <name>                     View an object\n");
    kprintf("  list [type]                     List objects\n");
    kprintf("  rm <name>                       Delete an object\n");
    kprintf("  cat <name>                      Display object/script\n");
    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  Scripts\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  save <name>                     Record a script\n");
    kprintf("  run <name>                      Execute a script\n");
    kprintf("  scripts                         List all scripts\n");
    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  Processes\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  ps                              List processes\n");
    kprintf("  spawn <program>                 Launch a process\n");
    kprintf("  kill <pid>                      Terminate a process\n");
    kprintf("  msg <pid> <text>                Send a message\n");
    kprintf("  inbox                           View received messages\n");
    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  Table Aliases\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  procs=ProcessTable  caps=CapabilityTable  objects=ObjectTable\n");
    kprintf("  msgs=MessageTable   audit=AuditTable      sys=SystemTable\n");
    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  SQL Commands\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
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

static void cmd_security(void) {
    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  VaultOS Security Status\n");
    fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
    kprintf("  ");
    for (int i = 0; i < 40; i++) kprintf("\xC4");
    kprintf("\n");

    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  Encryption:     ");
    fb_set_color(COLOR_GREEN, COLOR_VOS_BG);
    kprintf("AES-128-CBC (all tables)\n");

    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  Key Derivation: ");
    fb_set_color(COLOR_GREEN, COLOR_VOS_BG);
    kprintf("HMAC-SHA256 (domain-separated)\n");

    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  RNG Source:     ");
    if (random_hw_available()) {
        fb_set_color(COLOR_GREEN, COLOR_VOS_BG);
        kprintf("Hardware (RDRAND)\n");
    } else {
        fb_set_color(COLOR_YELLOW, COLOR_VOS_BG);
        kprintf("Software (xorshift128+ / RDTSC)\n");
    }

    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  Capabilities:   ");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("%llu active tokens\n", (unsigned long long)cap_table_count());

    /* Count audit events */
    uint64_t my_pid = 0;
    process_t *cur = process_get_current();
    if (cur) my_pid = cur->pid;
    query_result_t *ar = db_execute("SELECT * FROM AuditTable", my_pid);
    uint32_t audit_count = ar ? ar->row_count : 0;
    db_result_free(ar);

    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  Audit Events:   ");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("%u logged\n", audit_count);

    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  Table Encryption\n");
    fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
    kprintf("  ");
    for (int i = 0; i < 40; i++) kprintf("\xC4");
    kprintf("\n");

    uint32_t tc = db_get_table_count();
    for (uint32_t i = 0; i < tc; i++) {
        table_schema_t *s = db_get_schema_by_id(i);
        if (!s) continue;
        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
        kprintf("    %-20s ", s->name);
        fb_set_color(COLOR_GREEN, COLOR_VOS_BG);
        kprintf("SEALED\n");
    }
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("\n");
}

static void cmd_audit(int limit) {
    if (limit <= 0) limit = 20;

    uint64_t my_pid = 0;
    process_t *cur = process_get_current();
    if (cur) my_pid = cur->pid;

    query_result_t *ar = db_execute("SELECT * FROM AuditTable", my_pid);
    if (!ar || ar->row_count == 0) {
        kprintf("  No audit events recorded.\n");
        if (ar) db_result_free(ar);
        return;
    }

    kprintf("\n");
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("  Audit Log");
    fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
    kprintf(" (last %u of %u events)\n",
            (uint32_t)limit > ar->row_count ? ar->row_count : (uint32_t)limit,
            ar->row_count);
    kprintf("  ");
    for (int i = 0; i < 50; i++) kprintf("\xC4");
    kprintf("\n");

    /* Show the last N entries */
    uint32_t start = (ar->row_count > (uint32_t)limit)
                     ? ar->row_count - (uint32_t)limit : 0;

    for (uint32_t r = start; r < ar->row_count; r++) {
        /* fields: 0=audit_id, 1=timestamp, 2=pid, 3=action, 4=target_id, 5=result */
        uint64_t ts  = ar->rows[r].fields[1].u64_val;
        uint64_t pid = ar->rows[r].fields[2].u64_val;
        const char *action = ar->rows[r].fields[3].str_val.data;
        const char *result = ar->rows[r].fields[5].str_val.data;

        /* Format timestamp as MM:SS */
        uint64_t secs = ts / 1000;
        uint64_t mins = secs / 60;
        secs %= 60;

        /* Color-code by action type */
        uint32_t action_color = COLOR_VOS_FG;
        if (strncasecmp(action, "GRANT", 5) == 0 ||
            strncasecmp(action, "REVOKE", 6) == 0)
            action_color = COLOR_VOS_HL;   /* gold for security */
        else if (strncasecmp(action, "monitor", 7) == 0)
            action_color = COLOR_GRAY;     /* dim for monitoring */
        else if (strncasecmp(action, "ERROR", 5) == 0 ||
                 strncasecmp(action, "FAIL", 4) == 0)
            action_color = COLOR_RED;      /* red for errors */
        else if (strncasecmp(action, "INSERT", 6) == 0 ||
                 strncasecmp(action, "DELETE", 6) == 0 ||
                 strncasecmp(action, "UPDATE", 6) == 0)
            action_color = COLOR_CYAN;     /* teal for data ops */

        fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
        kprintf("  [%02llu:%02llu] ", (unsigned long long)mins,
                (unsigned long long)secs);
        fb_set_color(action_color, COLOR_VOS_BG);
        kprintf("%-10s ", action);
        fb_set_color(COLOR_CYAN, COLOR_VOS_BG);
        kprintf("PID:%llu ", (unsigned long long)pid);
        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
        kprintf("%s\n", result);
    }

    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("\n");
    db_result_free(ar);
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
    kprintf("  Type HELP or press F1 for commands. Tab for auto-complete.\n");
    kprintf("\n  ");
    for (int i = 0; i < 48; i++) kprintf("\xC4");
    kprintf("\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("  %u tables encrypted", db_get_table_count());
    fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
    kprintf(" \xB3 ");
    fb_set_color(random_hw_available() ? COLOR_GREEN : COLOR_YELLOW, COLOR_VOS_BG);
    kprintf("RNG: %s", random_hw_available() ? "Hardware" : "Software");
    fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
    kprintf(" \xB3 ");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
    kprintf("Caps: %llu\n\n", (unsigned long long)cap_table_count());
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
}

static void print_prompt(void) {
    uint64_t pid = 0;
    process_t *cur = process_get_current();
    if (cur) pid = cur->pid;

    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("vaultos");
    fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
    kprintf("[");
    fb_set_color(COLOR_CYAN, COLOR_VOS_BG);
    kprintf("%llu", (unsigned long long)pid);
    fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
    kprintf("]> ");
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
            case KEY_F4:
                cmd_security();
                break;
            case KEY_F5:
                clear_content();
                break;
            case KEY_F6:
                handle_query("SELECT * FROM AuditTable");
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
        if (strcasecmp(line, "security") == 0 || strcasecmp(line, "crypto") == 0) {
            cmd_security();
            continue;
        }
        if (strcasecmp(line, "audit") == 0) {
            cmd_audit(20);
            continue;
        }
        if (strncasecmp(line, "audit ", 6) == 0) {
            const char *s = line + 6;
            while (*s == ' ') s++;
            int n = 0;
            while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
            cmd_audit(n > 0 ? n : 20);
            continue;
        }

        /* ---- Script commands ---- */
        if (strncasecmp(line, "save ", 5) == 0) {
            const char *name = line + 5;
            while (*name == ' ') name++;
            if (*name) script_save(name);
            else kprintf("  Usage: save <name>\n");
            continue;
        }
        if (strncasecmp(line, "run ", 4) == 0) {
            const char *name = line + 4;
            while (*name == ' ') name++;
            if (*name) script_run(name);
            else kprintf("  Usage: run <name>\n");
            continue;
        }
        if (strcasecmp(line, "scripts") == 0) {
            script_list();
            continue;
        }
        if (strncasecmp(line, "cat ", 4) == 0) {
            const char *name = line + 4;
            while (*name == ' ') name++;
            if (*name) script_cat(name);
            else kprintf("  Usage: cat <name>\n");
            continue;
        }

        /* ---- Process commands ---- */
        if (strncasecmp(line, "spawn ", 6) == 0) {
            const char *prog = line + 6;
            while (*prog == ' ') prog++;
            if (!*prog) {
                kprintf("  Usage: spawn <program>\n");
                kprintf("  Programs: monitor");
                for (int i = 0; builtin_programs[i].name; i++) {
                    if (i > 0) kprintf(", %s", builtin_programs[i].name);
                }
                kprintf("\n");
                continue;
            }

            /* Check for script:<name> */
            if (strncmp(prog, "script:", 7) == 0) {
                process_t *p = process_create(prog, script_process_entry);
                if (p) {
                    scheduler_add(p);
                    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
                    kprintf("  Spawned '%s' (PID %llu)\n", prog,
                            (unsigned long long)p->pid);
                    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
                } else {
                    fb_set_color(COLOR_RED, COLOR_VOS_BG);
                    kprintf("  Failed to spawn '%s'\n", prog);
                    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
                }
                continue;
            }

            /* Look up built-in program */
            bool found = false;
            for (int i = 0; builtin_programs[i].name; i++) {
                if (strcasecmp(prog, builtin_programs[i].name) == 0) {
                    process_t *p = process_create(builtin_programs[i].name,
                                                   builtin_programs[i].entry);
                    if (p) {
                        scheduler_add(p);
                        fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
                        kprintf("  Spawned '%s' (PID %llu)\n",
                                builtin_programs[i].name,
                                (unsigned long long)p->pid);
                        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
                    } else {
                        fb_set_color(COLOR_RED, COLOR_VOS_BG);
                        kprintf("  Failed to spawn '%s'\n",
                                builtin_programs[i].name);
                        fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                fb_set_color(COLOR_RED, COLOR_VOS_BG);
                kprintf("  Unknown program '%s'\n", prog);
                fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
            }
            continue;
        }

        if (strncasecmp(line, "kill ", 5) == 0) {
            const char *s = line + 5;
            while (*s == ' ') s++;
            uint64_t target_pid = 0;
            while (*s >= '0' && *s <= '9') {
                target_pid = target_pid * 10 + (*s - '0');
                s++;
            }
            if (target_pid == 0) {
                kprintf("  Usage: kill <pid>\n");
                continue;
            }
            /* Protect the shell process */
            process_t *cur = process_get_current();
            if (cur && target_pid == cur->pid) {
                fb_set_color(COLOR_RED, COLOR_VOS_BG);
                kprintf("  Cannot kill the shell process\n");
                fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
                continue;
            }
            process_t *victim = process_get_by_pid(target_pid);
            if (!victim) {
                fb_set_color(COLOR_RED, COLOR_VOS_BG);
                kprintf("  Process %llu not found\n",
                        (unsigned long long)target_pid);
                fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
            } else {
                process_exit(victim, -1);
                fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
                kprintf("  Killed process %llu (%s)\n",
                        (unsigned long long)target_pid, victim->name);
                fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
            }
            continue;
        }

        if (strncasecmp(line, "msg ", 4) == 0) {
            const char *s = line + 4;
            while (*s == ' ') s++;
            /* Parse destination PID */
            uint64_t dst_pid = 0;
            while (*s >= '0' && *s <= '9') {
                dst_pid = dst_pid * 10 + (*s - '0');
                s++;
            }
            while (*s == ' ') s++;
            if (dst_pid == 0 || !*s) {
                kprintf("  Usage: msg <pid> <text>\n");
                continue;
            }
            uint64_t src_pid = 0;
            process_t *cur = process_get_current();
            if (cur) src_pid = cur->pid;

            char sql_buf2[FRIENDLY_SQL_MAX];
            snprintf(sql_buf2, sizeof(sql_buf2),
                "INSERT INTO MessageTable (src_pid, dst_pid, type, payload, delivered) "
                "VALUES (%llu, %llu, 'user', '%s', false)",
                (unsigned long long)src_pid,
                (unsigned long long)dst_pid, s);
            query_result_t *r = db_execute(sql_buf2, src_pid);
            if (r && r->error_code == 0) {
                fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
                kprintf("  Message sent to PID %llu\n",
                        (unsigned long long)dst_pid);
                fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
            } else {
                fb_set_color(COLOR_RED, COLOR_VOS_BG);
                kprintf("  Failed to send message\n");
                fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
            }
            db_result_free(r);
            continue;
        }

        if (strcasecmp(line, "inbox") == 0) {
            uint64_t my_pid = 0;
            process_t *cur = process_get_current();
            if (cur) my_pid = cur->pid;

            char sql_buf2[FRIENDLY_SQL_MAX];
            snprintf(sql_buf2, sizeof(sql_buf2),
                "SELECT * FROM MessageTable WHERE dst_pid = %llu",
                (unsigned long long)my_pid);
            query_result_t *r = db_execute(sql_buf2, my_pid);
            display_result(r);
            db_result_free(r);
            continue;
        }

        /* Try friendly command translation, fall back to raw SQL */
        char sql_buf[FRIENDLY_SQL_MAX];
        if (friendly_translate(line, sql_buf, sizeof(sql_buf))) {
            handle_query(sql_buf);
        } else {
            handle_query(line);
        }
    }
}
