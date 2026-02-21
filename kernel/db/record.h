#ifndef VAULTOS_RECORD_H
#define VAULTOS_RECORD_H

#include "../../include/vaultos/db_types.h"
#include "schema.h"

/* A field value (tagged union) */
typedef struct {
    column_type_t type;
    union {
        uint64_t u64_val;
        int64_t  i64_val;
        uint32_t u32_val;
        uint8_t  u8_val;
        bool     bool_val;
        struct { char data[MAX_STR_LEN + 1]; uint16_t length; } str_val;
        struct { uint8_t data[MAX_BLOB_LEN]; uint32_t length; } blob_val;
    };
} field_value_t;

/* A complete record (row) */
typedef struct {
    uint64_t      row_id;
    uint32_t      table_id;
    field_value_t fields[MAX_COLUMNS];
    uint32_t      field_count;
} record_t;

/* Set field helpers */
void record_set_u64(record_t *rec, uint32_t idx, uint64_t val);
void record_set_i64(record_t *rec, uint32_t idx, int64_t val);
void record_set_u32(record_t *rec, uint32_t idx, uint32_t val);
void record_set_str(record_t *rec, uint32_t idx, const char *val);
void record_set_bool(record_t *rec, uint32_t idx, bool val);
void record_set_blob(record_t *rec, uint32_t idx, const uint8_t *data, uint32_t len);

#endif /* VAULTOS_RECORD_H */
