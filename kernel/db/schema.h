#ifndef VAULTOS_SCHEMA_H
#define VAULTOS_SCHEMA_H

#include "../../include/vaultos/db_types.h"

typedef struct {
    char          name[MAX_COLUMN_NAME];
    column_type_t type;
    bool          primary_key;
    bool          not_null;
    bool          indexed;
} column_def_t;

typedef struct {
    char         name[MAX_TABLE_NAME];
    uint32_t     table_id;
    column_def_t columns[MAX_COLUMNS];
    uint32_t     column_count;
    bool         encrypted;
    bool         system_table;
} table_schema_t;

#endif /* VAULTOS_SCHEMA_H */
