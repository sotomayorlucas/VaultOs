#include "database.h"
#include "query.h"
#include "../mm/heap.h"
#include "../crypto/hmac.h"
#include "../crypto/random.h"
#include "../cap/capability.h"
#include "../lib/printf.h"
#include "../lib/string.h"
#include "../lib/assert.h"
#include "../../include/vaultos/error_codes.h"

static table_schema_t schemas[MAX_TABLES];
static btree_t        indexes[MAX_TABLES];
static aes_ctx_t      table_keys[MAX_TABLES];
static uint32_t       table_count = 0;
static uint64_t       global_row_id = 1;

/* Master encryption key for the database */
static uint8_t master_db_key[32];

uint64_t db_next_row_id(void) {
    return global_row_id++;
}

static void derive_table_key(uint32_t table_id, aes_ctx_t *ctx) {
    uint8_t derived[32];
    uint8_t tid_buf[4];
    tid_buf[0] = (table_id >> 0) & 0xFF;
    tid_buf[1] = (table_id >> 8) & 0xFF;
    tid_buf[2] = (table_id >> 16) & 0xFF;
    tid_buf[3] = (table_id >> 24) & 0xFF;

    hmac_sha256(master_db_key, 32, tid_buf, 4, derived);
    aes_init(ctx, derived); /* Use first 16 bytes of HMAC output */
}

static int db_create_table(const table_schema_t *schema) {
    if (table_count >= MAX_TABLES) return VOS_ERR_FULL;

    schemas[table_count] = *schema;
    schemas[table_count].table_id = table_count;
    btree_init(&indexes[table_count], table_count);
    derive_table_key(table_count, &table_keys[table_count]);

    kprintf("[DB] Created table '%s' (id=%u, cols=%u, encrypted=%s)\n",
            schema->name, table_count, schema->column_count,
            schema->encrypted ? "yes" : "no");

    table_count++;
    return VOS_OK;
}

void db_init(void) {
    random_bytes(master_db_key, sizeof(master_db_key));
    memset(schemas, 0, sizeof(schemas));
    table_count = 0;
    global_row_id = 1;

    kprintf("[DB] Database engine initialized\n");
}

void db_init_system_tables(BootInfo *boot_info) {
    /* TABLE 0: SystemTable */
    table_schema_t sys = { .name = "SystemTable", .encrypted = true, .system_table = true, .column_count = 4 };
    strcpy(sys.columns[0].name, "id");      sys.columns[0].type = COL_U64; sys.columns[0].primary_key = true;
    strcpy(sys.columns[1].name, "key");     sys.columns[1].type = COL_STR; sys.columns[1].not_null = true;
    strcpy(sys.columns[2].name, "value");   sys.columns[2].type = COL_STR;
    strcpy(sys.columns[3].name, "created"); sys.columns[3].type = COL_U64;
    db_create_table(&sys);

    /* TABLE 1: ProcessTable */
    table_schema_t proc = { .name = "ProcessTable", .encrypted = true, .system_table = true, .column_count = 6 };
    strcpy(proc.columns[0].name, "pid");      proc.columns[0].type = COL_U64; proc.columns[0].primary_key = true;
    strcpy(proc.columns[1].name, "name");     proc.columns[1].type = COL_STR;
    strcpy(proc.columns[2].name, "state");    proc.columns[2].type = COL_STR;
    strcpy(proc.columns[3].name, "priority"); proc.columns[3].type = COL_U32;
    strcpy(proc.columns[4].name, "cap_root"); proc.columns[4].type = COL_U64;
    strcpy(proc.columns[5].name, "created");  proc.columns[5].type = COL_U64;
    db_create_table(&proc);

    /* TABLE 2: CapabilityTable */
    table_schema_t cap = { .name = "CapabilityTable", .encrypted = true, .system_table = true, .column_count = 7 };
    strcpy(cap.columns[0].name, "cap_id");    cap.columns[0].type = COL_U64; cap.columns[0].primary_key = true;
    strcpy(cap.columns[1].name, "object_id"); cap.columns[1].type = COL_U64;
    strcpy(cap.columns[2].name, "owner_pid"); cap.columns[2].type = COL_U64;
    strcpy(cap.columns[3].name, "rights");    cap.columns[3].type = COL_U32;
    strcpy(cap.columns[4].name, "parent_id"); cap.columns[4].type = COL_U64;
    strcpy(cap.columns[5].name, "revoked");   cap.columns[5].type = COL_BOOL;
    strcpy(cap.columns[6].name, "created");   cap.columns[6].type = COL_U64;
    db_create_table(&cap);

    /* TABLE 3: ObjectTable */
    table_schema_t obj = { .name = "ObjectTable", .encrypted = true, .system_table = false, .column_count = 7 };
    strcpy(obj.columns[0].name, "obj_id");    obj.columns[0].type = COL_U64; obj.columns[0].primary_key = true;
    strcpy(obj.columns[1].name, "name");      obj.columns[1].type = COL_STR; obj.columns[1].not_null = true;
    strcpy(obj.columns[2].name, "type");      obj.columns[2].type = COL_STR;
    strcpy(obj.columns[3].name, "data");      obj.columns[3].type = COL_STR;
    strcpy(obj.columns[4].name, "owner_pid"); obj.columns[4].type = COL_U64;
    strcpy(obj.columns[5].name, "size");      obj.columns[5].type = COL_U64;
    strcpy(obj.columns[6].name, "created");   obj.columns[6].type = COL_U64;
    db_create_table(&obj);

    /* TABLE 4: MessageTable */
    table_schema_t msg = { .name = "MessageTable", .encrypted = true, .system_table = true, .column_count = 6 };
    strcpy(msg.columns[0].name, "msg_id");    msg.columns[0].type = COL_U64; msg.columns[0].primary_key = true;
    strcpy(msg.columns[1].name, "src_pid");   msg.columns[1].type = COL_U64;
    strcpy(msg.columns[2].name, "dst_pid");   msg.columns[2].type = COL_U64;
    strcpy(msg.columns[3].name, "type");      msg.columns[3].type = COL_STR;
    strcpy(msg.columns[4].name, "payload");   msg.columns[4].type = COL_STR;
    strcpy(msg.columns[5].name, "delivered"); msg.columns[5].type = COL_BOOL;
    db_create_table(&msg);

    /* TABLE 5: AuditTable */
    table_schema_t audit = { .name = "AuditTable", .encrypted = true, .system_table = true, .column_count = 6 };
    strcpy(audit.columns[0].name, "audit_id");  audit.columns[0].type = COL_U64; audit.columns[0].primary_key = true;
    strcpy(audit.columns[1].name, "timestamp"); audit.columns[1].type = COL_U64;
    strcpy(audit.columns[2].name, "pid");       audit.columns[2].type = COL_U64;
    strcpy(audit.columns[3].name, "action");    audit.columns[3].type = COL_STR;
    strcpy(audit.columns[4].name, "target_id"); audit.columns[4].type = COL_U64;
    strcpy(audit.columns[5].name, "result");    audit.columns[5].type = COL_STR;
    db_create_table(&audit);

    /* Insert boot metadata into SystemTable */
    record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.row_id = db_next_row_id();
    rec.table_id = TABLE_ID_SYSTEM;
    record_set_u64(&rec, 0, rec.row_id);
    record_set_str(&rec, 1, "os.name");
    record_set_str(&rec, 2, "VaultOS");
    record_set_u64(&rec, 3, 0);
    db_insert_record(TABLE_ID_SYSTEM, &rec);

    memset(&rec, 0, sizeof(rec));
    rec.row_id = db_next_row_id();
    rec.table_id = TABLE_ID_SYSTEM;
    record_set_u64(&rec, 0, rec.row_id);
    record_set_str(&rec, 1, "os.version");
    record_set_str(&rec, 2, "0.1.0");
    record_set_u64(&rec, 3, 0);
    db_insert_record(TABLE_ID_SYSTEM, &rec);

    memset(&rec, 0, sizeof(rec));
    rec.row_id = db_next_row_id();
    rec.table_id = TABLE_ID_SYSTEM;
    record_set_u64(&rec, 0, rec.row_id);
    record_set_str(&rec, 1, "os.philosophy");
    record_set_str(&rec, 2, "Everything is a database and all data is confidential");
    record_set_u64(&rec, 3, 0);
    db_insert_record(TABLE_ID_SYSTEM, &rec);

    kprintf("[DB] System tables initialized (%u tables)\n", table_count);
    (void)boot_info;
}

