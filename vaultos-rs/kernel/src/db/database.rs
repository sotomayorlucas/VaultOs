// Database engine for VaultOS-RS
// Full port of kernel/db/database.c — Encrypt-then-MAC pipeline
//
// All data is encrypted per-table with AES-128-CBC and authenticated
// with HMAC-SHA256. Keys are derived from a master key via HMAC-based
// domain separation.

use alloc::boxed::Box;
use alloc::vec;
use alloc::vec::Vec;

use crate::crypto::aes::{AesCtx, aes_init, aes_cbc_encrypt, aes_cbc_decrypt,
                          aes_padded_size, aes_pkcs7_pad, aes_pkcs7_unpad, AES_BLOCK_SIZE};
use crate::crypto::hmac::{HmacCtx, hmac_ctx_init, hmac_ctx_compute, hmac_sha256, hmac_verify};
use crate::crypto::random::random_bytes;
use crate::db::btree::{Btree, btree_init, btree_insert, btree_search, btree_delete, btree_scan};
use crate::db::record::{Record, EncryptedRecord, FieldValue};
use crate::db::record_serde::{record_serialize, record_deserialize};
use crate::db::schema::{TableSchema, ColumnDef};
use crate::storage::db_persist;
use vaultos_shared::db_types::*;
use vaultos_shared::error_codes::*;

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------

static mut SCHEMAS: [Option<TableSchema>; MAX_TABLES] = [const { None }; MAX_TABLES];
static mut INDEXES: [Option<Btree>; MAX_TABLES] = [const { None }; MAX_TABLES];
static mut TABLE_AES_KEYS: [Option<AesCtx>; MAX_TABLES] = [const { None }; MAX_TABLES];
static mut TABLE_MAC_CTXS: [Option<HmacCtx>; MAX_TABLES] = [const { None }; MAX_TABLES];
static mut TABLE_COUNT: u32 = 0;
static mut GLOBAL_ROW_ID: u64 = 1;
static mut MASTER_DB_KEY: [u8; 32] = [0u8; 32];

// Shared single-threaded buffers for encrypt/decrypt pipeline
static mut SERDE_BUF: [u8; MAX_RECORD_SIZE] = [0u8; MAX_RECORD_SIZE];
static mut CRYPTO_BUF: [u8; MAX_RECORD_SIZE + 16] = [0u8; { MAX_RECORD_SIZE + 16 }];

// ---------------------------------------------------------------------------
// Key derivation (byte-identical to C)
// ---------------------------------------------------------------------------

/// Derive per-table AES and MAC keys with domain separation.
///   AES key = HMAC-SHA256(master_key, "AES" || table_id_le32) -> first 16 bytes
///   MAC key = HMAC-SHA256(master_key, "MAC" || table_id_le32) -> full 32 bytes
fn derive_table_key(table_id: u32) {
    let mut domain = [0u8; 7]; // "AES" or "MAC" + 4-byte table_id LE
    let mut derived = [0u8; 32];

    // AES key derivation
    domain[0] = b'A'; domain[1] = b'E'; domain[2] = b'S';
    domain[3] = ((table_id >> 0) & 0xFF) as u8;
    domain[4] = ((table_id >> 8) & 0xFF) as u8;
    domain[5] = ((table_id >> 16) & 0xFF) as u8;
    domain[6] = ((table_id >> 24) & 0xFF) as u8;

    unsafe {
        hmac_sha256(&MASTER_DB_KEY, &domain, &mut derived);
        // First 16 bytes for AES-128
        let mut aes_key = [0u8; 16];
        aes_key.copy_from_slice(&derived[..16]);
        let mut ctx = AesCtx { round_key: [0u8; 176] };
        aes_init(&mut ctx, &aes_key);
        TABLE_AES_KEYS[table_id as usize] = Some(ctx);
    }

    // MAC key derivation
    domain[0] = b'M'; domain[1] = b'A'; domain[2] = b'C';

    unsafe {
        hmac_sha256(&MASTER_DB_KEY, &domain, &mut derived);
        // Full 32 bytes for HMAC key
        let mut mac_ctx = HmacCtx::new();
        hmac_ctx_init(&mut mac_ctx, &derived);
        TABLE_MAC_CTXS[table_id as usize] = Some(mac_ctx);
    }

    // Zero sensitive material
    for b in derived.iter_mut() { *b = 0; }
}

