// SQL-subset query parser for VaultOS-RS
// Port of kernel/db/query.c — recursive-descent parser
//
// Supported statements:
//   SELECT [cols|*] FROM table [WHERE col op val [AND ...]]
//   INSERT INTO table (cols) VALUES (vals)
//   DELETE FROM table [WHERE ...]
//   UPDATE table SET col=val [, ...] [WHERE ...]
//   SHOW TABLES
//   DESCRIBE table
//   GRANT rights ON object_id TO process_id
//   REVOKE cap_id

use alloc::string::String;
use alloc::vec::Vec;

use crate::db::database::{
    self, QueryResult, db_get_schema_by_name, db_get_schema_by_id, db_get_index,
    db_insert_record, db_decrypt_record, db_delete_record, db_update_encrypted,
    db_next_row_id, db_result_create, db_result_add_row, db_result_error,
    db_get_table_count, find_column_index, str_eq_ignore_case,
};
use crate::db::btree::btree_scan;
use crate::db::record::{Record, FieldValue, StrField};
use crate::db::schema::TableSchema;
use vaultos_shared::db_types::*;
use vaultos_shared::error_codes::*;

// ---------------------------------------------------------------------------
// Token types
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum TokenType {
    Select, Insert, Into, Delete, Update,
    From, Where, And, Set, Values,
    Show, Tables, Describe,
    Grant, Revoke, On, To,
    Read, Write, All,
    Star, Comma, LParen, RParen,
    Eq, Neq, Lt, Gt, Le, Ge,
    Ident, StringLit, Number,
    Eof, Error,
}

#[derive(Clone)]
struct Token {
    ttype: TokenType,
    value: [u8; MAX_STR_LEN + 1],
    value_len: usize,
}

impl Token {
    fn new() -> Self {
        Token {
            ttype: TokenType::Eof,
            value: [0u8; MAX_STR_LEN + 1],
            value_len: 0,
        }
    }

    fn value_str(&self) -> &str {
        core::str::from_utf8(&self.value[..self.value_len]).unwrap_or("")
    }

    fn set_value(&mut self, s: &str) {
        let bytes = s.as_bytes();
        let len = bytes.len().min(MAX_STR_LEN);
        self.value[..len].copy_from_slice(&bytes[..len]);
        self.value[len] = 0;
        self.value_len = len;
    }
}

// ---------------------------------------------------------------------------
// Parser state
// ---------------------------------------------------------------------------

struct Parser<'a> {
    input: &'a [u8],
    pos: usize,
    current: Token,
}

impl<'a> Parser<'a> {
    fn new(input: &'a str) -> Self {
        let mut p = Parser {
            input: input.as_bytes(),
            pos: 0,
            current: Token::new(),
        };
        p.next_token();
        p
    }

    fn skip_whitespace(&mut self) {
        while self.pos < self.input.len() && is_space(self.input[self.pos]) {
            self.pos += 1;
        }
    }

    fn next_token(&mut self) {
        self.skip_whitespace();

        if self.pos >= self.input.len() {
            self.current.ttype = TokenType::Eof;
            return;
        }

        let c = self.input[self.pos];

        // Single-char tokens
        if c == b'*' {
            self.current.ttype = TokenType::Star;
            self.current.value[0] = b'*';
            self.current.value[1] = 0;
            self.current.value_len = 1;
            self.pos += 1;
            return;
        }
        if c == b',' { self.current.ttype = TokenType::Comma; self.pos += 1; return; }
        if c == b'(' { self.current.ttype = TokenType::LParen; self.pos += 1; return; }
        if c == b')' { self.current.ttype = TokenType::RParen; self.pos += 1; return; }
        if c == b'=' { self.current.ttype = TokenType::Eq; self.pos += 1; return; }

        // Two-char operators
        if c == b'!' && self.pos + 1 < self.input.len() && self.input[self.pos + 1] == b'=' {
            self.current.ttype = TokenType::Neq;
            self.pos += 2;
            return;
        }
        if c == b'<' && self.pos + 1 < self.input.len() && self.input[self.pos + 1] == b'=' {
            self.current.ttype = TokenType::Le;
            self.pos += 2;
            return;
        }
        if c == b'>' && self.pos + 1 < self.input.len() && self.input[self.pos + 1] == b'=' {
            self.current.ttype = TokenType::Ge;
            self.pos += 2;
            return;
        }
        if c == b'<' { self.current.ttype = TokenType::Lt; self.pos += 1; return; }
        if c == b'>' { self.current.ttype = TokenType::Gt; self.pos += 1; return; }

        // String literal
        if c == b'\'' {
            self.pos += 1;
            let mut i = 0usize;
            while self.pos < self.input.len() && self.input[self.pos] != b'\'' && i < MAX_STR_LEN {
                self.current.value[i] = self.input[self.pos];
                i += 1;
                self.pos += 1;
            }
            self.current.value[i] = 0;
            self.current.value_len = i;
            if self.pos < self.input.len() && self.input[self.pos] == b'\'' {
                self.pos += 1;
            }
            self.current.ttype = TokenType::StringLit;
            return;
        }

        // Number
        if is_digit(c) {
            let mut i = 0usize;
            while self.pos < self.input.len() && is_digit(self.input[self.pos]) && i < 20 {
                self.current.value[i] = self.input[self.pos];
                i += 1;
                self.pos += 1;
            }
            self.current.value[i] = 0;
            self.current.value_len = i;
            self.current.ttype = TokenType::Number;
            return;
        }

        // Identifier or keyword
        if is_alpha(c) || c == b'_' {
            let mut i = 0usize;
            while self.pos < self.input.len()
                && (is_alnum(self.input[self.pos]) || self.input[self.pos] == b'_')
                && i < MAX_STR_LEN
            {
                self.current.value[i] = self.input[self.pos];
                i += 1;
                self.pos += 1;
            }
            self.current.value[i] = 0;
            self.current.value_len = i;
            self.current.ttype = check_keyword(self.current.value_str());
            return;
        }

        self.current.ttype = TokenType::Error;
        self.pos += 1;
    }

