#include "query.h"
#include "database.h"
#include "btree.h"
#include "record.h"
#include "../cap/capability.h"
#include "../cap/cap_table.h"
#include "../arch/x86_64/pit.h"
#include "../mm/heap.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "../../include/vaultos/error_codes.h"

/*
 * VaultOS Query Parser
 * Recursive-descent parser for SQL-subset:
 *   SELECT [cols|*] FROM table [WHERE conditions]
 *   INSERT INTO table (cols) VALUES (vals)
 *   DELETE FROM table [WHERE conditions]
 *   UPDATE table SET col=val [WHERE conditions]
 *   SHOW TABLES
 *   DESCRIBE table
 *   GRANT rights ON object_id TO process_id
 *   REVOKE cap_id
 */

/* ---- Tokenizer ---- */

typedef enum {
    TOK_SELECT, TOK_INSERT, TOK_INTO, TOK_DELETE, TOK_UPDATE,
    TOK_FROM, TOK_WHERE, TOK_AND, TOK_SET, TOK_VALUES,
    TOK_SHOW, TOK_TABLES, TOK_DESCRIBE,
    TOK_GRANT, TOK_REVOKE, TOK_ON, TOK_TO,
    TOK_READ, TOK_WRITE, TOK_ALL,
    TOK_STAR, TOK_COMMA, TOK_LPAREN, TOK_RPAREN,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_IDENT, TOK_STRING, TOK_NUMBER,
    TOK_EOF, TOK_ERROR
} token_type_t;

typedef struct {
    token_type_t type;
    char         value[MAX_STR_LEN + 1];
} token_t;

typedef struct {
    const char *input;
    size_t      pos;
    token_t     current;
} parser_t;

static void skip_whitespace(parser_t *p) {
    while (p->input[p->pos] && isspace(p->input[p->pos])) p->pos++;
}

static token_type_t check_keyword(const char *word) {
    if (strcasecmp(word, "SELECT") == 0) return TOK_SELECT;
    if (strcasecmp(word, "INSERT") == 0) return TOK_INSERT;
    if (strcasecmp(word, "INTO") == 0) return TOK_INTO;
    if (strcasecmp(word, "DELETE") == 0) return TOK_DELETE;
    if (strcasecmp(word, "UPDATE") == 0) return TOK_UPDATE;
    if (strcasecmp(word, "FROM") == 0) return TOK_FROM;
    if (strcasecmp(word, "WHERE") == 0) return TOK_WHERE;
    if (strcasecmp(word, "AND") == 0) return TOK_AND;
    if (strcasecmp(word, "SET") == 0) return TOK_SET;
    if (strcasecmp(word, "VALUES") == 0) return TOK_VALUES;
    if (strcasecmp(word, "SHOW") == 0) return TOK_SHOW;
    if (strcasecmp(word, "TABLES") == 0) return TOK_TABLES;
    if (strcasecmp(word, "DESCRIBE") == 0) return TOK_DESCRIBE;
    if (strcasecmp(word, "GRANT") == 0) return TOK_GRANT;
    if (strcasecmp(word, "REVOKE") == 0) return TOK_REVOKE;
    if (strcasecmp(word, "ON") == 0) return TOK_ON;
    if (strcasecmp(word, "TO") == 0) return TOK_TO;
    if (strcasecmp(word, "READ") == 0) return TOK_READ;
    if (strcasecmp(word, "WRITE") == 0) return TOK_WRITE;
    if (strcasecmp(word, "ALL") == 0) return TOK_ALL;
    return TOK_IDENT;
}