// ---------------------------------------------------------------------------
// Table creation (internal)
// ---------------------------------------------------------------------------

fn db_create_table_impl(schema: &TableSchema, verbose: bool) -> i32 {
    unsafe {
        if TABLE_COUNT >= MAX_TABLES as u32 {
            return VOS_ERR_FULL;
        }

        let id = TABLE_COUNT;
        let mut s = schema.clone();
        s.table_id = id;
        SCHEMAS[id as usize] = Some(s);

        let mut tree = Btree {
            root: core::ptr::null_mut(),
            count: 0,
            table_id: id,
        };
        btree_init(&mut tree, id);
        INDEXES[id as usize] = Some(tree);

        derive_table_key(id);

        if verbose {
            crate::serial_println!("[DB] Created table (encrypted)");
        }

        TABLE_COUNT += 1;
    }
    VOS_OK
}

pub fn db_create_table(schema: &TableSchema) -> i32 {
    db_create_table_impl(schema, true)
}

// ---------------------------------------------------------------------------
// Helper: build a column def
// ---------------------------------------------------------------------------

fn make_col(name: &str, col_type: ColumnType, pk: bool, nn: bool) -> ColumnDef {
    let mut c = ColumnDef::zeroed();
    let bytes = name.as_bytes();
    let len = bytes.len().min(MAX_COLUMN_NAME - 1);
    c.name[..len].copy_from_slice(&bytes[..len]);
    c.col_type = col_type;
    c.primary_key = pk;
    c.not_null = nn;
    c
}

// ---------------------------------------------------------------------------
// System table schema registration
// ---------------------------------------------------------------------------

fn register_table_schemas(verbose: bool) {
    // TABLE 0: SystemTable
    {
        let mut s = TableSchema::zeroed();
        s.set_name("SystemTable");
        s.encrypted = true;
        s.system_table = true;
        s.column_count = 4;
        s.columns[0] = make_col("id", ColumnType::U64, true, false);
        s.columns[1] = make_col("key", ColumnType::Str, false, true);
        s.columns[2] = make_col("value", ColumnType::Str, false, false);
        s.columns[3] = make_col("created", ColumnType::U64, false, false);
        db_create_table_impl(&s, verbose);
    }

    // TABLE 1: ProcessTable
    {
        let mut s = TableSchema::zeroed();
        s.set_name("ProcessTable");
        s.encrypted = true;
        s.system_table = true;
        s.column_count = 6;
        s.columns[0] = make_col("pid", ColumnType::U64, true, false);
        s.columns[1] = make_col("name", ColumnType::Str, false, false);
        s.columns[2] = make_col("state", ColumnType::Str, false, false);
        s.columns[3] = make_col("priority", ColumnType::U32, false, false);
        s.columns[4] = make_col("cap_root", ColumnType::U64, false, false);
        s.columns[5] = make_col("created", ColumnType::U64, false, false);
        db_create_table_impl(&s, verbose);
    }

    // TABLE 2: CapabilityTable
    {
        let mut s = TableSchema::zeroed();
        s.set_name("CapabilityTable");
        s.encrypted = true;
        s.system_table = true;
        s.column_count = 7;
        s.columns[0] = make_col("cap_id", ColumnType::U64, true, false);
        s.columns[1] = make_col("object_id", ColumnType::U64, false, false);
        s.columns[2] = make_col("owner_pid", ColumnType::U64, false, false);
        s.columns[3] = make_col("rights", ColumnType::U32, false, false);
        s.columns[4] = make_col("parent_id", ColumnType::U64, false, false);
        s.columns[5] = make_col("revoked", ColumnType::Bool, false, false);
        s.columns[6] = make_col("created", ColumnType::U64, false, false);
        db_create_table_impl(&s, verbose);
    }

    // TABLE 3: ObjectTable
    {
        let mut s = TableSchema::zeroed();
        s.set_name("ObjectTable");
        s.encrypted = true;
        s.system_table = false;
        s.column_count = 7;
        s.columns[0] = make_col("obj_id", ColumnType::U64, true, false);
        s.columns[1] = make_col("name", ColumnType::Str, false, true);
        s.columns[2] = make_col("type", ColumnType::Str, false, false);
        s.columns[3] = make_col("data", ColumnType::Str, false, false);
        s.columns[4] = make_col("owner_pid", ColumnType::U64, false, false);
        s.columns[5] = make_col("size", ColumnType::U64, false, false);
        s.columns[6] = make_col("created", ColumnType::U64, false, false);
        db_create_table_impl(&s, verbose);
    }

    // TABLE 4: MessageTable
    {
        let mut s = TableSchema::zeroed();
        s.set_name("MessageTable");
        s.encrypted = true;
        s.system_table = true;
        s.column_count = 6;
        s.columns[0] = make_col("msg_id", ColumnType::U64, true, false);
        s.columns[1] = make_col("src_pid", ColumnType::U64, false, false);
        s.columns[2] = make_col("dst_pid", ColumnType::U64, false, false);
        s.columns[3] = make_col("type", ColumnType::Str, false, false);
        s.columns[4] = make_col("payload", ColumnType::Str, false, false);
        s.columns[5] = make_col("delivered", ColumnType::Bool, false, false);
        db_create_table_impl(&s, verbose);
    }

    // TABLE 5: AuditTable
    {
        let mut s = TableSchema::zeroed();
        s.set_name("AuditTable");
        s.encrypted = true;
        s.system_table = true;
        s.column_count = 6;
        s.columns[0] = make_col("audit_id", ColumnType::U64, true, false);
        s.columns[1] = make_col("timestamp", ColumnType::U64, false, false);
        s.columns[2] = make_col("pid", ColumnType::U64, false, false);
        s.columns[3] = make_col("action", ColumnType::Str, false, false);
        s.columns[4] = make_col("target_id", ColumnType::U64, false, false);
        s.columns[5] = make_col("result", ColumnType::Str, false, false);
        db_create_table_impl(&s, verbose);
    }
}