    fn expect(&mut self, ttype: TokenType) -> bool {
        if self.current.ttype != ttype { return false; }
        self.next_token();
        true
    }

    fn current_value_str(&self) -> &str {
        self.current.value_str()
    }
}

// ---------------------------------------------------------------------------
// Keyword lookup
// ---------------------------------------------------------------------------

fn check_keyword(word: &str) -> TokenType {
    if str_eq_ignore_case(word, "SELECT")   { return TokenType::Select; }
    if str_eq_ignore_case(word, "INSERT")   { return TokenType::Insert; }
    if str_eq_ignore_case(word, "INTO")     { return TokenType::Into; }
    if str_eq_ignore_case(word, "DELETE")   { return TokenType::Delete; }
    if str_eq_ignore_case(word, "UPDATE")   { return TokenType::Update; }
    if str_eq_ignore_case(word, "FROM")     { return TokenType::From; }
    if str_eq_ignore_case(word, "WHERE")    { return TokenType::Where; }
    if str_eq_ignore_case(word, "AND")      { return TokenType::And; }
    if str_eq_ignore_case(word, "SET")      { return TokenType::Set; }
    if str_eq_ignore_case(word, "VALUES")   { return TokenType::Values; }
    if str_eq_ignore_case(word, "SHOW")     { return TokenType::Show; }
    if str_eq_ignore_case(word, "TABLES")   { return TokenType::Tables; }
    if str_eq_ignore_case(word, "DESCRIBE") { return TokenType::Describe; }
    if str_eq_ignore_case(word, "GRANT")    { return TokenType::Grant; }
    if str_eq_ignore_case(word, "REVOKE")   { return TokenType::Revoke; }
    if str_eq_ignore_case(word, "ON")       { return TokenType::On; }
    if str_eq_ignore_case(word, "TO")       { return TokenType::To; }
    if str_eq_ignore_case(word, "READ")     { return TokenType::Read; }
    if str_eq_ignore_case(word, "WRITE")    { return TokenType::Write; }
    if str_eq_ignore_case(word, "ALL")      { return TokenType::All; }
    TokenType::Ident
}

// ---------------------------------------------------------------------------
// Character classification (no_std)
// ---------------------------------------------------------------------------

#[inline]
fn is_space(c: u8) -> bool { c == b' ' || c == b'\t' || c == b'\n' || c == b'\r' }
#[inline]
fn is_digit(c: u8) -> bool { c >= b'0' && c <= b'9' }
#[inline]
fn is_alpha(c: u8) -> bool { (c >= b'a' && c <= b'z') || (c >= b'A' && c <= b'Z') }
#[inline]
fn is_alnum(c: u8) -> bool { is_alpha(c) || is_digit(c) }

// ---------------------------------------------------------------------------
// Simple u64 parser from string
// ---------------------------------------------------------------------------

fn parse_u64(s: &str) -> u64 {
    let mut val: u64 = 0;
    for &b in s.as_bytes() {
        if b < b'0' || b > b'9' { break; }
        val = val.wrapping_mul(10).wrapping_add((b - b'0') as u64);
    }
    val
}

// ---------------------------------------------------------------------------
// WHERE clause types and parsing
// ---------------------------------------------------------------------------

struct WhereCond {
    column: [u8; MAX_COLUMN_NAME],
    column_len: usize,
    op: CmpOp,
    value: WhereValue,
}

enum WhereValue {
    Str(StrField),
    U64(u64),
}

impl WhereCond {
    fn column_str(&self) -> &str {
        core::str::from_utf8(&self.column[..self.column_len]).unwrap_or("")
    }
}

