#include "complete.h"
#include "friendly.h"
#include "../kernel/lib/string.h"
#include "../kernel/lib/printf.h"
#include "../kernel/db/database.h"

static const char *sql_keywords[] = {
    "SELECT", "INSERT", "INTO", "DELETE", "UPDATE",
    "FROM", "WHERE", "AND", "SET", "VALUES",
    "SHOW", "TABLES", "DESCRIBE",
    "GRANT", "REVOKE", "ON", "TO",
    "READ", "WRITE", "ALL",
    NULL
};

static const char *friendly_verbs[] = {
    "tables", "show", "info", "find", "count", "add", "del", "set",
    "create", "open", "list", "rm", "cat",
    "ps", "kill", "spawn", "msg", "inbox",
    "save", "run", "scripts",
    "security", "crypto", "audit",
    NULL
};

static const char *object_types[] = {
    "note", "file", "config", "script", "key",
    NULL
};

/* Extract the word fragment before cursor_pos */
static uint32_t extract_word(const char *line, uint32_t cursor_pos,
                              char *word, size_t word_size) {
    /* Scan backward from cursor to find word start */
    uint32_t start = cursor_pos;
    while (start > 0 && line[start - 1] != ' ' && line[start - 1] != '('
                      && line[start - 1] != ',')
        start--;

    uint32_t len = cursor_pos - start;
    if (len >= word_size) len = word_size - 1;
    memcpy(word, line + start, len);
    word[len] = '\0';
    return start;
}

/* Compute longest common prefix of all matches */
static void compute_common_prefix(completion_result_t *result) {
    if (result->count == 0) {
        result->common_prefix[0] = '\0';
        return;
    }
    strncpy(result->common_prefix, result->matches[0], MAX_COMPLETION_LEN - 1);
    result->common_prefix[MAX_COMPLETION_LEN - 1] = '\0';

    for (uint32_t i = 1; i < result->count; i++) {
        uint32_t j = 0;
        while (result->common_prefix[j] &&
               toupper(result->common_prefix[j]) == toupper(result->matches[i][j]))
            j++;
        result->common_prefix[j] = '\0';
    }
}

static void add_match(completion_result_t *result, const char *str) {
    if (result->count >= MAX_COMPLETIONS) return;
    strncpy(result->matches[result->count], str, MAX_COMPLETION_LEN - 1);
    result->matches[result->count][MAX_COMPLETION_LEN - 1] = '\0';
    result->count++;
}

/* Extract space-separated tokens from line up to cursor_pos */
static int tokenize(const char *line, uint32_t cursor_pos,
                     char tokens[][MAX_COMPLETION_LEN], int max_tokens) {
    int count = 0;
    uint32_t i = 0;
    while (i < cursor_pos && count < max_tokens) {
        /* Skip spaces */
        while (i < cursor_pos && line[i] == ' ') i++;
        if (i >= cursor_pos) break;

        /* Extract token */
        uint32_t start = i;
        while (i < cursor_pos && line[i] != ' ') i++;
        uint32_t len = i - start;
        if (len >= MAX_COMPLETION_LEN) len = MAX_COMPLETION_LEN - 1;
        memcpy(tokens[count], line + start, len);
        tokens[count][len] = '\0';
        count++;
    }
    return count;
}

/* Check if a token is a friendly verb */
static bool is_friendly_verb(const char *token) {
    for (int i = 0; friendly_verbs[i]; i++) {
        if (strcasecmp(friendly_verbs[i], token) == 0) return true;
    }
    return false;
}

/* Resolve a table name (including aliases) and return schema, or NULL */
static table_schema_t *resolve_table_schema(const char *name) {
    const char *resolved = friendly_resolve_alias(name);
    return db_get_schema(resolved);
}