table_schema_t *db_get_schema(const char *table_name) {
    for (uint32_t i = 0; i < table_count; i++) {
        if (strcasecmp(schemas[i].name, table_name) == 0) {
            return &schemas[i];
        }
    }
    return NULL;
}

table_schema_t *db_get_schema_by_id(uint32_t table_id) {
    if (table_id >= table_count) return NULL;
    return &schemas[table_id];
}

uint32_t db_get_table_count(void) {
    return table_count;
}

btree_t *db_get_index(uint32_t table_id) {
    if (table_id >= table_count) return NULL;
    return &indexes[table_id];
}

int db_insert_record(uint32_t table_id, record_t *rec) {
    if (table_id >= table_count) return VOS_ERR_INVAL;

    /* Allocate persistent copy */
    record_t *stored = kmalloc(sizeof(record_t));
    if (!stored) return VOS_ERR_NOMEM;
    *stored = *rec;

    btree_insert(&indexes[table_id], rec->row_id, stored);
    return VOS_OK;
}

int db_delete_record(uint32_t table_id, uint64_t row_id) {
    if (table_id >= table_count) return VOS_ERR_INVAL;

    record_t *rec = btree_search(&indexes[table_id], row_id);
    if (!rec) return VOS_ERR_NOTFOUND;

    btree_delete(&indexes[table_id], row_id);
    kfree(rec);
    return VOS_OK;
}

record_t *db_get_record(uint32_t table_id, uint64_t row_id) {
    if (table_id >= table_count) return NULL;
    return btree_search(&indexes[table_id], row_id);
}

query_result_t *db_result_create(uint32_t capacity) {
    query_result_t *r = kzalloc(sizeof(query_result_t));
    if (!r) return NULL;
    if (capacity > 0) {
        r->rows = kmalloc(sizeof(record_t) * capacity);
        if (!r->rows) { kfree(r); return NULL; }
    }
    r->row_capacity = capacity;
    r->row_count = 0;
    r->error_code = VOS_OK;
    return r;
}

void db_result_add_row(query_result_t *result, const record_t *row) {
    if (result->row_count >= result->row_capacity) {
        /* Grow */
        uint32_t new_cap = result->row_capacity ? result->row_capacity * 2 : 16;
        record_t *new_rows = krealloc(result->rows, sizeof(record_t) * new_cap);
        if (!new_rows) return;
        result->rows = new_rows;
        result->row_capacity = new_cap;
    }
    result->rows[result->row_count++] = *row;
}

void db_result_free(query_result_t *result) {
    if (!result) return;
    if (result->rows) kfree(result->rows);
    kfree(result);
}

query_result_t *db_result_error(int code, const char *msg) {
    query_result_t *r = db_result_create(0);
    if (!r) return NULL;
    r->error_code = code;
    strncpy(r->error_msg, msg, sizeof(r->error_msg) - 1);
    return r;
}

query_result_t *db_execute(const char *query_str, uint64_t caller_pid) {
    return query_execute(query_str, caller_pid);
}
