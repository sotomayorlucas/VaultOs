#include "complete.h"
#include "../kernel/lib/string.h"
#include "../kernel/db/database.h"

static const char *sql_keywords[] = {
    "SELECT", "INSERT", "INTO", "DELETE", "UPDATE",
    "FROM", "WHERE", "AND", "SET", "VALUES",
    "SHOW", "TABLES", "DESCRIBE",
    "GRANT", "REVOKE", "ON", "TO",
    "READ", "WRITE", "ALL",
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

void complete_find(const char *line, uint32_t cursor_pos, completion_result_t *result) {
    result->count = 0;
    result->common_prefix[0] = '\0';

    char word[MAX_COMPLETION_LEN];
    extract_word(line, cursor_pos, word, sizeof(word));

    if (strlen(word) == 0) return;

    size_t wlen = strlen(word);

    /* Match SQL keywords */
    for (int i = 0; sql_keywords[i]; i++) {
        if (strncasecmp(sql_keywords[i], word, wlen) == 0) {
            add_match(result, sql_keywords[i]);
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

    compute_common_prefix(result);
}