fn parse_op(p: &mut Parser) -> CmpOp {
    let op = match p.current.ttype {
        TokenType::Eq  => CmpOp::Eq,
        TokenType::Neq => CmpOp::Neq,
        TokenType::Lt  => CmpOp::Lt,
        TokenType::Gt  => CmpOp::Gt,
        TokenType::Le  => CmpOp::Le,
        TokenType::Ge  => CmpOp::Ge,
        _ => CmpOp::Eq,
    };
    p.next_token();
    op
}

fn parse_where(p: &mut Parser) -> Vec<WhereCond> {
    let mut conds = Vec::new();
    if p.current.ttype != TokenType::Where { return conds; }
    p.next_token(); // skip WHERE

    while conds.len() < MAX_WHERE_CONDS {
        if p.current.ttype != TokenType::Ident { break; }

        let mut cond = WhereCond {
            column: [0u8; MAX_COLUMN_NAME],
            column_len: 0,
            op: CmpOp::Eq,
            value: WhereValue::U64(0),
        };

        // Copy column name
        let val = p.current.value_str();
        let bytes = val.as_bytes();
        let len = bytes.len().min(MAX_COLUMN_NAME - 1);
        cond.column[..len].copy_from_slice(&bytes[..len]);
        cond.column_len = len;

        p.next_token();
        cond.op = parse_op(p);

        // Value
        if p.current.ttype == TokenType::StringLit {
            cond.value = WhereValue::Str(StrField::from_str(p.current.value_str()));
        } else if p.current.ttype == TokenType::Number {
            cond.value = WhereValue::U64(parse_u64(p.current.value_str()));
        } else {
            break;
        }
        p.next_token();
        conds.push(cond);

        if p.current.ttype == TokenType::And {
            p.next_token();
        } else {
            break;
        }
    }

    conds
}

// ---------------------------------------------------------------------------
// Field matching for WHERE conditions
// ---------------------------------------------------------------------------

fn match_field(field: &FieldValue, op: CmpOp, cond_val: &WhereValue) -> bool {
    match (field, cond_val) {
        (FieldValue::Str(fs), WhereValue::Str(cs)) => {
            let a = fs.as_str();
            let b = cs.as_str();
            let cmp = str_compare(a, b);
            match op {
                CmpOp::Eq  => cmp == 0,
                CmpOp::Neq => cmp != 0,
                CmpOp::Lt  => cmp < 0,
                CmpOp::Gt  => cmp > 0,
                CmpOp::Le  => cmp <= 0,
                CmpOp::Ge  => cmp >= 0,
                _ => false,
            }
        }
        (FieldValue::U64(fv), WhereValue::U64(cv)) => {
            match op {
                CmpOp::Eq  => *fv == *cv,
                CmpOp::Neq => *fv != *cv,
                CmpOp::Lt  => *fv < *cv,
                CmpOp::Gt  => *fv > *cv,
                CmpOp::Le  => *fv <= *cv,
                CmpOp::Ge  => *fv >= *cv,
                _ => false,
            }
        }
        (FieldValue::U32(fv), WhereValue::U64(cv)) => {
            let fv64 = *fv as u64;
            match op {
                CmpOp::Eq  => fv64 == *cv,
                CmpOp::Neq => fv64 != *cv,
                CmpOp::Lt  => fv64 < *cv,
                CmpOp::Gt  => fv64 > *cv,
                CmpOp::Le  => fv64 <= *cv,
                CmpOp::Ge  => fv64 >= *cv,
                _ => false,
            }
        }
        (FieldValue::Bool(fv), WhereValue::U64(cv)) => {
            let fb = if *fv { 1u64 } else { 0u64 };
            match op {
                CmpOp::Eq => fb == *cv,
                CmpOp::Neq => fb != *cv,
                _ => false,
            }
        }
        (FieldValue::Bool(fv), WhereValue::Str(sv)) => {
            let s = sv.as_str();
            let bval = str_eq_ignore_case(s, "true") || s == "1";
            match op {
                CmpOp::Eq => *fv == bval,
                CmpOp::Neq => *fv != bval,
                _ => false,
            }
        }
        // Cross-type: U64 field vs string condition (parse string as number)
        (FieldValue::U64(fv), WhereValue::Str(sv)) => {
            let cv = parse_u64(sv.as_str());
            match op {
                CmpOp::Eq  => *fv == cv,
                CmpOp::Neq => *fv != cv,
                _ => false,
            }
        }
        _ => false,
    }
}

fn record_matches(rec: &Record, schema: &TableSchema, conds: &[WhereCond]) -> bool {
    for cond in conds {
        let col_idx = find_column_index(schema, cond.column_str());
        if col_idx < 0 { return false; }
        let field = match &rec.fields[col_idx as usize] {
            Some(f) => f,
            None => return false,
        };
        if !match_field(field, cond.op, &cond.value) {
            return false;
        }
    }
    true
}

fn str_compare(a: &str, b: &str) -> i32 {
    let ab = a.as_bytes();
    let bb = b.as_bytes();
    let len = ab.len().min(bb.len());
    for i in 0..len {
        if ab[i] < bb[i] { return -1; }
        if ab[i] > bb[i] { return 1; }
    }
    if ab.len() < bb.len() { -1 }
    else if ab.len() > bb.len() { 1 }
    else { 0 }
}