static void next_token(parser_t *p) {
    skip_whitespace(p);
    char c = p->input[p->pos];

    if (c == '\0') { p->current.type = TOK_EOF; return; }

    /* Single-char tokens */
    if (c == '*') { p->current.type = TOK_STAR; p->current.value[0] = '*'; p->current.value[1] = '\0'; p->pos++; return; }
    if (c == ',') { p->current.type = TOK_COMMA; p->pos++; return; }
    if (c == '(') { p->current.type = TOK_LPAREN; p->pos++; return; }
    if (c == ')') { p->current.type = TOK_RPAREN; p->pos++; return; }
    if (c == '=') { p->current.type = TOK_EQ; p->pos++; return; }

    /* Two-char operators */
    if (c == '!' && p->input[p->pos + 1] == '=') { p->current.type = TOK_NEQ; p->pos += 2; return; }
    if (c == '<' && p->input[p->pos + 1] == '=') { p->current.type = TOK_LE; p->pos += 2; return; }
    if (c == '>' && p->input[p->pos + 1] == '=') { p->current.type = TOK_GE; p->pos += 2; return; }
    if (c == '<') { p->current.type = TOK_LT; p->pos++; return; }
    if (c == '>') { p->current.type = TOK_GT; p->pos++; return; }

    /* String literal */
    if (c == '\'') {
        p->pos++;
        int i = 0;
        while (p->input[p->pos] && p->input[p->pos] != '\'' && i < MAX_STR_LEN) {
            p->current.value[i++] = p->input[p->pos++];
        }
        p->current.value[i] = '\0';
        if (p->input[p->pos] == '\'') p->pos++;
        p->current.type = TOK_STRING;
        return;
    }

    /* Number */
    if (isdigit(c)) {
        int i = 0;
        while (isdigit(p->input[p->pos]) && i < 20) {
            p->current.value[i++] = p->input[p->pos++];
        }
        p->current.value[i] = '\0';
        p->current.type = TOK_NUMBER;
        return;
    }

    /* Identifier or keyword */
    if (isalpha(c) || c == '_') {
        int i = 0;
        while ((isalnum(p->input[p->pos]) || p->input[p->pos] == '_') && i < MAX_STR_LEN) {
            p->current.value[i++] = p->input[p->pos++];
        }
        p->current.value[i] = '\0';
        p->current.type = check_keyword(p->current.value);
        return;
    }

    p->current.type = TOK_ERROR;
    p->pos++;
}

static bool expect(parser_t *p, token_type_t type) {
    if (p->current.type != type) return false;
    next_token(p);
    return true;
}

/* ---- WHERE clause evaluation ---- */

typedef struct {
    char        column[MAX_COLUMN_NAME];
    cmp_op_t    op;
    field_value_t value;
} where_cond_t;

static bool match_field(const field_value_t *field, cmp_op_t op, const field_value_t *cond_val) {
    if (field->type == COL_STR && cond_val->type == COL_STR) {
        int cmp = strcmp(field->str_val.data, cond_val->str_val.data);
        switch (op) {
            case CMP_EQ: return cmp == 0;
            case CMP_NEQ: return cmp != 0;
            case CMP_LT: return cmp < 0;
            case CMP_GT: return cmp > 0;
            case CMP_LE: return cmp <= 0;
            case CMP_GE: return cmp >= 0;
            default: return false;
        }
    }
    if (field->type == COL_U64 && cond_val->type == COL_U64) {
        switch (op) {
            case CMP_EQ: return field->u64_val == cond_val->u64_val;
            case CMP_NEQ: return field->u64_val != cond_val->u64_val;
            case CMP_LT: return field->u64_val < cond_val->u64_val;
            case CMP_GT: return field->u64_val > cond_val->u64_val;
            case CMP_LE: return field->u64_val <= cond_val->u64_val;
            case CMP_GE: return field->u64_val >= cond_val->u64_val;
            default: return false;
        }
    }
    if (field->type == COL_BOOL && cond_val->type == COL_BOOL) {
        return field->bool_val == cond_val->bool_val;
    }
    /* Cross-type: try converting string to u64 for comparison */
    if (field->type == COL_U64 && cond_val->type == COL_STR) {
        uint64_t cv = strtou64(cond_val->str_val.data, NULL, 10);
        switch (op) {
            case CMP_EQ: return field->u64_val == cv;
            case CMP_NEQ: return field->u64_val != cv;
            default: return false;
        }
    }
    return false;
}

/* ---- Scan callback context ---- */

typedef struct {
    query_result_t *result;
    table_schema_t *schema;
    where_cond_t   *conditions;
    uint32_t        cond_count;
} scan_ctx_t;

