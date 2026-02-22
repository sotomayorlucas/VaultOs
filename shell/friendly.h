#ifndef VAULTOS_FRIENDLY_H
#define VAULTOS_FRIENDLY_H

#include "../kernel/lib/types.h"

#define FRIENDLY_SQL_MAX 512

/* Translate a friendly command to SQL.
 * Returns true if input was recognized as a friendly command.
 * Returns false if not recognized (caller should try raw SQL). */
bool friendly_translate(const char *input, char *sql_out, size_t sql_size);

/* Resolve a table alias (e.g. "procs" -> "ProcessTable").
 * Returns the original name if no alias matches. */
const char *friendly_resolve_alias(const char *name);

/* Check if a word is a friendly verb */
bool friendly_is_verb(const char *word, size_t len);

/* Table alias list for completion (NULL-terminated) */
typedef struct {
    const char *alias;
    const char *table;
} table_alias_t;

const table_alias_t *friendly_get_aliases(void);

#endif /* VAULTOS_FRIENDLY_H */