// ---------------------------------------------------------------------------
// Scan callback context (for btree_scan)
// ---------------------------------------------------------------------------

struct ScanCtx {
    result: *mut QueryResult,
    schema: *const TableSchema,
    conds: *const Vec<WhereCond>,
    delete_mode: bool,
}

fn select_scan_callback(_key: u64, value: *mut u8, ctx: *mut u8) {
    unsafe {
        let sc = &*(ctx as *const ScanCtx);
        let schema = &*sc.schema;

        let rec = match db_decrypt_record(schema.table_id, value) {
            Some(r) => r,
            None => return,
        };

        let conds = &*sc.conds;
        if record_matches(&rec, schema, conds) {
            db_result_add_row(&mut *sc.result, &rec);
        }
    }
}

// ---------------------------------------------------------------------------
// SHOW TABLES
// ---------------------------------------------------------------------------

// Static schemas for virtual result sets
static mut SHOW_SCHEMA: Option<TableSchema> = None;
static mut DESC_SCHEMA: Option<TableSchema> = None;

fn get_show_schema() -> &'static TableSchema {
    unsafe {
        if SHOW_SCHEMA.is_none() {
            let mut s = TableSchema::zeroed();
            s.set_name("Tables");
            s.column_count = 3;
            set_col_name(&mut s.columns[0], "id");
            s.columns[0].col_type = ColumnType::U64;
            set_col_name(&mut s.columns[1], "table_name");
            s.columns[1].col_type = ColumnType::Str;
            set_col_name(&mut s.columns[2], "columns");
            s.columns[2].col_type = ColumnType::U64;
            SHOW_SCHEMA = Some(s);
        }
        // SAFETY: Some() was just assigned above if it was None
        match SHOW_SCHEMA.as_ref() {
            Some(s) => s,
            None => unreachable!(),
        }
    }
}

fn get_desc_schema() -> &'static TableSchema {
    unsafe {
        if DESC_SCHEMA.is_none() {
            let mut s = TableSchema::zeroed();
            s.set_name("Columns");
            s.column_count = 4;
            set_col_name(&mut s.columns[0], "name");
            s.columns[0].col_type = ColumnType::Str;
            set_col_name(&mut s.columns[1], "type");
            s.columns[1].col_type = ColumnType::Str;
            set_col_name(&mut s.columns[2], "pk");
            s.columns[2].col_type = ColumnType::Str;
            set_col_name(&mut s.columns[3], "not_null");
            s.columns[3].col_type = ColumnType::Str;
            DESC_SCHEMA = Some(s);
        }
        // SAFETY: Some() was just assigned above if it was None
        match DESC_SCHEMA.as_ref() {
            Some(s) => s,
            None => unreachable!(),
        }
    }
}

fn set_col_name(col: &mut crate::db::schema::ColumnDef, name: &str) {
    let bytes = name.as_bytes();
    let len = bytes.len().min(MAX_COLUMN_NAME - 1);
    col.name[..len].copy_from_slice(&bytes[..len]);
    col.name[len] = 0;
}

fn exec_show_tables() -> QueryResult {
    let tc = db_get_table_count();
    let mut result = db_result_create(tc);

    for i in 0..tc {
        if let Some(s) = db_get_schema_by_id(i) {
            let mut row = Record::new(i);
            row.row_id = i as u64;
            row.table_id = i;
            row.field_count = 3;
            row.set_u64(0, i as u64);
            row.set_str(1, s.name_str());
            row.set_u64(2, s.column_count as u64);
            db_result_add_row(&mut result, &row);
        }
    }

    result.schema = Some(get_show_schema());
    result
}

// ---------------------------------------------------------------------------
// DESCRIBE
// ---------------------------------------------------------------------------

fn exec_describe(p: &mut Parser) -> QueryResult {
    if p.current.ttype != TokenType::Ident {
        return db_result_error(VOS_ERR_SYNTAX, "Expected table name");
    }

    let table_name = p.current_value_str();
    let schema = match db_get_schema_by_name(table_name) {
        Some(s) => s,
        None => return db_result_error(VOS_ERR_NOTFOUND, "Table not found"),
    };

    let mut result = db_result_create(schema.column_count);

    for i in 0..schema.column_count as usize {
        let mut row = Record::new(0);
        row.row_id = i as u64;
        row.field_count = 4;
        row.set_str(0, schema.columns[i].name_str());

        let type_str = match schema.columns[i].col_type {
            ColumnType::U64  => "U64",
            ColumnType::I64  => "I64",
            ColumnType::Str  => "STR",
            ColumnType::Blob => "BLOB",
            ColumnType::Bool => "BOOL",
            ColumnType::U32  => "U32",
            ColumnType::U8   => "U8",
        };
        row.set_str(1, type_str);
        row.set_str(2, if schema.columns[i].primary_key { "YES" } else { "NO" });
        row.set_str(3, if schema.columns[i].not_null { "YES" } else { "NO" });
        db_result_add_row(&mut result, &row);
    }

    result.schema = Some(get_desc_schema());
    result
}

