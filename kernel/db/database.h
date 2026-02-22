#ifndef VAULTOS_DATABASE_H
#define VAULTOS_DATABASE_H

#include "schema.h"
#include "record.h"
#include "btree.h"
#include "../crypto/aes.h"
#include "../../include/vaultos/boot_info.h"

typedef struct {
    record_t   *rows;
    uint32_t    row_count;
    uint32_t    row_capacity;
    int         error_code;
    char        error_msg[256];
    /* For SHOW TABLES / DESCRIBE */
    table_schema_t *schema;
} query_result_t;

void             db_init(void);
void             db_init_system_tables(BootInfo *boot_info);
table_schema_t  *db_get_schema(const char *table_name);
table_schema_t  *db_get_schema_by_id(uint32_t table_id);
uint32_t         db_get_table_count(void);

/* Core operations */
query_result_t  *db_execute(const char *query_str, uint64_t caller_pid);
int              db_insert_record(uint32_t table_id, record_t *rec);
int              db_delete_record(uint32_t table_id, uint64_t row_id);
record_t        *db_get_record(uint32_t table_id, uint64_t row_id);

/* Get B-tree for table */
btree_t         *db_get_index(uint32_t table_id);

/* Result management */
query_result_t  *db_result_create(uint32_t capacity);
void             db_result_add_row(query_result_t *result, const record_t *row);
void             db_result_free(query_result_t *result);
query_result_t  *db_result_error(int code, const char *msg);

/* Global row ID counter */
uint64_t         db_next_row_id(void);

/* Decrypt a record from B-tree value (for scan callbacks).
 * Returns pointer to internal static buffer, or NULL on MAC failure. */
record_t        *db_decrypt_record(uint32_t table_id, void *encrypted_value);

/* Re-encrypt a modified record back into the B-tree */
int              db_update_encrypted(uint32_t table_id, uint64_t row_id,
                                      record_t *modified);

/* Constant-time audit log (pads strings to fixed length) */
void             db_audit_log(uint64_t pid, const char *action,
                               uint64_t target_id, const char *result);

#endif /* VAULTOS_DATABASE_H */
