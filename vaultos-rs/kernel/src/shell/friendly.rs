// Friendly command translator: user-friendly syntax → SQL

const SQL_MAX: usize = 512;

/// Table alias mappings
static ALIASES: &[(&str, &str)] = &[
    ("procs",   "ProcessTable"),
    ("caps",    "CapabilityTable"),
    ("objects", "ObjectTable"),
    ("msgs",    "MessageTable"),
    ("audit",   "AuditTable"),
    ("config",  "SystemTable"),
    ("sys",     "SystemTable"),
];

/// Resolve alias to real table name, or return as-is.
pub fn resolve_alias(name: &str) -> &str {
    for &(alias, real) in ALIASES {
        if eq_ci(name, alias) {
            return real;
        }
    }
    name
}

/// Try to translate a friendly command to SQL. Returns Some(sql_bytes_len) on success.
pub fn translate(input: &[u8], sql: &mut [u8; SQL_MAX]) -> Option<usize> {
    let input_str = to_str(input);
    let trimmed = input_str.trim();
    if trimmed.is_empty() { return None; }

    let mut tokens = [("", 0usize, 0usize); 32]; // (token, start, end)
    let count = tokenize(trimmed, &mut tokens);
    if count == 0 { return None; }

    let verb = tokens[0].0;

    // tables / SHOW TABLES
    if eq_ci(verb, "tables") {
        return write_sql(sql, b"SHOW TABLES");
    }

    // show <table> → SELECT * FROM <table>
    if eq_ci(verb, "show") || eq_ci(verb, "list") {
        if count >= 2 {
            let table = resolve_alias(tokens[1].0);
            return write_fmt(sql, &[b"SELECT * FROM ", table.as_bytes()]);
        } else if eq_ci(verb, "list") {
            return write_sql(sql, b"SELECT * FROM ObjectTable");
        }
        return None;
    }

    // info <table> → DESCRIBE <table>
    if eq_ci(verb, "info") && count >= 2 {
        let table = resolve_alias(tokens[1].0);
        return write_fmt(sql, &[b"DESCRIBE ", table.as_bytes()]);
    }

    // count <table> → SELECT * FROM <table> (result count shown by display)
    if eq_ci(verb, "count") && count >= 2 {
        let table = resolve_alias(tokens[1].0);
        return write_fmt(sql, &[b"SELECT * FROM ", table.as_bytes()]);
    }

    // find <table> [col=val ...] → SELECT * FROM <table> WHERE ...
    if eq_ci(verb, "find") && count >= 2 {
        let table = resolve_alias(tokens[1].0);
        let mut pos = write_to(sql, 0, b"SELECT * FROM ");
        pos = write_to(sql, pos, table.as_bytes());
        if count > 2 {
            pos = write_to(sql, pos, b" WHERE ");
            pos = build_where(trimmed, &tokens, 2, count, sql, pos);
        }
        return Some(pos);
    }

    // add <table> col=val [...] → INSERT INTO <table> (...) VALUES (...)
    if eq_ci(verb, "add") && count >= 3 {
        let table = resolve_alias(tokens[1].0);
        return build_insert(trimmed, &tokens, 2, count, table, sql);
    }

    // del <table> col=val [...] → DELETE FROM <table> WHERE ...
    if eq_ci(verb, "del") || eq_ci(verb, "rm") {
        if count >= 3 {
            let table = resolve_alias(tokens[1].0);
            let mut pos = write_to(sql, 0, b"DELETE FROM ");
            pos = write_to(sql, pos, table.as_bytes());
            pos = write_to(sql, pos, b" WHERE ");
            pos = build_where(trimmed, &tokens, 2, count, sql, pos);
            return Some(pos);
        }
        // rm <name> → DELETE FROM ObjectTable WHERE name = '<name>'
        if eq_ci(verb, "rm") && count >= 2 {
            let name = tokens[1].0;
            let mut pos = write_to(sql, 0, b"DELETE FROM ObjectTable WHERE name = '");
            pos = write_escaped(sql, pos, name.as_bytes());
            pos = write_to(sql, pos, b"'");
            return Some(pos);
        }
        return None;
    }

    // set <table> col=val [...] where k=v → UPDATE <table> SET ... WHERE ...
    if eq_ci(verb, "set") && count >= 4 {
        let table = resolve_alias(tokens[1].0);
        return build_update(trimmed, &tokens, 2, count, table, sql);
    }

    // create <type> <name> [content] → INSERT INTO ObjectTable
    if eq_ci(verb, "create") && count >= 3 {
        let obj_type = tokens[1].0;
        let name = tokens[2].0;
        let mut pos = write_to(sql, 0, b"INSERT INTO ObjectTable (name, type");
        if count > 3 {
            pos = write_to(sql, pos, b", data) VALUES ('");
        } else {
            pos = write_to(sql, pos, b") VALUES ('");
        }
        pos = write_escaped(sql, pos, name.as_bytes());
        pos = write_to(sql, pos, b"', '");
        pos = write_escaped(sql, pos, obj_type.as_bytes());
        pos = write_to(sql, pos, b"'");
        if count > 3 {
            // Rest of line is content
            let content_start = tokens[3].1;
            let content = &trimmed[content_start..];
            pos = write_to(sql, pos, b", '");
            pos = write_escaped(sql, pos, content.as_bytes());
            pos = write_to(sql, pos, b"'");
        }
        pos = write_to(sql, pos, b")");
        return Some(pos);
    }

    // open <name> → SELECT * FROM ObjectTable WHERE name = '<name>'
    if eq_ci(verb, "open") || eq_ci(verb, "cat") {
        if count >= 2 {
            let name = tokens[1].0;
            let mut pos = write_to(sql, 0, b"SELECT * FROM ObjectTable WHERE name = '");
            pos = write_escaped(sql, pos, name.as_bytes());
            pos = write_to(sql, pos, b"'");
            return Some(pos);
        }
        return None;
    }

    // ps → SELECT * FROM ProcessTable
    if eq_ci(verb, "ps") {
        return write_sql(sql, b"SELECT * FROM ProcessTable");
    }

    None // Not a friendly command; try as raw SQL
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn build_where(
    _input: &str, tokens: &[(&str, usize, usize); 32],
    start: usize, count: usize, sql: &mut [u8; SQL_MAX], mut pos: usize,
) -> usize {
    let mut first = true;
    for i in start..count {
        let tok = tokens[i].0;
        if eq_ci(tok, "where") { continue; }
        if let Some(eq_pos) = tok.find('=') {
            if !first { pos = write_to(sql, pos, b" AND "); }
            let col = &tok[..eq_pos];
            let val = &tok[eq_pos + 1..];
            pos = write_to(sql, pos, col.as_bytes());
            pos = write_to(sql, pos, b" = ");
            pos = append_value(sql, pos, val);
            first = false;
        }
    }
    pos
}

fn build_insert(
    _input: &str, tokens: &[(&str, usize, usize); 32],
    start: usize, count: usize, table: &str, sql: &mut [u8; SQL_MAX],
) -> Option<usize> {
    let mut cols: [&str; 16] = [""; 16];
    let mut vals: [&str; 16] = [""; 16];
    let mut n = 0;

    for i in start..count {
        let tok = tokens[i].0;
        if let Some(eq_pos) = tok.find('=') {
            if n >= 16 { break; }
            cols[n] = &tok[..eq_pos];
            vals[n] = &tok[eq_pos + 1..];
            n += 1;
        }
    }
    if n == 0 { return None; }

    let mut pos = write_to(sql, 0, b"INSERT INTO ");
    pos = write_to(sql, pos, table.as_bytes());
    pos = write_to(sql, pos, b" (");
    for i in 0..n {
        if i > 0 { pos = write_to(sql, pos, b", "); }
        pos = write_to(sql, pos, cols[i].as_bytes());
    }
    pos = write_to(sql, pos, b") VALUES (");
    for i in 0..n {
        if i > 0 { pos = write_to(sql, pos, b", "); }
        pos = append_value(sql, pos, vals[i]);
    }
    pos = write_to(sql, pos, b")");
    Some(pos)
}

fn build_update(
    _input: &str, tokens: &[(&str, usize, usize); 32],
    start: usize, count: usize, table: &str, sql: &mut [u8; SQL_MAX],
) -> Option<usize> {
    // Find "where" keyword
    let mut where_idx = count;
    for i in start..count {
        if eq_ci(tokens[i].0, "where") {
            where_idx = i;
            break;
        }
    }

    let mut pos = write_to(sql, 0, b"UPDATE ");
    pos = write_to(sql, pos, table.as_bytes());
    pos = write_to(sql, pos, b" SET ");

    let mut first = true;
    for i in start..where_idx {
        let tok = tokens[i].0;
        if let Some(eq_pos) = tok.find('=') {
            if !first { pos = write_to(sql, pos, b", "); }
            let col = &tok[..eq_pos];
            let val = &tok[eq_pos + 1..];
            pos = write_to(sql, pos, col.as_bytes());
            pos = write_to(sql, pos, b" = ");
            pos = append_value(sql, pos, val);
            first = false;
        }
    }

    if where_idx < count {
        pos = write_to(sql, pos, b" WHERE ");
        pos = build_where(_input, tokens, where_idx + 1, count, sql, pos);
    }

    Some(pos)
}

/// Escape single quotes in user input for safe SQL embedding (' → '').
fn write_escaped(sql: &mut [u8; SQL_MAX], pos: usize, input: &[u8]) -> usize {
    let mut p = pos;
    for &b in input {
        if p >= SQL_MAX - 1 { break; }
        if b == b'\'' {
            p = write_to(sql, p, b"''");
        } else {
            sql[p] = b;
            sql[p + 1] = 0;
            p += 1;
        }
    }
    p
}

fn append_value(sql: &mut [u8; SQL_MAX], mut pos: usize, val: &str) -> usize {
    if is_numeric(val) || eq_ci(val, "true") || eq_ci(val, "false") {
        pos = write_to(sql, pos, val.as_bytes());
    } else {
        pos = write_to(sql, pos, b"'");
        pos = write_escaped(sql, pos, val.as_bytes());
        pos = write_to(sql, pos, b"'");
    }
    pos
}

fn is_numeric(s: &str) -> bool {
    if s.is_empty() { return false; }
    let bytes = s.as_bytes();
    let start = if bytes[0] == b'-' { 1 } else { 0 };
    if start >= bytes.len() { return false; }
    bytes[start..].iter().all(|&b| b >= b'0' && b <= b'9')
}

fn tokenize<'a>(input: &'a str, tokens: &mut [(&'a str, usize, usize); 32]) -> usize {
    let mut count = 0;
    let mut i = 0;
    let bytes = input.as_bytes();
    let len = bytes.len();

    while i < len && count < 32 {
        // Skip whitespace
        while i < len && bytes[i] == b' ' { i += 1; }
        if i >= len { break; }

        let start = i;
        // Handle quoted strings as single token
        if bytes[i] == b'\'' || bytes[i] == b'"' {
            let quote = bytes[i];
            i += 1;
            while i < len && bytes[i] != quote { i += 1; }
            if i < len { i += 1; }
        } else {
            while i < len && bytes[i] != b' ' { i += 1; }
        }
        tokens[count] = (&input[start..i], start, i);
        count += 1;
    }
    count
}

fn eq_ci(a: &str, b: &str) -> bool {
    if a.len() != b.len() { return false; }
    a.as_bytes().iter().zip(b.as_bytes()).all(|(&x, &y)| {
        x.to_ascii_lowercase() == y.to_ascii_lowercase()
    })
}

fn write_to(sql: &mut [u8; SQL_MAX], pos: usize, data: &[u8]) -> usize {
    if pos >= SQL_MAX - 1 { return pos; }
    let avail = SQL_MAX - pos - 1;
    let len = if data.len() > avail { avail } else { data.len() };
    sql[pos..pos + len].copy_from_slice(&data[..len]);
    sql[pos + len] = 0;
    pos + len
}

fn write_sql(sql: &mut [u8; SQL_MAX], data: &[u8]) -> Option<usize> {
    let pos = write_to(sql, 0, data);
    Some(pos)
}

fn write_fmt(sql: &mut [u8; SQL_MAX], parts: &[&[u8]]) -> Option<usize> {
    let mut pos = 0;
    for part in parts {
        pos = write_to(sql, pos, part);
    }
    Some(pos)
}

fn to_str(bytes: &[u8]) -> &str {
    let len = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    core::str::from_utf8(&bytes[..len]).unwrap_or("")
}