// ---------------------------------------------------------------------------
// Public API: initialization
// ---------------------------------------------------------------------------

/// Zero all state. Master key must be set separately.
pub fn db_init() {
    unsafe {
        MASTER_DB_KEY = [0u8; 32];
        for i in 0..MAX_TABLES {
            SCHEMAS[i] = None;
            INDEXES[i] = None;
            TABLE_AES_KEYS[i] = None;
            TABLE_MAC_CTXS[i] = None;
        }
        TABLE_COUNT = 0;
        GLOBAL_ROW_ID = 1;
    }
    crate::serial_println!("[DB] Database engine initialized (Encrypt-then-MAC enabled)");
}

/// Generate a random 32-byte master key.
pub fn db_generate_master_key() {
    let mut key = [0u8; 32];
    random_bytes(&mut key);
    unsafe { MASTER_DB_KEY.copy_from_slice(&key); }
}

pub fn db_set_master_key(key: &[u8; 32]) {
    unsafe { MASTER_DB_KEY.copy_from_slice(key); }
}

pub fn db_get_master_key(out: &mut [u8; 32]) {
    unsafe { out.copy_from_slice(&MASTER_DB_KEY); }
}

/// Re-derive all per-table AES/MAC keys from current master key.
pub fn db_rederive_table_keys() {
    unsafe {
        for i in 0..TABLE_COUNT {
            derive_table_key(i);
        }
    }
}

/// Cold boot: register schemas, derive keys, init btrees, insert boot metadata.
pub fn db_init_system_tables() {
    register_table_schemas(true);
    db_insert_boot_metadata();
    unsafe {
        crate::serial_println!("[DB] System tables initialized");
    }
}

/// Warm boot: register schemas + derive keys WITHOUT inserting data.
pub fn db_register_system_tables() {
    register_table_schemas(false);
    unsafe {
        crate::serial_println!("[DB] Registered system table schemas (warm boot)");
    }
}