// ---------------------------------------------------------------------------
// SELECT
// ---------------------------------------------------------------------------

fn exec_select(p: &mut Parser, _pid: u64) -> QueryResult {
    // SELECT * FROM table [WHERE ...]
    if p.current.ttype == TokenType::Star {
        p.next_token();
    } else {
        // Skip column list for now - always select all
        while p.current.ttype == TokenType::Ident {
            p.next_token();
            if p.current.ttype == TokenType::Comma { p.next_token(); } else { break; }
        }
    }

    if !p.expect(TokenType::From) {
        return db_result_error(VOS_ERR_SYNTAX, "Expected FROM");
    }

    if p.current.ttype != TokenType::Ident {
        return db_result_error(VOS_ERR_SYNTAX, "Expected table name");
    }

    let table_name = p.current_value_str();
    let schema = match db_get_schema_by_name(table_name) {
        Some(s) => s,
        None => return db_result_error(VOS_ERR_NOTFOUND, "Table not found"),
    };
    p.next_token();

    let conds = parse_where(p);

    let mut result = db_result_create(16);
    result.schema = Some(schema);

    let index = match db_get_index(schema.table_id) {
        Some(t) => t as *mut crate::db::btree::Btree,
        None => return db_result_error(VOS_ERR_INVAL, "No index for table"),
    };

    let ctx = ScanCtx {
        result: &mut result as *mut QueryResult,
        schema: schema as *const TableSchema,
        conds: &conds as *const Vec<WhereCond>,
        delete_mode: false,
    };

    unsafe {
        btree_scan(&*index, select_scan_callback, &ctx as *const ScanCtx as *mut u8);
    }

    result
}

// ---------------------------------------------------------------------------
// INSERT
// ---------------------------------------------------------------------------

fn exec_insert(p: &mut Parser, pid: u64) -> QueryResult {
    // INSERT INTO table (cols) VALUES (vals)
    if !p.expect(TokenType::Into) {
        return db_result_error(VOS_ERR_SYNTAX, "Expected INTO");
    }

    if p.current.ttype != TokenType::Ident {
        return db_result_error(VOS_ERR_SYNTAX, "Expected table name");
    }

    let table_name = p.current_value_str();
    let schema = match db_get_schema_by_name(table_name) {
        Some(s) => s,
        None => return db_result_error(VOS_ERR_NOTFOUND, "Table not found"),
    };
    p.next_token();

    // Parse column names
    let mut col_names: Vec<[u8; MAX_COLUMN_NAME]> = Vec::new();

    if !p.expect(TokenType::LParen) {
        return db_result_error(VOS_ERR_SYNTAX, "Expected '('");
    }

    while p.current.ttype == TokenType::Ident && col_names.len() < MAX_INSERT_VALS {
        let mut name = [0u8; MAX_COLUMN_NAME];
        let val = p.current.value_str();
        let bytes = val.as_bytes();
        let len = bytes.len().min(MAX_COLUMN_NAME - 1);
        name[..len].copy_from_slice(&bytes[..len]);
        col_names.push(name);
        p.next_token();
        if p.current.ttype == TokenType::Comma { p.next_token(); } else { break; }
    }

    if !p.expect(TokenType::RParen) {
        return db_result_error(VOS_ERR_SYNTAX, "Expected ')'");
    }

    if !p.expect(TokenType::Values) {
        return db_result_error(VOS_ERR_SYNTAX, "Expected VALUES");
    }

    if !p.expect(TokenType::LParen) {
        return db_result_error(VOS_ERR_SYNTAX, "Expected '('");
    }

    // Build record
    let mut rec = Record::new(schema.table_id);
    rec.row_id = db_next_row_id();
    rec.table_id = schema.table_id;

    // Set primary key if first column is PK
    if schema.columns[0].primary_key {
        rec.set_u64(0, rec.row_id);
    }

    let mut val_idx = 0u32;
    while (val_idx as usize) < col_names.len()
        && p.current.ttype != TokenType::RParen
        && p.current.ttype != TokenType::Eof
    {
        let col_name_str = {
            let name = &col_names[val_idx as usize];
            let len = name.iter().position(|&c| c == 0).unwrap_or(name.len());
            core::str::from_utf8(&name[..len]).unwrap_or("")
        };
        let ci = find_column_index(schema, col_name_str);
        if ci < 0 {
            return db_result_error(VOS_ERR_INVAL, "Unknown column");
        }

        if p.current.ttype == TokenType::StringLit {
            rec.set_str(ci as u32, p.current.value_str());
        } else if p.current.ttype == TokenType::Number {
            let v = parse_u64(p.current.value_str());
            if schema.columns[ci as usize].col_type == ColumnType::U64 {
                rec.set_u64(ci as u32, v);
            } else if schema.columns[ci as usize].col_type == ColumnType::U32 {
                rec.set_u32(ci as u32, v as u32);
            } else {
                rec.set_str(ci as u32, p.current.value_str());
            }
        } else {
            break;
        }
        p.next_token();
        val_idx += 1;
        if p.current.ttype == TokenType::Comma { p.next_token(); } else { break; }
    }

    rec.field_count = schema.column_count;

    // Set owner_pid if column exists
    let owner_idx = find_column_index(schema, "owner_pid");
    if owner_idx >= 0 {
        rec.set_u64(owner_idx as u32, pid);
    }

    // Set created timestamp if column exists
    let created_idx = find_column_index(schema, "created");
    if created_idx >= 0 {
        rec.set_u64(created_idx as u32, crate::arch::x86_64::pit::pit_get_ticks());
    }

    // Set size if column exists and data column exists
    let size_idx = find_column_index(schema, "size");
    let data_idx = find_column_index(schema, "data");
    if size_idx >= 0 && data_idx >= 0 {
        if let Some(FieldValue::Str(ref s)) = rec.fields[data_idx as usize] {
            rec.set_u64(size_idx as u32, s.length as u64);
        }
    }

    let row_id = rec.row_id;
    let err = db_insert_record(schema.table_id, &mut rec);
    if err != VOS_OK {
        return db_result_error(err, "Insert failed");
    }

    let mut result = db_result_create(0);
    set_result_msg_insert(&mut result, row_id);
    result
}