static int find_column_index(table_schema_t *schema, const char *name) {
    for (uint32_t i = 0; i < schema->column_count; i++) {
        if (strcasecmp(schema->columns[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static bool record_matches(record_t *rec, table_schema_t *schema,
                            where_cond_t *conds, uint32_t cond_count) {
    for (uint32_t c = 0; c < cond_count; c++) {
        int col_idx = find_column_index(schema, conds[c].column);
        if (col_idx < 0) return false;
        if (!match_field(&rec->fields[col_idx], conds[c].op, &conds[c].value))
            return false;
    }
    return true;
}

/*
 * Scan callbacks: decrypt each encrypted record from the B-tree,
 * then apply WHERE filtering on the plaintext.
 */
static void select_scan_callback(uint64_t key, void *value, void *ctx) {
    (void)key;
    scan_ctx_t *sc = (scan_ctx_t *)ctx;

    record_t *rec = db_decrypt_record(sc->schema->table_id, value);
    if (!rec) return; /* MAC verification failed or decrypt error */

    if (record_matches(rec, sc->schema, sc->conditions, sc->cond_count)) {
        db_result_add_row(sc->result, rec);
    }
}

/* ---- Query execution ---- */

static cmp_op_t parse_op(parser_t *p) {
    switch (p->current.type) {
        case TOK_EQ: next_token(p); return CMP_EQ;
        case TOK_NEQ: next_token(p); return CMP_NEQ;
        case TOK_LT: next_token(p); return CMP_LT;
        case TOK_GT: next_token(p); return CMP_GT;
        case TOK_LE: next_token(p); return CMP_LE;
        case TOK_GE: next_token(p); return CMP_GE;
        default: return CMP_EQ;
    }
}

static uint32_t parse_where(parser_t *p, where_cond_t *conds) {
    uint32_t count = 0;
    if (p->current.type != TOK_WHERE) return 0;
    next_token(p); /* skip WHERE */

    while (count < MAX_WHERE_CONDS) {
        if (p->current.type != TOK_IDENT) break;

        strncpy(conds[count].column, p->current.value, MAX_COLUMN_NAME - 1);
        next_token(p);

        conds[count].op = parse_op(p);

        /* Value */
        if (p->current.type == TOK_STRING) {
            conds[count].value.type = COL_STR;
            strncpy(conds[count].value.str_val.data, p->current.value, MAX_STR_LEN);
            conds[count].value.str_val.length = (uint16_t)strlen(p->current.value);
        } else if (p->current.type == TOK_NUMBER) {
            conds[count].value.type = COL_U64;
            conds[count].value.u64_val = strtou64(p->current.value, NULL, 10);
        } else {
            break;
        }
        next_token(p);
        count++;

        if (p->current.type == TOK_AND) {
            next_token(p);
        } else {
            break;
        }
    }
    return count;
}

static query_result_t *exec_show_tables(void) {
    uint32_t tc = db_get_table_count();
    query_result_t *result = db_result_create(tc);
    if (!result) return db_result_error(VOS_ERR_NOMEM, "Out of memory");

    for (uint32_t i = 0; i < tc; i++) {
        table_schema_t *s = db_get_schema_by_id(i);
        record_t row;
        memset(&row, 0, sizeof(row));
        row.row_id = i;
        row.table_id = i;
        row.field_count = 3;
        record_set_u64(&row, 0, i);
        record_set_str(&row, 1, s->name);
        record_set_u64(&row, 2, s->column_count);
        db_result_add_row(result, &row);
    }

    /* Set schema for display */
    static table_schema_t show_schema = {
        .name = "Tables", .column_count = 3
    };
    strcpy(show_schema.columns[0].name, "id");
    show_schema.columns[0].type = COL_U64;
    strcpy(show_schema.columns[1].name, "table_name");
    show_schema.columns[1].type = COL_STR;
    strcpy(show_schema.columns[2].name, "columns");
    show_schema.columns[2].type = COL_U64;
    result->schema = &show_schema;

    return result;
}

static query_result_t *exec_describe(parser_t *p) {
    if (p->current.type != TOK_IDENT) return db_result_error(VOS_ERR_SYNTAX, "Expected table name");
    table_schema_t *schema = db_get_schema(p->current.value);
    if (!schema) return db_result_error(VOS_ERR_NOTFOUND, "Table not found");

    query_result_t *result = db_result_create(schema->column_count);
    if (!result) return db_result_error(VOS_ERR_NOMEM, "Out of memory");

    for (uint32_t i = 0; i < schema->column_count; i++) {
        record_t row;
        memset(&row, 0, sizeof(row));
        row.row_id = i;
        row.field_count = 4;
        record_set_str(&row, 0, schema->columns[i].name);

        const char *type_str = "unknown";
        switch (schema->columns[i].type) {
            case COL_U64: type_str = "U64"; break;
            case COL_I64: type_str = "I64"; break;
            case COL_STR: type_str = "STR"; break;
            case COL_BLOB: type_str = "BLOB"; break;
            case COL_BOOL: type_str = "BOOL"; break;
            case COL_U32: type_str = "U32"; break;
            case COL_U8: type_str = "U8"; break;
        }
        record_set_str(&row, 1, type_str);
        record_set_str(&row, 2, schema->columns[i].primary_key ? "YES" : "NO");
        record_set_str(&row, 3, schema->columns[i].not_null ? "YES" : "NO");
        db_result_add_row(result, &row);
    }

    static table_schema_t desc_schema = { .name = "Columns", .column_count = 4 };
    strcpy(desc_schema.columns[0].name, "name"); desc_schema.columns[0].type = COL_STR;
    strcpy(desc_schema.columns[1].name, "type"); desc_schema.columns[1].type = COL_STR;
    strcpy(desc_schema.columns[2].name, "pk");   desc_schema.columns[2].type = COL_STR;
    strcpy(desc_schema.columns[3].name, "not_null"); desc_schema.columns[3].type = COL_STR;
    result->schema = &desc_schema;

    return result;
}

static query_result_t *exec_select(parser_t *p, uint64_t pid) {
    (void)pid;
    /* SELECT * FROM table [WHERE ...] */
    bool select_all = false;
    if (p->current.type == TOK_STAR) {
        select_all = true;
        next_token(p);
    } else {
        /* Skip column list for now - always select all */
        while (p->current.type == TOK_IDENT) {
            next_token(p);
            if (p->current.type == TOK_COMMA) next_token(p); else break;
        }
        select_all = true;
    }

    if (!expect(p, TOK_FROM))
        return db_result_error(VOS_ERR_SYNTAX, "Expected FROM");

    if (p->current.type != TOK_IDENT)
        return db_result_error(VOS_ERR_SYNTAX, "Expected table name");

    table_schema_t *schema = db_get_schema(p->current.value);
    if (!schema) return db_result_error(VOS_ERR_NOTFOUND, "Table not found");
    next_token(p);

    where_cond_t conds[MAX_WHERE_CONDS];
    uint32_t cond_count = parse_where(p, conds);

    query_result_t *result = db_result_create(16);
    if (!result) return db_result_error(VOS_ERR_NOMEM, "Out of memory");
    result->schema = schema;

    scan_ctx_t ctx = { .result = result, .schema = schema,
                       .conditions = conds, .cond_count = cond_count };
    btree_t *index = db_get_index(schema->table_id);
    btree_scan(index, select_scan_callback, &ctx);

    (void)select_all;
    return result;
}

static query_result_t *exec_insert(parser_t *p, uint64_t pid) {
    /* INSERT INTO table (cols) VALUES (vals) */
    if (!expect(p, TOK_INTO))
        return db_result_error(VOS_ERR_SYNTAX, "Expected INTO");

    if (p->current.type != TOK_IDENT)
        return db_result_error(VOS_ERR_SYNTAX, "Expected table name");

    table_schema_t *schema = db_get_schema(p->current.value);
    if (!schema) return db_result_error(VOS_ERR_NOTFOUND, "Table not found");
    next_token(p);

    /* Parse column names */
    char col_names[MAX_INSERT_VALS][MAX_COLUMN_NAME];
    uint32_t col_count = 0;

    if (!expect(p, TOK_LPAREN))
        return db_result_error(VOS_ERR_SYNTAX, "Expected '('");

    while (p->current.type == TOK_IDENT && col_count < MAX_INSERT_VALS) {
        strncpy(col_names[col_count], p->current.value, MAX_COLUMN_NAME - 1);
        col_count++;
        next_token(p);
        if (p->current.type == TOK_COMMA) next_token(p); else break;
    }

    if (!expect(p, TOK_RPAREN))
        return db_result_error(VOS_ERR_SYNTAX, "Expected ')'");

    if (!expect(p, TOK_VALUES))
        return db_result_error(VOS_ERR_SYNTAX, "Expected VALUES");

    if (!expect(p, TOK_LPAREN))
        return db_result_error(VOS_ERR_SYNTAX, "Expected '('");

    /* Parse values */
    record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.row_id = db_next_row_id();
    rec.table_id = schema->table_id;

    /* Set primary key (first column is usually auto-increment id) */
    int pk_idx = find_column_index(schema, schema->columns[0].name);
    if (pk_idx >= 0 && schema->columns[0].primary_key) {
        record_set_u64(&rec, 0, rec.row_id);
    }

    uint32_t val_idx = 0;
    while (val_idx < col_count && p->current.type != TOK_RPAREN && p->current.type != TOK_EOF) {
        int ci = find_column_index(schema, col_names[val_idx]);
        if (ci < 0) {
            return db_result_error(VOS_ERR_INVAL, "Unknown column");
        }

        if (p->current.type == TOK_STRING) {
            record_set_str(&rec, (uint32_t)ci, p->current.value);
        } else if (p->current.type == TOK_NUMBER) {
            uint64_t v = strtou64(p->current.value, NULL, 10);
            if (schema->columns[ci].type == COL_U64)
                record_set_u64(&rec, (uint32_t)ci, v);
            else if (schema->columns[ci].type == COL_U32)
                record_set_u32(&rec, (uint32_t)ci, (uint32_t)v);
            else
                record_set_str(&rec, (uint32_t)ci, p->current.value);
        } else {
            break;
        }
        next_token(p);
        val_idx++;
        if (p->current.type == TOK_COMMA) next_token(p); else break;
    }

    rec.field_count = schema->column_count;

    /* Set owner_pid if column exists */
    int owner_idx = find_column_index(schema, "owner_pid");
    if (owner_idx >= 0) record_set_u64(&rec, (uint32_t)owner_idx, pid);

    /* Set created timestamp if column exists */
    int created_idx = find_column_index(schema, "created");
    if (created_idx >= 0) record_set_u64(&rec, (uint32_t)created_idx, pit_get_ticks());

    /* Set size if column exists */
    int size_idx = find_column_index(schema, "size");
    int data_idx = find_column_index(schema, "data");
    if (size_idx >= 0 && data_idx >= 0) {
        record_set_u64(&rec, (uint32_t)size_idx,
                        rec.fields[data_idx].str_val.length);
    }

    int err = db_insert_record(schema->table_id, &rec);
    if (err != VOS_OK) return db_result_error(err, "Insert failed");

    /* Audit log (constant-time) */
    db_audit_log(pid, "INSERT", rec.row_id, "OK");

    query_result_t *result = db_result_create(0);
    snprintf(result->error_msg, sizeof(result->error_msg),
             "1 row inserted (row_id=%llu)", rec.row_id);
    return result;
}

static void delete_scan_callback(uint64_t key, void *value, void *ctx) {
    scan_ctx_t *sc = (scan_ctx_t *)ctx;

    record_t *rec = db_decrypt_record(sc->schema->table_id, value);
    if (!rec) return; /* MAC verification failed */

    if (record_matches(rec, sc->schema, sc->conditions, sc->cond_count)) {
        /* Store decrypted copy for row_id extraction */
        db_result_add_row(sc->result, rec);
    }
    (void)key;
}

static query_result_t *exec_delete(parser_t *p, uint64_t pid) {
    /* DELETE FROM table [WHERE ...] */
    if (!expect(p, TOK_FROM))
        return db_result_error(VOS_ERR_SYNTAX, "Expected FROM");

    if (p->current.type != TOK_IDENT)
        return db_result_error(VOS_ERR_SYNTAX, "Expected table name");

    table_schema_t *schema = db_get_schema(p->current.value);
    if (!schema) return db_result_error(VOS_ERR_NOTFOUND, "Table not found");
    next_token(p);

    where_cond_t conds[MAX_WHERE_CONDS];
    uint32_t cond_count = parse_where(p, conds);

    /* Find matching rows */
    query_result_t *matches = db_result_create(16);
    scan_ctx_t ctx = { .result = matches, .schema = schema,
                       .conditions = conds, .cond_count = cond_count };
    btree_scan(db_get_index(schema->table_id), delete_scan_callback, &ctx);

    /* Delete matched rows */
    uint32_t deleted = 0;
    for (uint32_t i = 0; i < matches->row_count; i++) {
        db_delete_record(schema->table_id, matches->rows[i].row_id);
        deleted++;

        /* Audit (constant-time) */
        db_audit_log(pid, "DELETE", matches->rows[i].row_id, "OK");
    }

    db_result_free(matches);

    query_result_t *result = db_result_create(0);
    snprintf(result->error_msg, sizeof(result->error_msg),
             "%u row(s) deleted", deleted);
    return result;
}

static query_result_t *exec_update(parser_t *p, uint64_t pid) {
    /* UPDATE table SET col=val [WHERE ...] */
    if (p->current.type != TOK_IDENT)
        return db_result_error(VOS_ERR_SYNTAX, "Expected table name");

    table_schema_t *schema = db_get_schema(p->current.value);
    if (!schema) return db_result_error(VOS_ERR_NOTFOUND, "Table not found");
    next_token(p);

    if (!expect(p, TOK_SET))
        return db_result_error(VOS_ERR_SYNTAX, "Expected SET");

    /* Parse SET assignments */
    char  set_cols[MAX_INSERT_VALS][MAX_COLUMN_NAME];
    field_value_t set_vals[MAX_INSERT_VALS];
    uint32_t set_count = 0;

    while (p->current.type == TOK_IDENT && set_count < MAX_INSERT_VALS) {
        strncpy(set_cols[set_count], p->current.value, MAX_COLUMN_NAME - 1);
        next_token(p);
        if (p->current.type != TOK_EQ) break;
        next_token(p);

        if (p->current.type == TOK_STRING) {
            set_vals[set_count].type = COL_STR;
            strncpy(set_vals[set_count].str_val.data, p->current.value, MAX_STR_LEN);
            set_vals[set_count].str_val.length = (uint16_t)strlen(p->current.value);
        } else if (p->current.type == TOK_NUMBER) {
            set_vals[set_count].type = COL_U64;
            set_vals[set_count].u64_val = strtou64(p->current.value, NULL, 10);
        } else break;

        next_token(p);
        set_count++;
        if (p->current.type == TOK_COMMA) next_token(p); else break;
    }

    where_cond_t conds[MAX_WHERE_CONDS];
    uint32_t cond_count = parse_where(p, conds);

    /* Find matching rows via scan (decrypts each record) */
    query_result_t *matches = db_result_create(16);
    scan_ctx_t ctx = { .result = matches, .schema = schema,
                       .conditions = conds, .cond_count = cond_count };
    btree_scan(db_get_index(schema->table_id), select_scan_callback, &ctx);

    /* Update: decrypt → modify → re-encrypt with new IV */
    uint32_t updated = 0;
    for (uint32_t i = 0; i < matches->row_count; i++) {
        /* matches->rows[i] is already a decrypted copy */
        record_t modified = matches->rows[i];

        for (uint32_t s = 0; s < set_count; s++) {
            int ci = find_column_index(schema, set_cols[s]);
            if (ci < 0) continue;
            modified.fields[ci] = set_vals[s];
        }

        /* Re-encrypt: delete old + insert new with fresh IV */
        db_update_encrypted(schema->table_id, modified.row_id, &modified);
        updated++;
    }

    db_result_free(matches);

    /* Audit (constant-time) */
    db_audit_log(pid, "UPDATE", 0, "OK");

    query_result_t *result = db_result_create(0);
    snprintf(result->error_msg, sizeof(result->error_msg),
             "%u row(s) updated", updated);
    return result;
}

static query_result_t *exec_grant(parser_t *p, uint64_t pid) {
    /* GRANT rights ON object_id TO process_id */
    uint32_t rights = 0;
    while (p->current.type == TOK_READ || p->current.type == TOK_WRITE ||
           p->current.type == TOK_ALL || p->current.type == TOK_IDENT) {
        if (p->current.type == TOK_READ || strcasecmp(p->current.value, "READ") == 0)
            rights |= CAP_READ;
        else if (p->current.type == TOK_WRITE || strcasecmp(p->current.value, "WRITE") == 0)
            rights |= CAP_WRITE;
        else if (p->current.type == TOK_ALL || strcasecmp(p->current.value, "ALL") == 0)
            rights = CAP_ALL;
        next_token(p);
        if (p->current.type == TOK_COMMA) next_token(p);
        else break;
    }

    if (!expect(p, TOK_ON))
        return db_result_error(VOS_ERR_SYNTAX, "Expected ON");

    if (p->current.type != TOK_NUMBER)
        return db_result_error(VOS_ERR_SYNTAX, "Expected object_id");
    uint64_t object_id = strtou64(p->current.value, NULL, 10);
    next_token(p);

    if (!expect(p, TOK_TO))
        return db_result_error(VOS_ERR_SYNTAX, "Expected TO");

    if (p->current.type != TOK_NUMBER)
        return db_result_error(VOS_ERR_SYNTAX, "Expected process_id");
    uint64_t target_pid = strtou64(p->current.value, NULL, 10);

    capability_t cap = cap_create(object_id, CAP_OBJ_TABLE_ROW, target_pid, rights, 0);
    cap_table_insert(&cap);

    query_result_t *result = db_result_create(0);
    snprintf(result->error_msg, sizeof(result->error_msg),
             "Granted rights 0x%x on object %llu to pid %llu (cap_id=%llu)",
             rights, object_id, target_pid, cap.cap_id);
    return result;
}

static query_result_t *exec_revoke(parser_t *p, uint64_t pid) {
    if (p->current.type != TOK_NUMBER)
        return db_result_error(VOS_ERR_SYNTAX, "Expected cap_id");

    uint64_t cap_id = strtou64(p->current.value, NULL, 10);
    int err = cap_revoke(pid, cap_id);

    query_result_t *result = db_result_create(0);
    if (err == VOS_OK)
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Capability %llu revoked (cascade)", cap_id);
    else
        snprintf(result->error_msg, sizeof(result->error_msg),
                 "Revoke failed: %s", vos_strerror(err));
    result->error_code = err;
    return result;
}

query_result_t *query_execute(const char *input, uint64_t caller_pid) {
    parser_t p;
    p.input = input;
    p.pos = 0;
    next_token(&p);

    switch (p.current.type) {
    case TOK_SHOW:
        next_token(&p);
        if (p.current.type == TOK_TABLES) return exec_show_tables();
        return db_result_error(VOS_ERR_SYNTAX, "Expected TABLES after SHOW");

    case TOK_DESCRIBE:
        next_token(&p);
        return exec_describe(&p);

    case TOK_SELECT:
        next_token(&p);
        return exec_select(&p, caller_pid);

    case TOK_INSERT:
        next_token(&p);
        return exec_insert(&p, caller_pid);

    case TOK_DELETE:
        next_token(&p);
        return exec_delete(&p, caller_pid);

    case TOK_UPDATE:
        next_token(&p);
        return exec_update(&p, caller_pid);

    case TOK_GRANT:
        next_token(&p);
        return exec_grant(&p, caller_pid);

    case TOK_REVOKE:
        next_token(&p);
        return exec_revoke(&p, caller_pid);

    default:
        return db_result_error(VOS_ERR_SYNTAX,
            "Unknown command. Use: SELECT, INSERT, DELETE, UPDATE, SHOW TABLES, DESCRIBE, GRANT, REVOKE");
    }
}
