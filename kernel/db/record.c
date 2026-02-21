#include "record.h"
#include "../lib/string.h"

void record_set_u64(record_t *rec, uint32_t idx, uint64_t val) {
    if (idx >= MAX_COLUMNS) return;
    rec->fields[idx].type = COL_U64;
    rec->fields[idx].u64_val = val;
    if (idx >= rec->field_count) rec->field_count = idx + 1;
}

void record_set_i64(record_t *rec, uint32_t idx, int64_t val) {
    if (idx >= MAX_COLUMNS) return;
    rec->fields[idx].type = COL_I64;
    rec->fields[idx].i64_val = val;
    if (idx >= rec->field_count) rec->field_count = idx + 1;
}

void record_set_u32(record_t *rec, uint32_t idx, uint32_t val) {
    if (idx >= MAX_COLUMNS) return;
    rec->fields[idx].type = COL_U32;
    rec->fields[idx].u32_val = val;
    if (idx >= rec->field_count) rec->field_count = idx + 1;
}

void record_set_str(record_t *rec, uint32_t idx, const char *val) {
    if (idx >= MAX_COLUMNS) return;
    rec->fields[idx].type = COL_STR;
    strncpy(rec->fields[idx].str_val.data, val, MAX_STR_LEN);
    rec->fields[idx].str_val.data[MAX_STR_LEN] = '\0';
    rec->fields[idx].str_val.length = (uint16_t)strlen(rec->fields[idx].str_val.data);
    if (idx >= rec->field_count) rec->field_count = idx + 1;
}

void record_set_bool(record_t *rec, uint32_t idx, bool val) {
    if (idx >= MAX_COLUMNS) return;
    rec->fields[idx].type = COL_BOOL;
    rec->fields[idx].bool_val = val;
    if (idx >= rec->field_count) rec->field_count = idx + 1;
}

void record_set_blob(record_t *rec, uint32_t idx, const uint8_t *data, uint32_t len) {
    if (idx >= MAX_COLUMNS || len > MAX_BLOB_LEN) return;
    rec->fields[idx].type = COL_BLOB;
    memcpy(rec->fields[idx].blob_val.data, data, len);
    rec->fields[idx].blob_val.length = len;
    if (idx >= rec->field_count) rec->field_count = idx + 1;
}
