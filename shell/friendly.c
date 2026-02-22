#include "friendly.h"
#include "../kernel/lib/string.h"
#include "../kernel/lib/printf.h"
#include "../kernel/db/database.h"

/* ---- Table aliases ---- */
static const table_alias_t aliases[] = {
    { "procs",   "ProcessTable" },
    { "caps",    "CapabilityTable" },
    { "objects", "ObjectTable" },
    { "msgs",    "MessageTable" },
    { "audit",   "AuditTable" },
    { "config",  "SystemTable" },
    { "sys",     "SystemTable" },
    { NULL, NULL }
};

const table_alias_t *friendly_get_aliases(void) {
    return aliases;
}

const char *friendly_resolve_alias(const char *name) {
    for (int i = 0; aliases[i].alias; i++) {
        if (strcasecmp(name, aliases[i].alias) == 0)
            return aliases[i].table;
    }
    return name;
}

/* ---- Friendly verbs ---- */
static const char *verbs[] = {
    "tables", "show", "info", "find", "count", "add", "del", "set",
    "create", "open", "list", "rm", "cat", "ps", "scripts", "inbox",
    "save", "run", "kill", "spawn", "msg",
    NULL
};

bool friendly_is_verb(const char *word, size_t len) {
    for (int i = 0; verbs[i]; i++) {
        if (strlen(verbs[i]) == len && strncasecmp(verbs[i], word, len) == 0)
            return true;
    }
    return false;
}

/* ---- Parsing helpers ---- */

/* Skip whitespace, return pointer to next non-space char */
static const char *skip_spaces(const char *s) {
    while (*s && isspace(*s)) s++;
    return s;
}

/* Extract a token (word) from s into buf. Returns pointer past the token. */
static const char *next_token(const char *s, char *buf, size_t buf_size) {
    s = skip_spaces(s);
    size_t i = 0;
    while (*s && !isspace(*s) && i < buf_size - 1) {
        buf[i++] = *s++;
    }
    buf[i] = '\0';
    return s;
}

/* Check if a string looks numeric (digits only, optional leading minus) */
static bool is_numeric(const char *s) {
    if (!*s) return false;
    if (*s == '-') s++;
    if (!*s) return false;
    while (*s) {
        if (!isdigit(*s)) return false;
        s++;
    }
    return true;
}

/* Append a value to SQL, quoting strings but not numbers/booleans */
static size_t append_value(char *sql, size_t pos, size_t sql_size, const char *val) {
    if (is_numeric(val) ||
        strcasecmp(val, "true") == 0 ||
        strcasecmp(val, "false") == 0) {
        /* Numeric or boolean: no quotes */
        int n = snprintf(sql + pos, sql_size - pos, "%s", val);
        if (n > 0) pos += (size_t)n;
    } else {
        /* String: wrap in single quotes */
        int n = snprintf(sql + pos, sql_size - pos, "'%s'", val);
        if (n > 0) pos += (size_t)n;
    }
    return pos;
}

/* Parse "col=val" pair. Returns pointer past the pair, or NULL on failure. */
static const char *parse_pair(const char *s, char *col, size_t col_size,
                               char *val, size_t val_size) {
    s = skip_spaces(s);
    if (!*s) return NULL;

    /* Read column name (up to '=') */
    size_t i = 0;
    while (*s && *s != '=' && !isspace(*s) && i < col_size - 1) {
        col[i++] = *s++;
    }
    col[i] = '\0';
    if (i == 0 || *s != '=') return NULL;
    s++; /* skip '=' */

    /* Read value (up to next space or end) */
    i = 0;
    if (*s == '\'') {
        /* Quoted value */
        s++;
        while (*s && *s != '\'' && i < val_size - 1) {
            val[i++] = *s++;
        }
        if (*s == '\'') s++;
    } else {
        while (*s && !isspace(*s) && i < val_size - 1) {
            val[i++] = *s++;
        }
    }
    val[i] = '\0';
    return s;
}

/* ---- Main translator ---- */