/// Insert initial boot metadata into SystemTable (cold boot).
pub fn db_insert_boot_metadata() {
    // os.name
    {
        let mut rec = Record::new(TABLE_ID_SYSTEM);
        rec.row_id = db_next_row_id();
        rec.table_id = TABLE_ID_SYSTEM;
        rec.field_count = 4;
        rec.set_u64(0, rec.row_id);
        rec.set_str(1, "os.name");
        rec.set_str(2, "VaultOS");
        rec.set_u64(3, 0);
        db_insert_record(TABLE_ID_SYSTEM, &mut rec);
    }

    // os.version
    {
        let mut rec = Record::new(TABLE_ID_SYSTEM);
        rec.row_id = db_next_row_id();
        rec.table_id = TABLE_ID_SYSTEM;
        rec.field_count = 4;
        rec.set_u64(0, rec.row_id);
        rec.set_str(1, "os.version");
        rec.set_str(2, "0.1.0");
        rec.set_u64(3, 0);
        db_insert_record(TABLE_ID_SYSTEM, &mut rec);
    }

    // os.philosophy
    {
        let mut rec = Record::new(TABLE_ID_SYSTEM);
        rec.row_id = db_next_row_id();
        rec.table_id = TABLE_ID_SYSTEM;
        rec.field_count = 4;
        rec.set_u64(0, rec.row_id);
        rec.set_str(1, "os.philosophy");
        rec.set_str(2, "Everything is a database and all data is confidential");
        rec.set_u64(3, 0);
        db_insert_record(TABLE_ID_SYSTEM, &mut rec);
    }

    crate::serial_println!("[DB] Boot metadata inserted (3 records)");
}

// ---------------------------------------------------------------------------
// Schema lookup
// ---------------------------------------------------------------------------

pub fn db_get_schema_by_name(name: &str) -> Option<&'static TableSchema> {
    unsafe {
        for i in 0..TABLE_COUNT as usize {
            if let Some(ref s) = SCHEMAS[i] {
                if str_eq_ignore_case(s.name_str(), name) {
                    return SCHEMAS[i].as_ref();
                }
            }
        }
    }
    None
}

pub fn db_get_schema_by_id(id: u32) -> Option<&'static TableSchema> {
    unsafe {
        if id >= TABLE_COUNT { return None; }
        SCHEMAS[id as usize].as_ref()
    }
}

pub fn db_get_table_count() -> u32 {
    unsafe { TABLE_COUNT }
}

pub fn db_set_table_count(count: u32) {
    unsafe { TABLE_COUNT = count; }
}

// ---------------------------------------------------------------------------
// B-tree index access
// ---------------------------------------------------------------------------

pub fn db_get_index(table_id: u32) -> Option<&'static mut Btree> {
    unsafe {
        if table_id >= TABLE_COUNT { return None; }
        INDEXES[table_id as usize].as_mut()
    }
}

pub fn db_set_index(table_id: u32, tree: Btree) {
    if (table_id as usize) < MAX_TABLES {
        unsafe { INDEXES[table_id as usize] = Some(tree); }
    }
}

// ---------------------------------------------------------------------------
// Row ID management
// ---------------------------------------------------------------------------

pub fn db_next_row_id() -> u64 {
    unsafe {
        let id = GLOBAL_ROW_ID;
        GLOBAL_ROW_ID += 1;
        id
    }
}

pub fn db_get_global_row_id() -> u64 {
    unsafe { GLOBAL_ROW_ID }
}

pub fn db_set_global_row_id(row_id: u64) {
    unsafe { GLOBAL_ROW_ID = row_id; }
}

// ---------------------------------------------------------------------------
// Encrypt-then-MAC pipeline
// ---------------------------------------------------------------------------