fn set_result_msg_insert(result: &mut QueryResult, row_id: u64) {
    // Format: "1 row inserted (row_id=N)"
    let mut msg = [0u8; 256];
    let prefix = b"1 row inserted (row_id=";
    msg[..prefix.len()].copy_from_slice(prefix);
    let mut pos = prefix.len();
    pos += write_u64_to_buf(&mut msg[pos..], row_id);
    if pos < 255 { msg[pos] = b')'; pos += 1; }
    result.error_msg = msg;
}

fn set_result_msg_count(result: &mut QueryResult, prefix: &str, count: u32) {
    let mut msg = [0u8; 256];
    let bytes = prefix.as_bytes();
    let plen = bytes.len().min(200);
    msg[..plen].copy_from_slice(&bytes[..plen]);
    let mut pos = plen;
    pos += write_u32_to_buf(&mut msg[pos..], count);
    result.error_msg = msg;
}

// ---------------------------------------------------------------------------
// DELETE
// ---------------------------------------------------------------------------

fn exec_delete(p: &mut Parser, _pid: u64) -> QueryResult {
    // DELETE FROM table [WHERE ...]
    if !p.expect(TokenType::From) {
        return db_result_error(VOS_ERR_SYNTAX, "Expected FROM");
    }

    if p.current.ttype != TokenType::Ident {
        return db_result_error(VOS_ERR_SYNTAX, "Expected table name");
    }

    let table_name = p.current_value_str();
    let schema = match db_get_schema_by_name(table_name) {
        Some(s) => s,
        None => return db_result_error(VOS_ERR_NOTFOUND, "Table not found"),
    };
    p.next_token();

    let conds = parse_where(p);

    // Find matching rows via scan
    let mut matches = db_result_create(16);
    let index = match db_get_index(schema.table_id) {
        Some(t) => t as *mut crate::db::btree::Btree,
        None => return db_result_error(VOS_ERR_INVAL, "No index for table"),
    };

    let ctx = ScanCtx {
        result: &mut matches as *mut QueryResult,
        schema: schema as *const TableSchema,
        conds: &conds as *const Vec<WhereCond>,
        delete_mode: true,
    };

    unsafe {
        btree_scan(&*index, select_scan_callback, &ctx as *const ScanCtx as *mut u8);
    }

    // Delete matched rows
    let mut deleted: u32 = 0;
    // Collect row IDs first to avoid mutating tree during iteration
    let row_ids: Vec<u64> = matches.rows.iter().map(|r| r.row_id).collect();
    for row_id in row_ids {
        db_delete_record(schema.table_id, row_id);
        deleted += 1;
    }

    let mut result = db_result_create(0);
    set_result_msg_count(&mut result, "row(s) deleted: ", deleted);
    result
}

// ---------------------------------------------------------------------------
// UPDATE
// ---------------------------------------------------------------------------