bool friendly_translate(const char *input, char *sql_out, size_t sql_size) {
    char verb[32];
    const char *rest = next_token(input, verb, sizeof(verb));

    if (verb[0] == '\0') return false;

    /* tables */
    if (strcasecmp(verb, "tables") == 0) {
        snprintf(sql_out, sql_size, "SHOW TABLES");
        return true;
    }

    /* show <table> */
    if (strcasecmp(verb, "show") == 0) {
        char table[MAX_TABLE_NAME];
        rest = next_token(rest, table, sizeof(table));
        if (table[0] == '\0') return false;
        /* "show tables" is also valid */
        if (strcasecmp(table, "tables") == 0) {
            snprintf(sql_out, sql_size, "SHOW TABLES");
            return true;
        }
        const char *resolved = friendly_resolve_alias(table);
        snprintf(sql_out, sql_size, "SELECT * FROM %s", resolved);
        return true;
    }

    /* info <table> */
    if (strcasecmp(verb, "info") == 0) {
        char table[MAX_TABLE_NAME];
        rest = next_token(rest, table, sizeof(table));
        if (table[0] == '\0') return false;
        const char *resolved = friendly_resolve_alias(table);
        snprintf(sql_out, sql_size, "DESCRIBE %s", resolved);
        return true;
    }

    /* count <table> */
    if (strcasecmp(verb, "count") == 0) {
        char table[MAX_TABLE_NAME];
        rest = next_token(rest, table, sizeof(table));
        if (table[0] == '\0') return false;
        const char *resolved = friendly_resolve_alias(table);
        /* Use SELECT * — the shell will show row count in the result */
        snprintf(sql_out, sql_size, "SELECT * FROM %s", resolved);
        return true;
    }

    /* find <table> [col=val ...] */
    if (strcasecmp(verb, "find") == 0) {
        char table[MAX_TABLE_NAME];
        rest = next_token(rest, table, sizeof(table));
        if (table[0] == '\0') return false;
        const char *resolved = friendly_resolve_alias(table);

        /* Check for conditions */
        rest = skip_spaces(rest);
        if (*rest == '\0') {
            /* No conditions: show all */
            snprintf(sql_out, sql_size, "SELECT * FROM %s", resolved);
            return true;
        }

        size_t pos = (size_t)snprintf(sql_out, sql_size, "SELECT * FROM %s WHERE ", resolved);
        bool first = true;

        while (*rest) {
            char col[MAX_COLUMN_NAME], val[MAX_STR_LEN];
            const char *next = parse_pair(rest, col, sizeof(col), val, sizeof(val));
            if (!next) break;

            if (!first) {
                int n = snprintf(sql_out + pos, sql_size - pos, " AND ");
                if (n > 0) pos += (size_t)n;
            }
            int n = snprintf(sql_out + pos, sql_size - pos, "%s = ", col);
            if (n > 0) pos += (size_t)n;
            pos = append_value(sql_out, pos, sql_size, val);

            first = false;
            rest = next;
            rest = skip_spaces(rest);
        }
        return true;
    }

    /* add <table> col=val [col=val ...] */
    if (strcasecmp(verb, "add") == 0) {
        char table[MAX_TABLE_NAME];
        rest = next_token(rest, table, sizeof(table));
        if (table[0] == '\0') return false;
        const char *resolved = friendly_resolve_alias(table);

        /* Collect col=val pairs */
        char cols[MAX_INSERT_VALS][MAX_COLUMN_NAME];
        char vals[MAX_INSERT_VALS][MAX_STR_LEN];
        int pair_count = 0;

        rest = skip_spaces(rest);
        while (*rest && pair_count < MAX_INSERT_VALS) {
            const char *next = parse_pair(rest, cols[pair_count], MAX_COLUMN_NAME,
                                           vals[pair_count], MAX_STR_LEN);
            if (!next) break;
            pair_count++;
            rest = next;
            rest = skip_spaces(rest);
        }

        if (pair_count == 0) return false;

        /* Build INSERT */
        size_t pos = (size_t)snprintf(sql_out, sql_size, "INSERT INTO %s (", resolved);
        for (int i = 0; i < pair_count; i++) {
            int n = snprintf(sql_out + pos, sql_size - pos, "%s%s",
                             i > 0 ? ", " : "", cols[i]);
            if (n > 0) pos += (size_t)n;
        }
        {
            int n = snprintf(sql_out + pos, sql_size - pos, ") VALUES (");
            if (n > 0) pos += (size_t)n;
        }
        for (int i = 0; i < pair_count; i++) {
            if (i > 0) {
                int n = snprintf(sql_out + pos, sql_size - pos, ", ");
                if (n > 0) pos += (size_t)n;
            }
            pos = append_value(sql_out, pos, sql_size, vals[i]);
        }
        snprintf(sql_out + pos, sql_size - pos, ")");
        return true;
    }

    /* del <table> col=val [col=val ...] */
    if (strcasecmp(verb, "del") == 0) {
        char table[MAX_TABLE_NAME];
        rest = next_token(rest, table, sizeof(table));
        if (table[0] == '\0') return false;
        const char *resolved = friendly_resolve_alias(table);

        rest = skip_spaces(rest);
        if (*rest == '\0') return false; /* require at least one condition */

        size_t pos = (size_t)snprintf(sql_out, sql_size, "DELETE FROM %s WHERE ", resolved);
        bool first = true;

        while (*rest) {
            char col[MAX_COLUMN_NAME], val[MAX_STR_LEN];
            const char *next = parse_pair(rest, col, sizeof(col), val, sizeof(val));
            if (!next) break;

            if (!first) {
                int n = snprintf(sql_out + pos, sql_size - pos, " AND ");
                if (n > 0) pos += (size_t)n;
            }
            int n = snprintf(sql_out + pos, sql_size - pos, "%s = ", col);
            if (n > 0) pos += (size_t)n;
            pos = append_value(sql_out, pos, sql_size, val);

            first = false;
            rest = next;
            rest = skip_spaces(rest);
        }
        return true;
    }

    /* set <table> col=val [col=val] where key=kval */
    if (strcasecmp(verb, "set") == 0) {
        char table[MAX_TABLE_NAME];
        rest = next_token(rest, table, sizeof(table));
        if (table[0] == '\0') return false;
        const char *resolved = friendly_resolve_alias(table);

        /* Collect SET pairs until "where" keyword */
        char set_cols[MAX_INSERT_VALS][MAX_COLUMN_NAME];
        char set_vals[MAX_INSERT_VALS][MAX_STR_LEN];
        int set_count = 0;

        rest = skip_spaces(rest);
        while (*rest && set_count < MAX_INSERT_VALS) {
            /* Check for "where" keyword */
            char peek[32];
            const char *peek_rest = next_token(rest, peek, sizeof(peek));
            if (strcasecmp(peek, "where") == 0) {
                rest = peek_rest;
                break;
            }

            const char *next = parse_pair(rest, set_cols[set_count], MAX_COLUMN_NAME,
                                           set_vals[set_count], MAX_STR_LEN);
            if (!next) break;
            set_count++;
            rest = next;
            rest = skip_spaces(rest);
        }

        if (set_count == 0) return false;

        /* Build UPDATE ... SET ... */
        size_t pos = (size_t)snprintf(sql_out, sql_size, "UPDATE %s SET ", resolved);
        for (int i = 0; i < set_count; i++) {
            if (i > 0) {
                int n = snprintf(sql_out + pos, sql_size - pos, ", ");
                if (n > 0) pos += (size_t)n;
            }
            int n = snprintf(sql_out + pos, sql_size - pos, "%s = ", set_cols[i]);
            if (n > 0) pos += (size_t)n;
            pos = append_value(sql_out, pos, sql_size, set_vals[i]);
        }

        /* Parse WHERE conditions */
        rest = skip_spaces(rest);
        if (*rest) {
            int n = snprintf(sql_out + pos, sql_size - pos, " WHERE ");
            if (n > 0) pos += (size_t)n;

            bool first = true;
            while (*rest) {
                char col[MAX_COLUMN_NAME], val[MAX_STR_LEN];
                const char *next = parse_pair(rest, col, sizeof(col), val, sizeof(val));
                if (!next) break;

                if (!first) {
                    n = snprintf(sql_out + pos, sql_size - pos, " AND ");
                    if (n > 0) pos += (size_t)n;
                }
                n = snprintf(sql_out + pos, sql_size - pos, "%s = ", col);
                if (n > 0) pos += (size_t)n;
                pos = append_value(sql_out, pos, sql_size, val);

                first = false;
                rest = next;
                rest = skip_spaces(rest);
            }
        }
        return true;
    }

    /* create <type> <name> [content] */
    if (strcasecmp(verb, "create") == 0) {
        char type[MAX_STR_LEN], name[MAX_STR_LEN];
        rest = next_token(rest, type, sizeof(type));
        if (type[0] == '\0') return false;
        rest = next_token(rest, name, sizeof(name));
        if (name[0] == '\0') return false;

        rest = skip_spaces(rest);
        if (*rest) {
            /* Content provided — strip surrounding quotes if present */
            const char *p = rest;
            if (*p == '\'') p++;
            char content[MAX_STR_LEN];
            size_t ci = 0;
            while (*p && ci < MAX_STR_LEN - 1) {
                if (*p == '\'' && *(p + 1) == '\0') break;
                content[ci++] = *p++;
            }
            content[ci] = '\0';
            snprintf(sql_out, sql_size,
                "INSERT INTO ObjectTable (name, type, data) VALUES ('%s', '%s', '%s')",
                name, type, content);
        } else {
            snprintf(sql_out, sql_size,
                "INSERT INTO ObjectTable (name, type) VALUES ('%s', '%s')",
                name, type);
        }
        return true;
    }

    /* open <name> */
    if (strcasecmp(verb, "open") == 0) {
        char name[MAX_STR_LEN];
        rest = next_token(rest, name, sizeof(name));
        if (name[0] == '\0') return false;
        snprintf(sql_out, sql_size,
            "SELECT * FROM ObjectTable WHERE name = '%s'", name);
        return true;
    }

    /* list [type] */
    if (strcasecmp(verb, "list") == 0) {
        char type[MAX_STR_LEN];
        rest = next_token(rest, type, sizeof(type));
        if (type[0] == '\0') {
            snprintf(sql_out, sql_size, "SELECT * FROM ObjectTable");
        } else {
            snprintf(sql_out, sql_size,
                "SELECT * FROM ObjectTable WHERE type = '%s'", type);
        }
        return true;
    }

    /* rm <name> */
    if (strcasecmp(verb, "rm") == 0) {
        char name[MAX_STR_LEN];
        rest = next_token(rest, name, sizeof(name));
        if (name[0] == '\0') return false;
        snprintf(sql_out, sql_size,
            "DELETE FROM ObjectTable WHERE name = '%s'", name);
        return true;
    }

    /* ps */
    if (strcasecmp(verb, "ps") == 0) {
        snprintf(sql_out, sql_size, "SELECT * FROM ProcessTable");
        return true;
    }

    /* Not a friendly command */
    return false;
}