void complete_find(const char *line, uint32_t cursor_pos, completion_result_t *result) {
    result->count = 0;
    result->common_prefix[0] = '\0';

    char word[MAX_COMPLETION_LEN];
    extract_word(line, cursor_pos, word, sizeof(word));

    if (strlen(word) == 0) return;

    size_t wlen = strlen(word);

    /* Analyze context: tokenize everything before the current word */
    char tokens[8][MAX_COMPLETION_LEN];
    /* Find start of current word */
    uint32_t word_start = cursor_pos;
    while (word_start > 0 && line[word_start - 1] != ' ' && line[word_start - 1] != '('
                          && line[word_start - 1] != ',')
        word_start--;
    int token_count = tokenize(line, word_start, tokens, 8);

    /* Context: If first token is a friendly verb and we have a resolved table,
     * complete with column names (appending '=') */
    if (token_count >= 2 && is_friendly_verb(tokens[0])) {
        /* tokens[0] = verb, tokens[1] = table name */
        /* Verbs that take col=val pairs after the table */
        if (strcasecmp(tokens[0], "find") == 0 ||
            strcasecmp(tokens[0], "add") == 0 ||
            strcasecmp(tokens[0], "del") == 0 ||
            strcasecmp(tokens[0], "set") == 0) {

            table_schema_t *schema = resolve_table_schema(tokens[1]);
            if (schema) {
                /* Check if word contains '=' — if so, don't complete columns */
                bool has_eq = (strchr(word, '=') != NULL);
                if (!has_eq) {
                    /* Complete against column names with '=' suffix */
                    for (uint32_t c = 0; c < schema->column_count; c++) {
                        if (strncasecmp(schema->columns[c].name, word, wlen) == 0) {
                            char col_eq[MAX_COMPLETION_LEN];
                            snprintf(col_eq, sizeof(col_eq), "%s=",
                                     schema->columns[c].name);
                            add_match(result, col_eq);
                        }
                    }
                    if (result->count > 0) {
                        compute_common_prefix(result);
                        return;
                    }
                }
            }
        }
    }

    /* Context: "create" → complete with object types as second token */
    if (token_count == 1 && strcasecmp(tokens[0], "create") == 0) {
        for (int i = 0; object_types[i]; i++) {
            if (strncasecmp(object_types[i], word, wlen) == 0) {
                add_match(result, object_types[i]);
            }
        }
        if (result->count > 0) {
            compute_common_prefix(result);
            return;
        }
    }

    /* Context: "spawn" → complete with built-in program names */
    if (token_count == 1 && strcasecmp(tokens[0], "spawn") == 0) {
        const char *programs[] = { "monitor", NULL };
        for (int i = 0; programs[i]; i++) {
            if (strncasecmp(programs[i], word, wlen) == 0) {
                add_match(result, programs[i]);
            }
        }
        /* Also offer "script:" prefix */
        if (strncasecmp("script:", word, wlen) == 0) {
            add_match(result, "script:");
        }
        if (result->count > 0) {
            compute_common_prefix(result);
            return;
        }
    }

    /* Context: If first token is a friendly verb that takes a table name,
     * and we're on the second token, complete with table names + aliases */
    if (token_count == 1 && is_friendly_verb(tokens[0]) &&
        strcasecmp(tokens[0], "tables") != 0 &&
        strcasecmp(tokens[0], "create") != 0 &&
        strcasecmp(tokens[0], "spawn") != 0 &&
        strcasecmp(tokens[0], "scripts") != 0 &&
        strcasecmp(tokens[0], "ps") != 0 &&
        strcasecmp(tokens[0], "inbox") != 0) {
        /* Complete with table names */
        uint32_t table_count = db_get_table_count();
        for (uint32_t t = 0; t < table_count; t++) {
            table_schema_t *schema = db_get_schema_by_id(t);
            if (schema && strncasecmp(schema->name, word, wlen) == 0) {
                add_match(result, schema->name);
            }
        }
        /* Also complete aliases */
        const table_alias_t *al = friendly_get_aliases();
        for (int i = 0; al[i].alias; i++) {
            if (strncasecmp(al[i].alias, word, wlen) == 0) {
                add_match(result, al[i].alias);
            }
        }
        if (result->count > 0) {
            compute_common_prefix(result);
            return;
        }
    }

    /* Default: match SQL keywords */
    for (int i = 0; sql_keywords[i]; i++) {
        if (strncasecmp(sql_keywords[i], word, wlen) == 0) {
            add_match(result, sql_keywords[i]);
        }
    }

    /* Match friendly verbs */
    for (int i = 0; friendly_verbs[i]; i++) {
        if (strncasecmp(friendly_verbs[i], word, wlen) == 0) {
            add_match(result, friendly_verbs[i]);
        }
    }

    /* Match table names */
    uint32_t table_count = db_get_table_count();
    for (uint32_t t = 0; t < table_count; t++) {
        table_schema_t *schema = db_get_schema_by_id(t);
        if (schema && strncasecmp(schema->name, word, wlen) == 0) {
            add_match(result, schema->name);
        }
    }

    /* Match table aliases */
    const table_alias_t *al = friendly_get_aliases();
    for (int i = 0; al[i].alias; i++) {
        if (strncasecmp(al[i].alias, word, wlen) == 0) {
            add_match(result, al[i].alias);
        }
    }

    compute_common_prefix(result);
}
