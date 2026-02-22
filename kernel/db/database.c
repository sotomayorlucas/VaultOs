#include "database.h"
#include "query.h"
#include "record_serde.h"
#include "../mm/heap.h"
#include "../crypto/hmac.h"
#include "../crypto/random.h"
#include "../cap/capability.h"
#include "../lib/printf.h"
#include "../lib/string.h"
#include "../lib/assert.h"
#include "../arch/x86_64/pit.h"
#include "../../include/vaultos/error_codes.h"

static table_schema_t schemas[MAX_TABLES];
static btree_t        indexes[MAX_TABLES];
static aes_ctx_t      table_aes_keys[MAX_TABLES];
static hmac_ctx_t     table_mac_ctxs[MAX_TABLES];
static uint32_t       table_count = 0;
static uint64_t       global_row_id = 1;

/* Master encryption key for the database */
static uint8_t master_db_key[32];

/* Shared buffers for single-threaded encrypt/decrypt pipeline */
static uint8_t   serde_buf[MAX_RECORD_SIZE];
static uint8_t   crypto_buf[MAX_RECORD_SIZE + AES_BLOCK_SIZE];
static record_t  decrypt_rec_buf;

uint64_t db_next_row_id(void) {
    return global_row_id++;
}

/*
 * Derive per-table AES and MAC keys with domain separation.
 * AES key = HMAC-SHA256(master_key, "AES" || table_id_le32) → first 16 bytes
 * MAC key = HMAC-SHA256(master_key, "MAC" || table_id_le32) → full 32 bytes
 */
static void derive_table_key(uint32_t table_id, aes_ctx_t *aes_ctx,
                               hmac_ctx_t *mac_ctx) {
    uint8_t domain[7]; /* "AES" or "MAC" + 4-byte table_id */
    uint8_t derived[32];

    /* AES key derivation */
    domain[0] = 'A'; domain[1] = 'E'; domain[2] = 'S';
    domain[3] = (table_id >> 0) & 0xFF;
    domain[4] = (table_id >> 8) & 0xFF;
    domain[5] = (table_id >> 16) & 0xFF;
    domain[6] = (table_id >> 24) & 0xFF;
    hmac_sha256(master_db_key, 32, domain, 7, derived);
    aes_init(aes_ctx, derived); /* First 16 bytes for AES-128 */

    /* MAC key derivation */
    domain[0] = 'M'; domain[1] = 'A'; domain[2] = 'C';
    hmac_sha256(master_db_key, 32, domain, 7, derived);
    hmac_ctx_init(mac_ctx, derived, 32); /* Full 32 bytes for HMAC key */

    /* Zero sensitive material */
    memset(derived, 0, sizeof(derived));
}