/// Insert a record: serialize -> pad -> encrypt -> MAC -> store in btree.
pub fn db_insert_record(table_id: u32, rec: &mut Record) -> i32 {
    unsafe {
        if table_id >= TABLE_COUNT { return VOS_ERR_INVAL; }

        // Step 1: Serialize
        let plain_len = record_serialize(rec, &mut SERDE_BUF);
        if plain_len == 0 { return VOS_ERR_INVAL; }

        // Step 2: PKCS7 pad
        let padded_len = aes_padded_size(plain_len);
        if padded_len > CRYPTO_BUF.len() { return VOS_ERR_INVAL; }
        CRYPTO_BUF[..plain_len].copy_from_slice(&SERDE_BUF[..plain_len]);
        aes_pkcs7_pad(&mut CRYPTO_BUF, plain_len, padded_len);

        // Step 3: Allocate encrypted record
        let mut enc = Box::new(EncryptedRecord::new());
        enc.ciphertext = vec![0u8; padded_len];
        enc.ciphertext_len = padded_len as u32;
        enc.row_id = rec.row_id;
        enc.table_id = table_id;

        // Step 4: Random IV + AES-CBC encrypt
        random_bytes(&mut enc.iv);
        let aes_ctx = match TABLE_AES_KEYS[table_id as usize].as_ref() {
            Some(c) => c,
            None => return VOS_ERR_INVAL,
        };
        aes_cbc_encrypt(aes_ctx, &enc.iv, &CRYPTO_BUF[..padded_len],
                         &mut enc.ciphertext, padded_len);

        // Step 5: HMAC-SHA256(IV || ciphertext)
        let mac_ctx = match TABLE_MAC_CTXS[table_id as usize].as_ref() {
            Some(c) => c,
            None => return VOS_ERR_INVAL,
        };
        {
            let mac_input_len = match AES_BLOCK_SIZE.checked_add(padded_len) {
                Some(len) => len,
                None => return VOS_ERR_INVAL, // Overflow
            };
            let mut mac_input = vec![0u8; mac_input_len];
            mac_input[..AES_BLOCK_SIZE].copy_from_slice(&enc.iv);
            mac_input[AES_BLOCK_SIZE..].copy_from_slice(&enc.ciphertext[..padded_len]);
            hmac_ctx_compute(mac_ctx, &mac_input, &mut enc.mac);
            // Zero mac input
            for b in mac_input.iter_mut() { *b = 0; }
        }

        // Step 6: Store in B-tree
        let enc_ptr = Box::into_raw(enc) as *mut u8;
        let tree = match INDEXES[table_id as usize].as_mut() {
            Some(t) => t,
            None => return VOS_ERR_INVAL,
        };
        btree_insert(tree, rec.row_id, enc_ptr);

        // Zero plaintext from shared buffers
        for i in 0..plain_len { SERDE_BUF[i] = 0; }
        for i in 0..padded_len { CRYPTO_BUF[i] = 0; }
    }
    VOS_OK
}

/// Verify-then-decrypt pipeline.
/// The `encrypted_value` pointer comes from btree_search (points to EncryptedRecord).
pub fn db_decrypt_record(table_id: u32, encrypted_value: *mut u8) -> Option<Record> {
    unsafe {
        if encrypted_value.is_null() || table_id >= TABLE_COUNT { return None; }
        let enc = &*(encrypted_value as *const EncryptedRecord);

        // Step 1: Verify HMAC
        let mac_ctx = TABLE_MAC_CTXS[table_id as usize].as_ref()?;
        let mut computed_mac = [0u8; 32];
        {
            let mac_input_len = AES_BLOCK_SIZE.checked_add(enc.ciphertext_len as usize)?;
            let mut mac_input = vec![0u8; mac_input_len];
            mac_input[..AES_BLOCK_SIZE].copy_from_slice(&enc.iv);
            mac_input[AES_BLOCK_SIZE..].copy_from_slice(&enc.ciphertext[..enc.ciphertext_len as usize]);
            hmac_ctx_compute(mac_ctx, &mac_input, &mut computed_mac);
            for b in mac_input.iter_mut() { *b = 0; }
        }

        if !hmac_verify(&enc.mac, &computed_mac, 32) {
            crate::serial_println!("[DB] MAC verification failed!");
            for b in computed_mac.iter_mut() { *b = 0; }
            return None;
        }
        for b in computed_mac.iter_mut() { *b = 0; }

        // Step 2: AES-CBC decrypt
        let ct_len = enc.ciphertext_len as usize;
        if ct_len > CRYPTO_BUF.len() { return None; }
        let aes_ctx = TABLE_AES_KEYS[table_id as usize].as_ref()?;
        aes_cbc_decrypt(aes_ctx, &enc.iv, &enc.ciphertext[..ct_len],
                         &mut CRYPTO_BUF[..ct_len], ct_len);

        // Step 3: PKCS7 unpad
        let plain_len = aes_pkcs7_unpad(&CRYPTO_BUF, ct_len);
        if plain_len == 0 {
            for i in 0..ct_len { CRYPTO_BUF[i] = 0; }
            return None;
        }

        // Step 4: Deserialize
        let result = record_deserialize(&CRYPTO_BUF[..plain_len]);
        for i in 0..ct_len { CRYPTO_BUF[i] = 0; }

        match result {
            Some((rec, _consumed)) => Some(rec),
            None => None,
        }
    }
}

/// Get a single record by row_id (decrypt from btree).
pub fn db_get_record(table_id: u32, row_id: u64) -> Option<Record> {
    unsafe {
        if table_id >= TABLE_COUNT { return None; }
        let tree = INDEXES[table_id as usize].as_ref()?;
        let value = btree_search(tree, row_id);
        if value.is_null() { return None; }
        db_decrypt_record(table_id, value)
    }
}