fn exec_update(p: &mut Parser, _pid: u64) -> QueryResult {
    // UPDATE table SET col=val [, ...] [WHERE ...]
    if p.current.ttype != TokenType::Ident {
        return db_result_error(VOS_ERR_SYNTAX, "Expected table name");
    }

    let table_name = p.current_value_str();
    let schema = match db_get_schema_by_name(table_name) {
        Some(s) => s,
        None => return db_result_error(VOS_ERR_NOTFOUND, "Table not found"),
    };
    p.next_token();

    if !p.expect(TokenType::Set) {
        return db_result_error(VOS_ERR_SYNTAX, "Expected SET");
    }

    // Parse SET assignments
    struct SetAssign {
        col_name: [u8; MAX_COLUMN_NAME],
        col_name_len: usize,
        value: SetValue,
    }

    enum SetValue {
        Str(StrField),
        U64(u64),
    }

    let mut assignments: Vec<SetAssign> = Vec::new();

    while p.current.ttype == TokenType::Ident && assignments.len() < MAX_INSERT_VALS {
        let mut sa = SetAssign {
            col_name: [0u8; MAX_COLUMN_NAME],
            col_name_len: 0,
            value: SetValue::U64(0),
        };
        let val = p.current.value_str();
        let bytes = val.as_bytes();
        let len = bytes.len().min(MAX_COLUMN_NAME - 1);
        sa.col_name[..len].copy_from_slice(&bytes[..len]);
        sa.col_name_len = len;

        p.next_token();
        if p.current.ttype != TokenType::Eq { break; }
        p.next_token();

        if p.current.ttype == TokenType::StringLit {
            sa.value = SetValue::Str(StrField::from_str(p.current.value_str()));
        } else if p.current.ttype == TokenType::Number {
            sa.value = SetValue::U64(parse_u64(p.current.value_str()));
        } else {
            break;
        }
        p.next_token();
        assignments.push(sa);
        if p.current.ttype == TokenType::Comma { p.next_token(); } else { break; }
    }

    let conds = parse_where(p);

    // Find matching rows
    let mut matches = db_result_create(16);
    let index = match db_get_index(schema.table_id) {
        Some(t) => t as *mut crate::db::btree::Btree,
        None => return db_result_error(VOS_ERR_INVAL, "No index for table"),
    };

    let ctx = ScanCtx {
        result: &mut matches as *mut QueryResult,
        schema: schema as *const TableSchema,
        conds: &conds as *const Vec<WhereCond>,
        delete_mode: false,
    };

    unsafe {
        btree_scan(&*index, select_scan_callback, &ctx as *const ScanCtx as *mut u8);
    }

    // Update: modify matched records and re-encrypt
    let mut updated: u32 = 0;
    for i in 0..matches.rows.len() {
        let mut modified = Record::new(schema.table_id);
        modified.row_id = matches.rows[i].row_id;
        modified.table_id = matches.rows[i].table_id;
        modified.field_count = matches.rows[i].field_count;
        for f in 0..MAX_COLUMNS {
            modified.fields[f] = matches.rows[i].fields[f].clone();
        }

        for sa in &assignments {
            let col_str = core::str::from_utf8(&sa.col_name[..sa.col_name_len]).unwrap_or("");
            let ci = find_column_index(schema, col_str);
            if ci < 0 { continue; }
            match &sa.value {
                SetValue::Str(s) => {
                    modified.fields[ci as usize] = Some(FieldValue::Str(s.clone()));
                }
                SetValue::U64(v) => {
                    modified.fields[ci as usize] = Some(FieldValue::U64(*v));
                }
            }
        }

        db_update_encrypted(schema.table_id, modified.row_id, &mut modified);
        updated += 1;
    }

    let mut result = db_result_create(0);
    set_result_msg_count(&mut result, "row(s) updated: ", updated);
    result
}

// ---------------------------------------------------------------------------
// GRANT (stub - parse and return message)
// ---------------------------------------------------------------------------

fn exec_grant(p: &mut Parser, _pid: u64) -> QueryResult {
    let mut rights: u32 = 0;

    while p.current.ttype == TokenType::Read
       || p.current.ttype == TokenType::Write
       || p.current.ttype == TokenType::All
       || p.current.ttype == TokenType::Ident
    {
        let val = p.current.value_str();
        if p.current.ttype == TokenType::Read || str_eq_ignore_case(val, "READ") {
            rights |= 0x01; // CAP_READ
        } else if p.current.ttype == TokenType::Write || str_eq_ignore_case(val, "WRITE") {
            rights |= 0x02; // CAP_WRITE
        } else if p.current.ttype == TokenType::All || str_eq_ignore_case(val, "ALL") {
            rights = 0xFF; // CAP_ALL
        }
        p.next_token();
        if p.current.ttype == TokenType::Comma { p.next_token(); } else { break; }
    }

    if !p.expect(TokenType::On) {
        return db_result_error(VOS_ERR_SYNTAX, "Expected ON");
    }

    if p.current.ttype != TokenType::Number {
        return db_result_error(VOS_ERR_SYNTAX, "Expected object_id");
    }
    let object_id = parse_u64(p.current.value_str());
    p.next_token();

    if !p.expect(TokenType::To) {
        return db_result_error(VOS_ERR_SYNTAX, "Expected TO");
    }

    if p.current.ttype != TokenType::Number {
        return db_result_error(VOS_ERR_SYNTAX, "Expected process_id");
    }
    let target_pid = parse_u64(p.current.value_str());

    let mut result = db_result_create(0);
    // Format grant message
    let mut msg = [0u8; 256];
    let prefix = b"GRANT rights=0x";
    msg[..prefix.len()].copy_from_slice(prefix);
    let mut pos = prefix.len();
    pos += write_hex_to_buf(&mut msg[pos..], rights as u64);
    let on_str = b" on obj=";
    if pos + on_str.len() < 256 {
        msg[pos..pos + on_str.len()].copy_from_slice(on_str);
        pos += on_str.len();
    }
    pos += write_u64_to_buf(&mut msg[pos..], object_id);
    let to_str = b" to pid=";
    if pos + to_str.len() < 256 {
        msg[pos..pos + to_str.len()].copy_from_slice(to_str);
        pos += to_str.len();
    }
    pos += write_u64_to_buf(&mut msg[pos..], target_pid);
    let note = b" (cap system not yet wired)";
    if pos + note.len() < 256 {
        msg[pos..pos + note.len()].copy_from_slice(note);
    }
    result.error_msg = msg;
    result
}