static int db_create_table(const table_schema_t *schema) {
    if (table_count >= MAX_TABLES) return VOS_ERR_FULL;

    schemas[table_count] = *schema;
    schemas[table_count].table_id = table_count;
    btree_init(&indexes[table_count], table_count);
    derive_table_key(table_count, &table_aes_keys[table_count],
                     &table_mac_ctxs[table_count]);

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

    kprintf("[DB] Database engine initialized (Encrypt-then-MAC enabled)\n");
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

/*
 * Encrypt-then-MAC pipeline:
 *   1. Serialize record to bytes
 *   2. PKCS7 pad to AES block boundary
 *   3. Generate random IV
 *   4. AES-CBC encrypt
 *   5. HMAC-SHA256(IV || ciphertext)
 *   6. Store encrypted_record_t in B-tree
 */
int db_insert_record(uint32_t table_id, record_t *rec) {
    if (table_id >= table_count) return VOS_ERR_INVAL;

    /* Step 1: Serialize */
    size_t plain_len = record_serialize(rec, serde_buf, sizeof(serde_buf));
    if (plain_len == 0) return VOS_ERR_INVAL;

    /* Step 2: PKCS7 pad */
    size_t padded_len = aes_padded_size(plain_len);
    if (padded_len > sizeof(crypto_buf)) return VOS_ERR_INVAL;
    memcpy(crypto_buf, serde_buf, plain_len);
    aes_pkcs7_pad(crypto_buf, plain_len, padded_len);

    /* Step 3: Allocate encrypted record */
    encrypted_record_t *enc = kzalloc(sizeof(encrypted_record_t));
    if (!enc) return VOS_ERR_NOMEM;
    enc->ciphertext = kmalloc(padded_len);
    if (!enc->ciphertext) { kfree(enc); return VOS_ERR_NOMEM; }
    enc->ciphertext_len = (uint32_t)padded_len;
    enc->row_id = rec->row_id;
    enc->table_id = table_id;

    /* Step 4: Random IV + AES-CBC encrypt */
    random_bytes(enc->iv, AES_BLOCK_SIZE);
    aes_cbc_encrypt(&table_aes_keys[table_id], enc->iv,
                     crypto_buf, enc->ciphertext, padded_len);

    /* Step 5: HMAC-SHA256(IV || ciphertext) */
    {
        /* Build MAC input: IV(16) + ciphertext */
        uint8_t mac_input_hdr[AES_BLOCK_SIZE];
        memcpy(mac_input_hdr, enc->iv, AES_BLOCK_SIZE);

        /* Use two-pass: hash IV then ciphertext via SHA-256 streaming.
         * Since hmac_ctx_compute takes a single buffer, concatenate. */
        size_t mac_input_len = AES_BLOCK_SIZE + padded_len;
        uint8_t *mac_input = kmalloc(mac_input_len);
        if (!mac_input) {
            kfree(enc->ciphertext);
            kfree(enc);
            return VOS_ERR_NOMEM;
        }
        memcpy(mac_input, enc->iv, AES_BLOCK_SIZE);
        memcpy(mac_input + AES_BLOCK_SIZE, enc->ciphertext, padded_len);
        hmac_ctx_compute(&table_mac_ctxs[table_id], mac_input, mac_input_len,
                          enc->mac);
        /* Zero and free MAC input */
        memset(mac_input, 0, mac_input_len);
        kfree(mac_input);
    }

    /* Step 6: Store in B-tree */
    btree_insert(&indexes[table_id], rec->row_id, enc);

    /* Zero plaintext from shared buffers */
    memset(serde_buf, 0, plain_len);
    memset(crypto_buf, 0, padded_len);

    return VOS_OK;
}

/*
 * Verify-then-decrypt pipeline:
 *   1. Verify HMAC (constant-time)
 *   2. AES-CBC decrypt
 *   3. PKCS7 unpad
 *   4. Deserialize to record
 */
record_t *db_decrypt_record(uint32_t table_id, void *encrypted_value) {
    if (!encrypted_value || table_id >= table_count) return NULL;
    encrypted_record_t *enc = (encrypted_record_t *)encrypted_value;

    /* Step 1: Verify HMAC */
    uint8_t computed_mac[32];
    {
        size_t mac_input_len = AES_BLOCK_SIZE + enc->ciphertext_len;
        uint8_t *mac_input = kmalloc(mac_input_len);
        if (!mac_input) return NULL;
        memcpy(mac_input, enc->iv, AES_BLOCK_SIZE);
        memcpy(mac_input + AES_BLOCK_SIZE, enc->ciphertext, enc->ciphertext_len);
        hmac_ctx_compute(&table_mac_ctxs[table_id], mac_input, mac_input_len,
                          computed_mac);
        memset(mac_input, 0, mac_input_len);
        kfree(mac_input);
    }

    if (!hmac_verify(enc->mac, computed_mac, 32)) {
        kprintf("[DB] MAC verification failed for row %llu in table %u!\n",
                enc->row_id, table_id);
        memset(computed_mac, 0, sizeof(computed_mac));
        return NULL;
    }
    memset(computed_mac, 0, sizeof(computed_mac));

    /* Step 2: AES-CBC decrypt */
    if (enc->ciphertext_len > sizeof(crypto_buf)) return NULL;
    aes_cbc_decrypt(&table_aes_keys[table_id], enc->iv,
                     enc->ciphertext, crypto_buf, enc->ciphertext_len);

    /* Step 3: PKCS7 unpad */
    size_t plain_len = aes_pkcs7_unpad(crypto_buf, enc->ciphertext_len);
    if (plain_len == 0) {
        memset(crypto_buf, 0, enc->ciphertext_len);
        return NULL;
    }

    /* Step 4: Deserialize */
    size_t consumed = record_deserialize(&decrypt_rec_buf, crypto_buf, plain_len);
    memset(crypto_buf, 0, enc->ciphertext_len);

    if (consumed == 0) return NULL;
    return &decrypt_rec_buf;
}

record_t *db_get_record(uint32_t table_id, uint64_t row_id) {
    if (table_id >= table_count) return NULL;
    void *value = btree_search(&indexes[table_id], row_id);
    if (!value) return NULL;
    return db_decrypt_record(table_id, value);
}

int db_delete_record(uint32_t table_id, uint64_t row_id) {
    if (table_id >= table_count) return VOS_ERR_INVAL;

    encrypted_record_t *enc = btree_search(&indexes[table_id], row_id);
    if (!enc) return VOS_ERR_NOTFOUND;

    btree_delete(&indexes[table_id], row_id);

    /* Zero and free encrypted data */
    if (enc->ciphertext) {
        memset(enc->ciphertext, 0, enc->ciphertext_len);
        kfree(enc->ciphertext);
    }
    memset(enc, 0, sizeof(*enc));
    kfree(enc);

    return VOS_OK;
}

int db_update_encrypted(uint32_t table_id, uint64_t row_id,
                         record_t *modified) {
    if (table_id >= table_count) return VOS_ERR_INVAL;

    /* Delete old encrypted record */
    int err = db_delete_record(table_id, row_id);
    if (err != VOS_OK) return err;

    /* Re-insert with new IV and MAC */
    modified->row_id = row_id;
    modified->table_id = table_id;
    return db_insert_record(table_id, modified);
}

/*
 * Constant-time audit logging.
 * Pads action and result strings to MAX_STR_LEN so timing is independent
 * of string content length.
 */
void db_audit_log(uint64_t pid, const char *action,
                   uint64_t target_id, const char *result_str) {
    char padded_action[MAX_STR_LEN + 1];
    char padded_result[MAX_STR_LEN + 1];

    /* Zero-fill then copy (constant-time pattern: always write full buffer) */
    memset(padded_action, 0, sizeof(padded_action));
    memset(padded_result, 0, sizeof(padded_result));

    size_t alen = strlen(action);
    size_t rlen = strlen(result_str);
    if (alen > MAX_STR_LEN) alen = MAX_STR_LEN;
    if (rlen > MAX_STR_LEN) rlen = MAX_STR_LEN;
    memcpy(padded_action, action, alen);
    memcpy(padded_result, result_str, rlen);

    record_t audit;
    memset(&audit, 0, sizeof(audit));
    audit.row_id = db_next_row_id();
    audit.table_id = TABLE_ID_AUDIT;
    audit.field_count = 6;
    record_set_u64(&audit, 0, audit.row_id);
    record_set_u64(&audit, 1, pit_get_ticks());
    record_set_u64(&audit, 2, pid);
    record_set_str(&audit, 3, padded_action);
    audit.fields[3].str_val.length = MAX_STR_LEN;  /* fixed-length serialization */
    record_set_u64(&audit, 4, target_id);
    record_set_str(&audit, 5, padded_result);
    audit.fields[5].str_val.length = MAX_STR_LEN;  /* fixed-length serialization */
    db_insert_record(TABLE_ID_AUDIT, &audit);

    /* Zero sensitive buffers */
    memset(padded_action, 0, sizeof(padded_action));
    memset(padded_result, 0, sizeof(padded_result));
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