/// Delete a record by row_id.
pub fn db_delete_record(table_id: u32, row_id: u64) -> i32 {
    unsafe {
        if table_id >= TABLE_COUNT { return VOS_ERR_INVAL; }
        let tree = match INDEXES[table_id as usize].as_mut() {
            Some(t) => t,
            None => return VOS_ERR_INVAL,
        };

        let enc_ptr = btree_search(tree, row_id);
        if enc_ptr.is_null() { return VOS_ERR_NOTFOUND; }

        btree_delete(tree, row_id);

        // Zero and free encrypted data
        let enc = Box::from_raw(enc_ptr as *mut EncryptedRecord);
        // ciphertext Vec is dropped automatically; mac/iv zeroed on drop
        drop(enc);
    }
    VOS_OK
}

/// Re-encrypt a modified record (delete old + insert new with fresh IV).
pub fn db_update_encrypted(table_id: u32, row_id: u64, modified: &mut Record) -> i32 {
    unsafe {
        if table_id >= TABLE_COUNT { return VOS_ERR_INVAL; }
    }

    let err = db_delete_record(table_id, row_id);
    if err != VOS_OK { return err; }

    modified.row_id = row_id;
    modified.table_id = table_id;
    db_insert_record(table_id, modified)
}

/// Flush all dirty tables to disk.
pub fn db_flush() -> i32 {
    db_persist::db_persist_commit()
}

// ---------------------------------------------------------------------------
// QueryResult and result helpers
// ---------------------------------------------------------------------------

pub struct QueryResult {
    pub rows: Vec<Record>,
    pub error_code: i32,
    pub error_msg: [u8; 256],
    pub schema: Option<&'static TableSchema>,
}

impl QueryResult {
    pub fn error_msg_str(&self) -> &str {
        let len = self.error_msg.iter().position(|&c| c == 0).unwrap_or(self.error_msg.len());
        core::str::from_utf8(&self.error_msg[..len]).unwrap_or("")
    }
}

pub fn db_result_create(capacity: u32) -> QueryResult {
    QueryResult {
        rows: Vec::with_capacity(capacity as usize),
        error_code: VOS_OK,
        error_msg: [0u8; 256],
        schema: None,
    }
}

pub fn db_result_add_row(result: &mut QueryResult, row: &Record) {
    // Clone the record into the result
    let mut new_rec = Record::new(row.table_id);
    new_rec.row_id = row.row_id;
    new_rec.field_count = row.field_count;
    for i in 0..MAX_COLUMNS {
        new_rec.fields[i] = row.fields[i].clone();
    }
    result.rows.push(new_rec);
}

pub fn db_result_free(_result: QueryResult) {
    // Vec and fields are dropped automatically in Rust
}

pub fn db_result_error(code: i32, msg: &str) -> QueryResult {
    let mut r = db_result_create(0);
    r.error_code = code;
    let bytes = msg.as_bytes();
    let len = bytes.len().min(255);
    r.error_msg[..len].copy_from_slice(&bytes[..len]);
    r
}

fn set_result_msg(result: &mut QueryResult, msg: &str) {
    let bytes = msg.as_bytes();
    let len = bytes.len().min(255);
    result.error_msg = [0u8; 256];
    result.error_msg[..len].copy_from_slice(&bytes[..len]);
}

// ---------------------------------------------------------------------------
// Utility: case-insensitive string compare
// ---------------------------------------------------------------------------

pub fn str_eq_ignore_case(a: &str, b: &str) -> bool {
    if a.len() != b.len() { return false; }
    for (ca, cb) in a.bytes().zip(b.bytes()) {
        let la = if ca >= b'A' && ca <= b'Z' { ca + 32 } else { ca };
        let lb = if cb >= b'A' && cb <= b'Z' { cb + 32 } else { cb };
        if la != lb { return false; }
    }
    true
}

/// Find column index by name (case-insensitive).
pub fn find_column_index(schema: &TableSchema, name: &str) -> i32 {
    for i in 0..schema.column_count as usize {
        if str_eq_ignore_case(schema.columns[i].name_str(), name) {
            return i as i32;
        }
    }
    -1
}