// ---------------------------------------------------------------------------
// REVOKE (stub - parse and return message)
// ---------------------------------------------------------------------------

fn exec_revoke(p: &mut Parser, _pid: u64) -> QueryResult {
    if p.current.ttype != TokenType::Number {
        return db_result_error(VOS_ERR_SYNTAX, "Expected cap_id");
    }
    let cap_id = parse_u64(p.current.value_str());

    let mut result = db_result_create(0);
    let mut msg = [0u8; 256];
    let prefix = b"REVOKE cap_id=";
    msg[..prefix.len()].copy_from_slice(prefix);
    let mut pos = prefix.len();
    pos += write_u64_to_buf(&mut msg[pos..], cap_id);
    let note = b" (cap system not yet wired)";
    if pos + note.len() < 256 {
        msg[pos..pos + note.len()].copy_from_slice(note);
    }
    result.error_msg = msg;
    result
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

/// Execute a SQL-subset query. Returns a QueryResult.
pub fn query_execute(input: &str, caller_pid: u64) -> QueryResult {
    let mut p = Parser::new(input);

    match p.current.ttype {
        TokenType::Show => {
            p.next_token();
            if p.current.ttype == TokenType::Tables {
                return exec_show_tables();
            }
            db_result_error(VOS_ERR_SYNTAX, "Expected TABLES after SHOW")
        }
        TokenType::Describe => {
            p.next_token();
            exec_describe(&mut p)
        }
        TokenType::Select => {
            p.next_token();
            exec_select(&mut p, caller_pid)
        }
        TokenType::Insert => {
            p.next_token();
            exec_insert(&mut p, caller_pid)
        }
        TokenType::Delete => {
            p.next_token();
            exec_delete(&mut p, caller_pid)
        }
        TokenType::Update => {
            p.next_token();
            exec_update(&mut p, caller_pid)
        }
        TokenType::Grant => {
            p.next_token();
            exec_grant(&mut p, caller_pid)
        }
        TokenType::Revoke => {
            p.next_token();
            exec_revoke(&mut p, caller_pid)
        }
        _ => {
            db_result_error(VOS_ERR_SYNTAX,
                "Unknown command. Use: SELECT, INSERT, DELETE, UPDATE, SHOW TABLES, DESCRIBE, GRANT, REVOKE")
        }
    }
}

// ---------------------------------------------------------------------------
// Number-to-string helpers (no_std, no alloc for formatting)
// ---------------------------------------------------------------------------

fn write_u64_to_buf(buf: &mut [u8], val: u64) -> usize {
    if val == 0 {
        if buf.is_empty() { return 0; }
        buf[0] = b'0';
        return 1;
    }
    let mut tmp = [0u8; 20];
    let mut v = val;
    let mut i = 0usize;
    while v > 0 {
        tmp[i] = b'0' + (v % 10) as u8;
        v /= 10;
        i += 1;
    }
    let len = i.min(buf.len());
    for j in 0..len {
        buf[j] = tmp[i - 1 - j];
    }
    len
}

fn write_u32_to_buf(buf: &mut [u8], val: u32) -> usize {
    write_u64_to_buf(buf, val as u64)
}

fn write_hex_to_buf(buf: &mut [u8], val: u64) -> usize {
    let hex = b"0123456789abcdef";
    if val == 0 {
        if buf.is_empty() { return 0; }
        buf[0] = b'0';
        return 1;
    }
    let mut tmp = [0u8; 16];
    let mut v = val;
    let mut i = 0usize;
    while v > 0 {
        tmp[i] = hex[(v & 0xF) as usize];
        v >>= 4;
        i += 1;
    }
    let len = i.min(buf.len());
    for j in 0..len {
        buf[j] = tmp[i - 1 - j];
    }
    len
}

fn set_result_msg(result: &mut QueryResult, msg: &str) {
    let bytes = msg.as_bytes();
    let len = bytes.len().min(255);
    result.error_msg = [0u8; 256];
    result.error_msg[..len].copy_from_slice(&bytes[..len]);
}
