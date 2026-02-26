/// Full graphical desktop — taskbar, start menu, and 12+ application windows.
/// Port of the C `gui/desktop.c`.

use core::fmt::Write;
use crate::drivers::mouse;
use crate::drivers::keyboard;
use crate::drivers::serial;
use crate::drivers::font::{FONT_WIDTH, FONT_HEIGHT};
use crate::mm::{heap, pmm};
use crate::db::database::{self, db_get_table_count, db_get_schema_by_id};
use crate::db::query::query_execute;
use crate::db::record::FieldValue;
use crate::arch::x86_64::{pit, cpu};
use crate::crypto::random;
use crate::cap;
use crate::serial_println;
use crate::shell::shell_main;

use super::graphics::*;
use super::event::*;
use super::window::*;
use super::compositor::*;
use super::widgets::*;

// ---- Taskbar ----
const TASKBAR_HEIGHT: u16 = 28;
const TASKBAR_BG: u32     = 0xFF16162E;
const TASKBAR_BORDER: u32 = 0xFF333355;
const CLIENT_BG: u32      = 0xFF1A1A2E;

// ---- Menu ----
const MENU_ITEMS: usize = 18;
static MENU_LABELS: [&str; MENU_ITEMS] = [
    "Terminal",             // 0  (NEW)
    "Query Console",        // 1
    "Table Browser",        // 2
    "Data Grid",            // 3
    "---",                  // 4 separator
    "VaultPad Editor",      // 5
    "Calculator",           // 6
    "Object Inspector",     // 7
    "---",                  // 8 separator
    "Security Dashboard",   // 9
    "Audit Log",            // 10
    "Capability Manager",   // 11
    "Object Manager",       // 12
    "---",                  // 13 separator
    "Process Manager",      // 14
    "System Status",        // 15
    "---",                  // 16 separator
    "Exit to Shell",        // 17
];

static mut MENU_OPEN: bool = false;
static mut GUI_RUNNING: bool = false;

// ---- Formatting helper (stack-allocated, no alloc) ----
struct FmtBuf {
    buf: [u8; 256],
    pos: usize,
}

impl FmtBuf {
    fn new() -> Self { FmtBuf { buf: [0; 256], pos: 0 } }
    fn as_str(&self) -> &str {
        core::str::from_utf8(&self.buf[..self.pos]).unwrap_or("")
    }
    fn push_str(&mut self, s: &str) {
        let bytes = s.as_bytes();
        let len = bytes.len().min(self.buf.len() - self.pos);
        self.buf[self.pos..self.pos + len].copy_from_slice(&bytes[..len]);
        self.pos += len;
    }
}

impl core::fmt::Write for FmtBuf {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        self.push_str(s);
        Ok(())
    }
}

fn fmt_u64(val: u64) -> FmtBuf {
    let mut b = FmtBuf::new();
    let _ = write!(b, "{}", val);
    b
}

// ---- Helper: extract field value as string for listview ----
fn field_to_str(fv: &FieldValue, buf: &mut FmtBuf) {
    match fv {
        FieldValue::U64(v) => { let _ = write!(buf, "{}", v); }
        FieldValue::I64(v) => { let _ = write!(buf, "{}", v); }
        FieldValue::U32(v) => { let _ = write!(buf, "{}", v); }
        FieldValue::U8(v)  => { let _ = write!(buf, "{}", v); }
        FieldValue::Bool(v) => { buf.push_str(if *v { "true" } else { "false" }); }
        FieldValue::Str(s) => {
            let txt = s.as_str();
            let trunc = if txt.len() > 40 { &txt[..40] } else { txt };
            buf.push_str(trunc);
        }
        FieldValue::Blob(_) => { buf.push_str("..."); }
    }
}

/// Populate a listview widget from a query result.
fn populate_lv_from_query(w: &mut Widget, sql: &str) {
    let result = query_execute(sql, 0);
    listview_clear(w);

    if result.error_code != 0 {
        listview_add_item(w, result.error_msg_str());
        return;
    }

    for row in result.rows.iter() {
        let mut line = FmtBuf::new();
        let max_fields = if row.field_count > 6 { 6 } else { row.field_count };
        for f in 0..max_fields as usize {
            if let Some(ref fv) = row.fields[f] {
                if f > 0 { line.push_str(" | "); }
                field_to_str(fv, &mut line);
            }
        }
        listview_add_item(w, line.as_str());
    }

    let mut summary = FmtBuf::new();
    let _ = write!(summary, "-- {} row(s) --", result.rows.len());
    listview_add_item(w, summary.as_str());
}

fn col_type_name(ct: vaultos_shared::db_types::ColumnType) -> &'static str {
    use vaultos_shared::db_types::ColumnType;
    match ct {
        ColumnType::U64  => "U64",
        ColumnType::I64  => "I64",
        ColumnType::Str  => "STR",
        ColumnType::Blob => "BLOB",
        ColumnType::Bool => "BOOL",
        ColumnType::U32  => "U32",
        ColumnType::U8   => "U8",
    }
}

// ===========================================================================
// ---- Query Console ----
// ===========================================================================
static mut QC_WIDGETS: WidgetSet = WidgetSet::new();
const QC_TEXTBOX: usize = 0;
const QC_EXEC_BTN: usize = 1;
const QC_TMPL_BTN: usize = 2;
const QC_LISTVIEW: usize = 3;
static mut QC_TMPL_IDX: usize = 0;

const QC_TEMPLATES: [&str; 6] = [
    "SHOW TABLES",
    "SELECT * FROM SystemTable",
    "SELECT * FROM ProcessTable",
    "SELECT * FROM CapabilityTable",
    "DESCRIBE SystemTable",
    "DESCRIBE ProcessTable",
];

fn qc_paint(win: &mut Window) {
    wm_clear_canvas(win, CLIENT_BG);
    unsafe { QC_WIDGETS.draw_all(win); }
}

fn qc_event(win: &mut Window, ev: &mut GuiEvent) {
    if ev.etype == EventType::Close {
        wm_destroy_window(win.id);
        return;
    }
    unsafe {
        let action = QC_WIDGETS.dispatch(ev);
        match action {
            WidgetAction::Clicked(idx) if idx == QC_EXEC_BTN => {
                if let Some(ref tb) = QC_WIDGETS.widgets[QC_TEXTBOX] {
                    let sql = widget_get_text(tb);
                    if !sql.is_empty() {
                        if let Some(ref mut lv) = QC_WIDGETS.widgets[QC_LISTVIEW] {
                            populate_lv_from_query(lv, sql);
                        }
                    }
                }
            }
            WidgetAction::Clicked(idx) if idx == QC_TMPL_BTN => {
                let tmpl = QC_TEMPLATES[QC_TMPL_IDX];
                QC_TMPL_IDX = (QC_TMPL_IDX + 1) % QC_TEMPLATES.len();
                if let Some(ref mut tb) = QC_WIDGETS.widgets[QC_TEXTBOX] {
                    widget_set_text(tb, tmpl);
                }
            }
            _ => {}
        }
    }
}

fn open_query_console() {
    unsafe {
        QC_WIDGETS.clear();
        QC_TMPL_IDX = 0;
    }
    let id = match wm_create_window("Query Console", 100, 60, 540, 400, Some(qc_event), Some(qc_paint)) {
        Some(v) => v,
        None => return,
    };
    let cw;
    if let Some(win) = wm_get_window(id) { cw = win.client_w; } else { return; }

    unsafe {
        QC_WIDGETS.add_textbox(4, 4, cw as i16 - 160, 24);
        if let Some(ref mut w) = QC_WIDGETS.widgets[QC_TEXTBOX] { w.focused = true; }
        QC_WIDGETS.add_button(cw as i16 - 152, 4, 68, 24, "Execute");
        QC_WIDGETS.add_button(cw as i16 - 80, 4, 76, 24, "Template");
        QC_WIDGETS.add_listview(4, 34, cw as i16 - 8, 400 - 26 - 40);
    }
}

// ===========================================================================
// ---- Table Browser ----
// ===========================================================================
static mut TB_WIDGETS: WidgetSet = WidgetSet::new();
const TB_REFRESH_BTN: usize = 0;
const TB_VIEWALL_BTN: usize = 1;
const TB_LBL: usize = 2;
const TB_SEARCH_BOX: usize = 3;
const TB_SEARCH_BTN: usize = 4;
const TB_TABLE_LIST: usize = 5;
const TB_DETAIL_LIST: usize = 6;

fn tb_refresh_tables() {
    unsafe {
        if let Some(ref mut lv) = TB_WIDGETS.widgets[TB_TABLE_LIST] {
            listview_clear(lv);
            let count = db_get_table_count();
            for i in 0..count {
                if let Some(s) = db_get_schema_by_id(i) {
                    listview_add_item(lv, s.name_str());
                }
            }
        }
    }
}

fn tb_paint(win: &mut Window) {
    wm_clear_canvas(win, CLIENT_BG);
    unsafe { TB_WIDGETS.draw_all(win); }
}

fn tb_event(win: &mut Window, ev: &mut GuiEvent) {
    if ev.etype == EventType::Close {
        wm_destroy_window(win.id);
        return;
    }
    unsafe {
        let action = TB_WIDGETS.dispatch(ev);
        match action {
            WidgetAction::Clicked(idx) if idx == TB_REFRESH_BTN => {
                tb_refresh_tables();
            }
            WidgetAction::Clicked(idx) if idx == TB_VIEWALL_BTN => {
                if let Some(ref tl) = TB_WIDGETS.widgets[TB_TABLE_LIST] {
                    let sel = tl.lv_selected;
                    if sel >= 0 {
                        if let Some(s) = db_get_schema_by_id(sel as u32) {
                            let mut sql = FmtBuf::new();
                            let _ = write!(sql, "SELECT * FROM {}", s.name_str());
                            if let Some(ref mut dl) = TB_WIDGETS.widgets[TB_DETAIL_LIST] {
                                populate_lv_from_query(dl, sql.as_str());
                            }
                        }
                    }
                }
            }
            WidgetAction::Clicked(idx) if idx == TB_SEARCH_BTN => {
                // Get selected table and search term
                let sel = TB_WIDGETS.widgets[TB_TABLE_LIST].as_ref()
                    .map(|w| w.lv_selected).unwrap_or(-1);
                let query_str = TB_WIDGETS.widgets[TB_SEARCH_BOX].as_ref()
                    .map(|w| widget_get_text(w)).unwrap_or("");
                if sel >= 0 && !query_str.is_empty() {
                    if let Some(s) = db_get_schema_by_id(sel as u32) {
                        // Parse "col=val"
                        if let Some(eq_pos) = query_str.find('=') {
                            let col = &query_str[..eq_pos];
                            let val = &query_str[eq_pos + 1..];
                            let mut sql = FmtBuf::new();
                            // Simple numeric check
                            let numeric = !val.is_empty() && val.bytes().all(|b| b.is_ascii_digit() || b == b'-');
                            if numeric {
                                let _ = write!(sql, "SELECT * FROM {} WHERE {} = {}", s.name_str(), col, val);
                            } else {
                                let _ = write!(sql, "SELECT * FROM {} WHERE {} = '{}'", s.name_str(), col, val);
                            }
                            if let Some(ref mut dl) = TB_WIDGETS.widgets[TB_DETAIL_LIST] {
                                populate_lv_from_query(dl, sql.as_str());
                            }
                        } else {
                            if let Some(ref mut dl) = TB_WIDGETS.widgets[TB_DETAIL_LIST] {
                                listview_clear(dl);
                                listview_add_item(dl, "Search format: column=value");
                            }
                        }
                    }
                }
            }
            WidgetAction::Selected(idx, sel) if idx == TB_TABLE_LIST => {
                // Show schema for selected table
                if let Some(schema) = db_get_schema_by_id(sel as u32) {
                    if let Some(ref mut dl) = TB_WIDGETS.widgets[TB_DETAIL_LIST] {
                        listview_clear(dl);
                        let mut hdr = FmtBuf::new();
                        let _ = write!(hdr, "Table: {} ({} columns){}",
                            schema.name_str(), schema.column_count,
                            if schema.encrypted { " [ENCRYPTED]" } else { "" });
                        listview_add_item(dl, hdr.as_str());
                        listview_add_item(dl, "---");
                        for c in 0..schema.column_count as usize {
                            let col = &schema.columns[c];
                            let mut line = FmtBuf::new();
                            let _ = write!(line, "  {} {} {}{}",
                                col.name_str(), col_type_name(col.col_type),
                                if col.primary_key { "PK " } else { "" },
                                if col.not_null { "NOT NULL" } else { "" });
                            listview_add_item(dl, line.as_str());
                        }
                    }
                }
            }
            _ => {}
        }
    }
}

fn open_table_browser() {
    unsafe { TB_WIDGETS.clear(); }
    let id = match wm_create_window("Table Browser", 150, 80, 600, 400, Some(tb_event), Some(tb_paint)) {
        Some(v) => v,
        None => return,
    };
    let cw;
    let ch;
    if let Some(win) = wm_get_window(id) { cw = win.client_w; ch = win.client_h; } else { return; }

    unsafe {
        TB_WIDGETS.add_button(4, 2, 72, 22, "Refresh");
        TB_WIDGETS.add_button(80, 2, 72, 22, "View All");
        TB_WIDGETS.add_label(158, 5, "Tables", 0xFFFFCC00, CLIENT_BG);
        TB_WIDGETS.add_textbox(190, 2, cw as i16 - 270, 22);
        TB_WIDGETS.add_button(cw as i16 - 76, 2, 72, 22, "Search");
        TB_WIDGETS.add_listview(4, 28, 180, ch as i16 - 34);
        TB_WIDGETS.add_listview(190, 28, cw as i16 - 196, ch as i16 - 34);
    }
    tb_refresh_tables();
}

// ===========================================================================
// ---- Process Manager ----
// ===========================================================================
static mut PM_WIDGETS: WidgetSet = WidgetSet::new();
const PM_REFRESH_BTN: usize = 0;
const PM_KILL_BTN: usize = 1;
const PM_LISTVIEW: usize = 2;

fn pm_refresh_list() {
    unsafe {
        if let Some(ref mut lv) = PM_WIDGETS.widgets[PM_LISTVIEW] {
            populate_lv_from_query(lv, "SELECT * FROM ProcessTable");
        }
    }
}

fn pm_paint(win: &mut Window) {
    wm_clear_canvas(win, CLIENT_BG);
    unsafe { PM_WIDGETS.draw_all(win); }
}

fn pm_event(win: &mut Window, ev: &mut GuiEvent) {
    if ev.etype == EventType::Close { wm_destroy_window(win.id); return; }
    unsafe {
        let action = PM_WIDGETS.dispatch(ev);
        match action {
            WidgetAction::Clicked(idx) if idx == PM_REFRESH_BTN => pm_refresh_list(),
            WidgetAction::Clicked(idx) if idx == PM_KILL_BTN => {
                // Kill not implemented in GUI for safety
            }
            _ => {}
        }
    }
}

fn open_process_manager() {
    unsafe { PM_WIDGETS.clear(); }
    let id = match wm_create_window("Process Manager", 120, 70, 480, 340, Some(pm_event), Some(pm_paint)) {
        Some(v) => v,
        None => return,
    };
    let cw;
    let ch;
    if let Some(win) = wm_get_window(id) { cw = win.client_w; ch = win.client_h; } else { return; }

    unsafe {
        PM_WIDGETS.add_button(4, 2, 72, 22, "Refresh");
        PM_WIDGETS.add_button(80, 2, 72, 22, "Kill");
        PM_WIDGETS.add_listview(4, 28, cw as i16 - 8, ch as i16 - 34);
    }
    pm_refresh_list();
}

// ===========================================================================
// ---- System Status ----
// ===========================================================================
static mut SS_WIDGETS: WidgetSet = WidgetSet::new();

fn ss_paint(win: &mut Window) {
    wm_clear_canvas(win, CLIENT_BG);
    unsafe {
        // Update dynamic labels
        let update = |idx: usize, text: &str| {
            if let Some(ref mut w) = SS_WIDGETS.widgets[idx] {
                widget_set_text(w, text);
                w.w = (text.len() as i16) * FONT_WIDTH as i16;
            }
        };

        let ms = pit::pit_get_uptime_ms();
        let mut b = FmtBuf::new();
        let _ = write!(b, "Uptime:       {}.{}s", ms / 1000, (ms % 1000) / 100);
        update(1, b.as_str());

        b = FmtBuf::new();
        let _ = write!(b, "Heap Used:    {} bytes", heap::heap_used());
        update(2, b.as_str());

        b = FmtBuf::new();
        let _ = write!(b, "Heap Free:    {} bytes", heap::heap_free());
        update(3, b.as_str());

        b = FmtBuf::new();
        let _ = write!(b, "PMM Pages:    {} / {}", pmm::pmm_get_free_pages(), pmm::pmm_get_total_pages());
        update(4, b.as_str());

        b = FmtBuf::new();
        let _ = write!(b, "Tables:       {} (encrypted)", db_get_table_count());
        update(5, b.as_str());

        b = FmtBuf::new();
        let _ = write!(b, "Capabilities: {} active", cap::cap_table_count());
        update(6, b.as_str());

        let rng_text = if random::random_hw_available() {
            "RNG Source:   Hardware (RDRAND)"
        } else {
            "RNG Source:   Software (xorshift128+)"
        };
        update(7, rng_text);
        if let Some(ref mut w) = SS_WIDGETS.widgets[7] {
            w.fg = if random::random_hw_available() { 0xFF00CC66 } else { 0xFFCCCC00 };
        }

        update(8, "Encryption:   AES-128-CBC + HMAC-SHA256");

        SS_WIDGETS.draw_all(win);
    }
}

fn ss_event(win: &mut Window, ev: &mut GuiEvent) {
    if ev.etype == EventType::Close { wm_destroy_window(win.id); return; }
    unsafe { SS_WIDGETS.dispatch(ev); }
}

fn open_system_status() {
    unsafe { SS_WIDGETS.clear(); }
    wm_create_window("System Status", 200, 100, 420, 340, Some(ss_event), Some(ss_paint));

    let fg = 0xFF00DDAA;
    let bg = CLIENT_BG;
    let mut y: i16 = 10;

    unsafe {
        SS_WIDGETS.add_label(12, y, "VaultOS System Status", 0xFFFFCC00, bg);
        y += 28;
        SS_WIDGETS.add_label(12, y, "Uptime:       ...", fg, bg); y += 22;
        SS_WIDGETS.add_label(12, y, "Heap Used:    ...", fg, bg); y += 22;
        SS_WIDGETS.add_label(12, y, "Heap Free:    ...", fg, bg); y += 22;
        SS_WIDGETS.add_label(12, y, "PMM Pages:    ...", fg, bg); y += 22;
        SS_WIDGETS.add_label(12, y, "Tables:       ...", fg, bg); y += 22;
        SS_WIDGETS.add_label(12, y, "Capabilities: ...", fg, bg); y += 22;
        SS_WIDGETS.add_label(12, y, "RNG Source:   ...", fg, bg); y += 22;
        SS_WIDGETS.add_label(12, y, "Encryption:   ...", 0xFF00CC66, bg); y += 28;
        SS_WIDGETS.add_label(12, y, "CPU: x86-64 (qemu64)", 0xFF808080, bg);
    }
}

// ===========================================================================
// ---- Security Dashboard ----
// ===========================================================================
static mut SD_WIDGETS: WidgetSet = WidgetSet::new();
const SD_VIEW_AUDIT_BTN: usize = 14;

fn sd_paint(win: &mut Window) {
    wm_clear_canvas(win, CLIENT_BG);
    unsafe {
        let update = |idx: usize, text: &str| {
            if let Some(ref mut w) = SD_WIDGETS.widgets[idx] {
                widget_set_text(w, text);
                w.w = (text.len() as i16) * FONT_WIDTH as i16;
            }
        };

        // Update table SEALED labels (indices 3..9)
        let tc = db_get_table_count();
        for i in 0..tc.min(6) {
            if let Some(s) = db_get_schema_by_id(i) {
                let mut b = FmtBuf::new();
                let _ = write!(b, "  {} SEALED", s.name_str());
                update(3 + i as usize, b.as_str());
            }
        }

        let mut b = FmtBuf::new();
        let _ = write!(b, "Active Tokens: {}", cap::cap_table_count());
        update(11, b.as_str());

        let rng_text = if random::random_hw_available() { "RNG: Hardware (RDRAND)" }
            else { "RNG: Software (xorshift128+)" };
        update(12, rng_text);
        if let Some(ref mut w) = SD_WIDGETS.widgets[12] {
            w.fg = if random::random_hw_available() { 0xFF00CC66 } else { 0xFFCCCC00 };
        }

        // Audit count
        let ar = query_execute("SELECT * FROM AuditTable", 0);
        b = FmtBuf::new();
        let _ = write!(b, "Audit Events: {} logged", ar.rows.len());
        update(13, b.as_str());

        SD_WIDGETS.draw_all(win);
    }
}

fn sd_event(win: &mut Window, ev: &mut GuiEvent) {
    if ev.etype == EventType::Close { wm_destroy_window(win.id); return; }
    unsafe {
        let action = SD_WIDGETS.dispatch(ev);
        if let WidgetAction::Clicked(idx) = action {
            if idx == SD_VIEW_AUDIT_BTN { open_audit_viewer(); }
        }
    }
}

fn open_security_dashboard() {
    unsafe { SD_WIDGETS.clear(); }
    wm_create_window("Security Dashboard", 80, 50, 500, 380, Some(sd_event), Some(sd_paint));

    let bg = CLIENT_BG;
    let mut y: i16 = 8;
    unsafe {
        SD_WIDGETS.add_label(12, y, "ENCRYPTION STATUS", 0xFFFFCC00, bg); y += 24;
        SD_WIDGETS.add_label(12, y, "Algorithm: AES-128-CBC", 0xFF00CC66, bg); y += 18;
        SD_WIDGETS.add_label(12, y, "Key Derivation: HMAC-SHA256", 0xFF00CC66, bg); y += 24;
        // 6 table labels (indices 3..8)
        for _ in 0..6 {
            SD_WIDGETS.add_label(12, y, "  ...", 0xFF00CC66, bg); y += 18;
        }
        y += 8;
        SD_WIDGETS.add_label(12, y, "SECURITY TOKENS", 0xFFFFCC00, bg); y += 22; // idx 9
        SD_WIDGETS.add_label(12, y, "Capability System: HMAC-sealed", 0xFF00DDAA, bg); y += 18; // 10
        SD_WIDGETS.add_label(12, y, "Active Tokens: ...", 0xFF00DDAA, bg); y += 22; // 11
        SD_WIDGETS.add_label(12, y, "RNG: ...", 0xFF00CC66, bg); y += 18; // 12
        SD_WIDGETS.add_label(12, y, "Audit Events: ...", 0xFF00DDAA, bg); y += 28; // 13
        SD_WIDGETS.add_button(12, y, 140, 24, "View Audit Log"); // 14
    }
}

// ===========================================================================
// ---- Audit Log Viewer ----
// ===========================================================================
static mut AL_WIDGETS: WidgetSet = WidgetSet::new();
const AL_FILTER_BOX: usize = 0;
const AL_REFRESH_BTN: usize = 1;
const AL_LBL: usize = 2;
const AL_LISTVIEW: usize = 3;

fn al_refresh() {
    unsafe {
        let result = query_execute("SELECT * FROM AuditTable", 0);
        if let Some(ref mut lv) = AL_WIDGETS.widgets[AL_LISTVIEW] {
            listview_clear(lv);
            if result.rows.is_empty() {
                listview_add_item(lv, "No audit events.");
                return;
            }
            for row in result.rows.iter() {
                let ts = match row.fields[1] { Some(FieldValue::U64(v)) => v, _ => 0 };
                let apid = match row.fields[2] { Some(FieldValue::U64(v)) => v, _ => 0 };
                let action = match row.fields[3] { Some(FieldValue::Str(ref s)) => s.as_str(), _ => "?" };
                let res_str = match row.fields[5] { Some(FieldValue::Str(ref s)) => s.as_str(), _ => "" };

                let secs = ts / 1000;
                let mins = secs / 60;
                let s = secs % 60;
                let mut line = FmtBuf::new();
                let _ = write!(line, "[{:02}:{:02}] {} PID:{} {}", mins, s, action, apid, res_str);
                listview_add_item(lv, line.as_str());
            }
            let mut summary = FmtBuf::new();
            let _ = write!(summary, "-- {} entries --", lv.lv_count);
            listview_add_item(lv, summary.as_str());
        }
    }
}

fn al_paint(win: &mut Window) {
    wm_clear_canvas(win, CLIENT_BG);
    unsafe { AL_WIDGETS.draw_all(win); }
}

fn al_event(win: &mut Window, ev: &mut GuiEvent) {
    if ev.etype == EventType::Close { wm_destroy_window(win.id); return; }
    unsafe {
        let action = AL_WIDGETS.dispatch(ev);
        if let WidgetAction::Clicked(idx) = action {
            if idx == AL_REFRESH_BTN { al_refresh(); }
        }
    }
}

fn open_audit_viewer() {
    unsafe { AL_WIDGETS.clear(); }
    let id = match wm_create_window("Audit Log", 140, 60, 580, 400, Some(al_event), Some(al_paint)) {
        Some(v) => v,
        None => return,
    };
    let cw;
    let ch;
    if let Some(win) = wm_get_window(id) { cw = win.client_w; ch = win.client_h; } else { return; }

    unsafe {
        AL_WIDGETS.add_textbox(4, 4, cw as i16 - 160, 24);
        AL_WIDGETS.add_button(cw as i16 - 152, 4, 72, 24, "Refresh");
        AL_WIDGETS.add_label(cw as i16 - 76, 8, "Filter", 0xFF808080, CLIENT_BG);
        AL_WIDGETS.add_listview(4, 34, cw as i16 - 8, ch as i16 - 40);
    }
    al_refresh();
}

// ===========================================================================
// ---- Capability Manager ----
// ===========================================================================
static mut CM_WIDGETS: WidgetSet = WidgetSet::new();
const CM_REFRESH_BTN: usize = 0;
const CM_REVOKE_BTN: usize = 1;
const CM_LISTVIEW: usize = 2;

fn cm_refresh() {
    unsafe {
        if let Some(ref mut lv) = CM_WIDGETS.widgets[CM_LISTVIEW] {
            let result = query_execute("SELECT * FROM CapabilityTable", 0);
            listview_clear(lv);
            if result.rows.is_empty() {
                listview_add_item(lv, "No capabilities found.");
                return;
            }
            listview_add_item(lv, "  CAP_ID  OBJ_ID  PID    RIGHTS  STATUS");
            for row in result.rows.iter() {
                let cap_id = match row.fields[0] { Some(FieldValue::U64(v)) => v, _ => 0 };
                let obj_id = match row.fields[1] { Some(FieldValue::U64(v)) => v, _ => 0 };
                let own_pid = match row.fields[2] { Some(FieldValue::U64(v)) => v, _ => 0 };
                let rights = match row.fields[3] { Some(FieldValue::U32(v)) => v, _ => 0 };
                let revoked = match row.fields[5] { Some(FieldValue::Bool(v)) => v, _ => false };

                let mut r_str = FmtBuf::new();
                if rights & 0x01 != 0 { r_str.push_str("R"); }
                if rights & 0x02 != 0 { r_str.push_str("W"); }
                if rights & 0x04 != 0 { r_str.push_str("X"); }
                if rights & 0x08 != 0 { r_str.push_str("D"); }
                if rights & 0x10 != 0 { r_str.push_str("G"); }
                if r_str.pos == 0 { r_str.push_str("NONE"); }

                let mut line = FmtBuf::new();
                let _ = write!(line, "  {} {} {} {} {}",
                    cap_id, obj_id, own_pid, r_str.as_str(),
                    if revoked { "REVOKED" } else { "ACTIVE" });
                listview_add_item(lv, line.as_str());
            }
            let mut summary = FmtBuf::new();
            let _ = write!(summary, "-- {} capability(s) --", result.rows.len());
            listview_add_item(lv, summary.as_str());
        }
    }
}

fn cm_paint(win: &mut Window) {
    wm_clear_canvas(win, CLIENT_BG);
    unsafe { CM_WIDGETS.draw_all(win); }
}

fn cm_event(win: &mut Window, ev: &mut GuiEvent) {
    if ev.etype == EventType::Close { wm_destroy_window(win.id); return; }
    unsafe {
        let action = CM_WIDGETS.dispatch(ev);
        match action {
            WidgetAction::Clicked(idx) if idx == CM_REFRESH_BTN => cm_refresh(),
            WidgetAction::Clicked(idx) if idx == CM_REVOKE_BTN => {
                // Revoke selected capability
                if let Some(ref lv) = CM_WIDGETS.widgets[CM_LISTVIEW] {
                    let sel = lv.lv_selected;
                    if sel > 0 { // 0 = header
                        let result = query_execute("SELECT * FROM CapabilityTable", 0);
                        let data_idx = (sel - 1) as usize;
                        if data_idx < result.rows.len() {
                            let cap_id = match result.rows[data_idx].fields[0] {
                                Some(FieldValue::U64(v)) => v, _ => 0
                            };
                            let mut sql = FmtBuf::new();
                            let _ = write!(sql, "REVOKE {}", cap_id);
                            let _ = query_execute(sql.as_str(), 0);
                            cm_refresh();
                        }
                    }
                }
            }
            _ => {}
        }
    }
}

fn open_cap_manager() {
    unsafe { CM_WIDGETS.clear(); }
    let id = match wm_create_window("Capability Manager", 100, 70, 560, 380, Some(cm_event), Some(cm_paint)) {
        Some(v) => v,
        None => return,
    };
    let cw;
    let ch;
    if let Some(win) = wm_get_window(id) { cw = win.client_w; ch = win.client_h; } else { return; }

    unsafe {
        CM_WIDGETS.add_button(4, 2, 72, 22, "Refresh");
        CM_WIDGETS.add_button(80, 2, 100, 22, "Revoke Sel.");
        CM_WIDGETS.add_listview(4, 28, cw as i16 - 8, ch as i16 - 34);
    }
    cm_refresh();
}

// ===========================================================================
// ---- Object Manager ----
// ===========================================================================
static mut OM_WIDGETS: WidgetSet = WidgetSet::new();
const OM_REFRESH_BTN: usize = 0;
const OM_DELETE_BTN: usize = 1;
const OM_LISTVIEW: usize = 2;

fn om_refresh() {
    unsafe {
        if let Some(ref mut lv) = OM_WIDGETS.widgets[OM_LISTVIEW] {
            let result = query_execute("SELECT * FROM ObjectTable", 0);
            listview_clear(lv);
            if result.rows.is_empty() {
                listview_add_item(lv, "No objects found.");
                return;
            }
            listview_add_item(lv, "  TYPE       NAME             DATA");
            for row in result.rows.iter() {
                let name = match row.fields[1] { Some(FieldValue::Str(ref s)) => s.as_str(), _ => "?" };
                let otype = match row.fields[2] { Some(FieldValue::Str(ref s)) => s.as_str(), _ => "?" };
                let data = match row.fields[3] { Some(FieldValue::Str(ref s)) => s.as_str(), _ => "" };
                let preview = if data.len() > 28 { &data[..28] } else { data };
                let mut line = FmtBuf::new();
                let _ = write!(line, "  {} {} {}", otype, name, preview);
                listview_add_item(lv, line.as_str());
            }
            let mut summary = FmtBuf::new();
            let _ = write!(summary, "-- {} object(s) --", result.rows.len());
            listview_add_item(lv, summary.as_str());
        }
    }
}

fn om_paint(win: &mut Window) {
    wm_clear_canvas(win, CLIENT_BG);
    unsafe { OM_WIDGETS.draw_all(win); }
}

fn om_event(win: &mut Window, ev: &mut GuiEvent) {
    if ev.etype == EventType::Close { wm_destroy_window(win.id); return; }
    unsafe {
        let action = OM_WIDGETS.dispatch(ev);
        match action {
            WidgetAction::Clicked(idx) if idx == OM_REFRESH_BTN => om_refresh(),
            WidgetAction::Clicked(idx) if idx == OM_DELETE_BTN => {
                if let Some(ref lv) = OM_WIDGETS.widgets[OM_LISTVIEW] {
                    let sel = lv.lv_selected;
                    if sel > 0 {
                        let result = query_execute("SELECT * FROM ObjectTable", 0);
                        let data_idx = (sel - 1) as usize;
                        if data_idx < result.rows.len() {
                            let name = match result.rows[data_idx].fields[1] {
                                Some(FieldValue::Str(ref s)) => s.as_str(),
                                _ => return,
                            };
                            let mut sql = FmtBuf::new();
                            let _ = write!(sql, "DELETE FROM ObjectTable WHERE name = '{}'", name);
                            let _ = query_execute(sql.as_str(), 0);
                            om_refresh();
                        }
                    }
                }
            }
            _ => {}
        }
    }
}

fn open_object_manager() {
    unsafe { OM_WIDGETS.clear(); }
    let id = match wm_create_window("Object Manager", 120, 80, 520, 380, Some(om_event), Some(om_paint)) {
        Some(v) => v,
        None => return,
    };
    let cw;
    let ch;
    if let Some(win) = wm_get_window(id) { cw = win.client_w; ch = win.client_h; } else { return; }

    unsafe {
        OM_WIDGETS.add_button(4, 2, 72, 22, "Refresh");
        OM_WIDGETS.add_button(80, 2, 100, 22, "Delete Sel.");
        OM_WIDGETS.add_listview(4, 28, cw as i16 - 8, ch as i16 - 34);
    }
    om_refresh();
}

// ===========================================================================
// ---- Calculator ----
// ===========================================================================
static mut CALC_DISPLAY: [u8; 32] = [b'0', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
static mut CALC_VALUE: i64 = 0;
static mut CALC_OPERAND: i64 = 0;
static mut CALC_OP: u8 = 0;
static mut CALC_NEW_INPUT: bool = true;
static mut CALC_INPUT: [u8; 20] = [0; 20];
static mut CALC_INPUT_LEN: usize = 0;

const CALC_BTN_W: i16 = 48;
const CALC_BTN_H: i16 = 36;
const CALC_BTN_PAD: i16 = 4;
const CALC_ROWS: usize = 5;
const CALC_COLS: usize = 4;

static CALC_BUTTONS: [[u8; 4]; 5] = [
    [b'C', b'/', b'*', b'<'],
    [b'7', b'8', b'9', b'-'],
    [b'4', b'5', b'6', b'+'],
    [b'1', b'2', b'3', b'='],
    [b'0', b' ', b' ', b'='],
];

fn calc_set_display(s: &str) {
    unsafe {
        let len = s.len().min(31);
        CALC_DISPLAY[..len].copy_from_slice(&s.as_bytes()[..len]);
        CALC_DISPLAY[len] = 0;
    }
}

fn calc_display_str() -> &'static str {
    unsafe {
        let len = CALC_DISPLAY.iter().position(|&b| b == 0).unwrap_or(32);
        core::str::from_utf8(&CALC_DISPLAY[..len]).unwrap_or("0")
    }
}

fn calc_input_str() -> &'static str {
    unsafe {
        core::str::from_utf8(&CALC_INPUT[..CALC_INPUT_LEN]).unwrap_or("")
    }
}

fn calc_input_to_i64() -> i64 {
    let s = calc_input_str();
    let mut val: i64 = 0;
    let mut neg = false;
    for (i, &b) in s.as_bytes().iter().enumerate() {
        if i == 0 && b == b'-' { neg = true; continue; }
        if b >= b'0' && b <= b'9' {
            val = val * 10 + (b - b'0') as i64;
        }
    }
    if neg { -val } else { val }
}

fn calc_execute_pending() {
    unsafe {
        match CALC_OP {
            b'+' => CALC_VALUE += CALC_OPERAND,
            b'-' => CALC_VALUE -= CALC_OPERAND,
            b'*' => CALC_VALUE *= CALC_OPERAND,
            b'/' => {
                if CALC_OPERAND != 0 { CALC_VALUE /= CALC_OPERAND; }
                else { calc_set_display("Error: /0"); return; }
            }
            _ => {}
        }
        let mut b = FmtBuf::new();
        let _ = write!(b, "{}", CALC_VALUE);
        calc_set_display(b.as_str());
    }
}

fn calc_handle_button(btn: u8) {
    unsafe {
        if btn >= b'0' && btn <= b'9' {
            if CALC_NEW_INPUT {
                CALC_INPUT_LEN = 0;
                CALC_NEW_INPUT = false;
            }
            if CALC_INPUT_LEN < 18 {
                CALC_INPUT[CALC_INPUT_LEN] = btn;
                CALC_INPUT_LEN += 1;
                CALC_INPUT[CALC_INPUT_LEN] = 0;
            }
            calc_set_display(calc_input_str());
        } else if btn == b'C' {
            CALC_VALUE = 0;
            CALC_OPERAND = 0;
            CALC_OP = 0;
            CALC_INPUT_LEN = 0;
            calc_set_display("0");
            CALC_NEW_INPUT = true;
        } else if btn == b'<' {
            if CALC_INPUT_LEN > 0 {
                CALC_INPUT_LEN -= 1;
                CALC_INPUT[CALC_INPUT_LEN] = 0;
                if CALC_INPUT_LEN == 0 { calc_set_display("0"); }
                else { calc_set_display(calc_input_str()); }
            }
        } else if btn == b'+' || btn == b'-' || btn == b'*' || btn == b'/' {
            let val = calc_input_to_i64();
            if CALC_OP != 0 {
                CALC_OPERAND = val;
                calc_execute_pending();
            } else {
                CALC_VALUE = val;
            }
            CALC_OP = btn;
            CALC_NEW_INPUT = true;
        } else if btn == b'=' {
            CALC_OPERAND = calc_input_to_i64();
            if CALC_OP != 0 { calc_execute_pending(); }
            else { CALC_VALUE = CALC_OPERAND; }
            CALC_OP = 0;
            CALC_NEW_INPUT = true;
            let mut b = FmtBuf::new();
            let _ = write!(b, "{}", CALC_VALUE);
            calc_set_display(b.as_str());
            // Copy display to input
            let ds = calc_display_str();
            CALC_INPUT_LEN = ds.len().min(19);
            CALC_INPUT[..CALC_INPUT_LEN].copy_from_slice(&ds.as_bytes()[..CALC_INPUT_LEN]);
        }
    }
}

fn calc_paint(win: &mut Window) {
    wm_clear_canvas(win, CLIENT_BG);
    let cw = win.client_w;
    let ch = win.client_h;
    // Display area
    let disp_x: i16 = 8;
    let disp_y: i16 = 8;
    let disp_w: i16 = cw as i16 - 16;
    let disp_h: i16 = 44;
    canvas_fill(&mut win.canvas, cw, ch, disp_x, disp_y, disp_w, disp_h, 0xFF0A0A1A);
    canvas_rect(&mut win.canvas, cw, ch, disp_x, disp_y, disp_w, disp_h, 0xFF555577);

    // Right-aligned display text
    let ds = calc_display_str();
    let text_w = (ds.len() as i16) * FONT_WIDTH as i16;
    let text_x = disp_x + disp_w - text_w - 8;
    let text_y = disp_y + (disp_h - FONT_HEIGHT as i16) / 2;
    canvas_text(&mut win.canvas, cw, ch, text_x, text_y, ds, 0xFFFFCC00, 0xFF0A0A1A);

    // Operator indicator
    unsafe {
        if CALC_OP != 0 {
            let op_buf = [CALC_OP];
            let op_str = core::str::from_utf8(&op_buf).unwrap_or("");
            canvas_text(&mut win.canvas, cw, ch, disp_x + 6, text_y, op_str, 0xFF808080, 0xFF0A0A1A);
        }
    }

    // Button grid
    let mut grid_y = disp_y + disp_h + CALC_BTN_PAD + 4;
    for r in 0..CALC_ROWS {
        let mut bx: i16 = 8;
        for c in 0..CALC_COLS {
            let ch_btn = CALC_BUTTONS[r][c];
            if ch_btn == b' ' { bx += CALC_BTN_W + CALC_BTN_PAD; continue; }

            let bw = if r == 4 && c == 0 { CALC_BTN_W * 3 + CALC_BTN_PAD * 2 } else { CALC_BTN_W };

            let (btn_bg, btn_fg) = if ch_btn >= b'0' && ch_btn <= b'9' {
                (0xFF2A2A4A, 0xFFFFFFFFu32)
            } else if ch_btn == b'C' {
                (0xFF663333, 0xFFFFAAAAu32)
            } else if ch_btn == b'=' {
                (0xFF1A4444, 0xFF00DDAAu32)
            } else if ch_btn == b'<' {
                (0xFF333355, 0xFFCCCCCCu32)
            } else {
                (0xFF333355, 0xFFFFCC00u32)
            };

            canvas_fill(&mut win.canvas, cw, ch, bx, grid_y, bw, CALC_BTN_H, btn_bg);
            canvas_rect(&mut win.canvas, cw, ch, bx, grid_y, bw, CALC_BTN_H, 0xFF555577);

            let btn_buf = [ch_btn];
            let btn_str = core::str::from_utf8(&btn_buf).unwrap_or("");
            let tx = bx + (bw - FONT_WIDTH as i16) / 2;
            let ty = grid_y + (CALC_BTN_H - FONT_HEIGHT as i16) / 2;
            canvas_text(&mut win.canvas, cw, ch, tx, ty, btn_str, btn_fg, btn_bg);

            bx += bw + CALC_BTN_PAD;
        }
        grid_y += CALC_BTN_H + CALC_BTN_PAD;
    }
}

fn calc_event(win: &mut Window, ev: &mut GuiEvent) {
    if ev.etype == EventType::Close { wm_destroy_window(win.id); return; }

    if ev.etype == EventType::KeyDown {
        let key = ev.key;
        if key >= b'0' && key <= b'9' { calc_handle_button(key); }
        else if key == b'+' || key == b'-' || key == b'*' || key == b'/' { calc_handle_button(key); }
        else if key == b'\n' || key == b'=' { calc_handle_button(b'='); }
        else if key == 0x08 { calc_handle_button(b'<'); }
        else if key == b'c' || key == b'C' { calc_handle_button(b'C'); }
    }

    if ev.etype == EventType::MouseDown {
        let disp_h_total: i16 = 8 + 44 + CALC_BTN_PAD + 4;
        let row = (ev.mouse_y - disp_h_total) / (CALC_BTN_H + CALC_BTN_PAD);
        let col = (ev.mouse_x - 8) / (CALC_BTN_W + CALC_BTN_PAD);
        if row >= 0 && (row as usize) < CALC_ROWS && col >= 0 && (col as usize) < CALC_COLS {
            let btn = CALC_BUTTONS[row as usize][col as usize];
            if btn != b' ' { calc_handle_button(btn); }
        }
    }
}

fn open_calculator() {
    unsafe {
        calc_set_display("0");
        CALC_VALUE = 0;
        CALC_OPERAND = 0;
        CALC_OP = 0;
        CALC_NEW_INPUT = true;
        CALC_INPUT_LEN = 0;
    }
    wm_create_window("Calculator", 200, 80, 260, 340, Some(calc_event), Some(calc_paint));
}

// ===========================================================================
// ---- Object Inspector / Hex Viewer ----
// ===========================================================================
static mut OI_WIDGETS: WidgetSet = WidgetSet::new();
const OI_REFRESH_BTN: usize = 0;
const OI_FILTER_BOX: usize = 1;
const OI_FILTER_BTN: usize = 2;
const OI_OBJ_LIST: usize = 3;

static mut OI_OBJ_NAME: [u8; 64] = [0; 64];
static mut OI_OBJ_TYPE: [u8; 64] = [0; 64];
static mut OI_OBJ_DATA: [u8; 256] = [0; 256];
static mut OI_OBJ_ID: u64 = 0;
static mut OI_OBJ_OWNER: u64 = 0;
static mut OI_OBJ_CREATED: u64 = 0;
static mut OI_HEX_SCROLL: i32 = 0;
static mut OI_HAS_SELECTION: bool = false;

fn oi_str(buf: &[u8]) -> &str {
    let len = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    core::str::from_utf8(&buf[..len]).unwrap_or("")
}

fn oi_refresh() {
    unsafe {
        let result = query_execute("SELECT * FROM ObjectTable", 0);
        if let Some(ref mut lv) = OI_WIDGETS.widgets[OI_OBJ_LIST] {
            listview_clear(lv);
            for row in result.rows.iter() {
                let name = match row.fields[1] { Some(FieldValue::Str(ref s)) => s.as_str(), _ => "?" };
                let otype = match row.fields[2] { Some(FieldValue::Str(ref s)) => s.as_str(), _ => "?" };
                let mut item = FmtBuf::new();
                let _ = write!(item, "[{}] {}", otype, name);
                listview_add_item(lv, item.as_str());
            }
        }
    }
}

fn oi_paint(win: &mut Window) {
    wm_clear_canvas(win, CLIENT_BG);
    unsafe { OI_WIDGETS.draw_all(win); }

    unsafe {
        if !OI_HAS_SELECTION { return; }

        let cw = win.client_w;
        let ch = win.client_h;

        let px: i16 = 168;
        let mut py: i16 = 28;

        canvas_text(&mut win.canvas, cw, ch, px, py, "METADATA", 0xFFFFCC00, CLIENT_BG);
        py += 22;

        let mut b = FmtBuf::new();
        let _ = write!(b, "ID:      {}", OI_OBJ_ID);
        canvas_text(&mut win.canvas, cw, ch, px, py, b.as_str(), 0xFF00DDAA, CLIENT_BG);
        py += FONT_HEIGHT as i16 + 2;

        b = FmtBuf::new();
        let _ = write!(b, "Name:    {}", oi_str(&OI_OBJ_NAME));
        canvas_text(&mut win.canvas, cw, ch, px, py, b.as_str(), 0xFF00DDAA, CLIENT_BG);
        py += FONT_HEIGHT as i16 + 2;

        b = FmtBuf::new();
        let _ = write!(b, "Type:    {}", oi_str(&OI_OBJ_TYPE));
        canvas_text(&mut win.canvas, cw, ch, px, py, b.as_str(), 0xFF00DDAA, CLIENT_BG);
        py += FONT_HEIGHT as i16 + 2;

        b = FmtBuf::new();
        let _ = write!(b, "Owner:   PID {}", OI_OBJ_OWNER);
        canvas_text(&mut win.canvas, cw, ch, px, py, b.as_str(), 0xFF00DDAA, CLIENT_BG);
        py += FONT_HEIGHT as i16 + 2;

        let secs = OI_OBJ_CREATED / 1000;
        let mins = secs / 60;
        let s = secs % 60;
        b = FmtBuf::new();
        let _ = write!(b, "Created: {:02}:{:02}", mins, s);
        canvas_text(&mut win.canvas, cw, ch, px, py, b.as_str(), 0xFF00DDAA, CLIENT_BG);
        py += FONT_HEIGHT as i16 + 6;

        // Separator
        canvas_hline(&mut win.canvas, cw, ch, px, py, cw as i16 - px - 8, 0xFF555577);
        py += 8;

        // Hex dump
        canvas_text(&mut win.canvas, cw, ch, px, py, "HEX DUMP", 0xFFFFCC00, CLIENT_BG);
        py += 22;

        let data = oi_str(&OI_OBJ_DATA);
        let data_bytes = data.as_bytes();
        let data_len = data_bytes.len();
        let bytes_per_row = 8;
        let total_rows = if data_len == 0 { 1 } else { (data_len + bytes_per_row - 1) / bytes_per_row };
        let visible_rows = ((ch as i16 - py - 4) / FONT_HEIGHT as i16) as i32;

        for r in 0..visible_rows {
            let dr = r + OI_HEX_SCROLL;
            if dr >= total_rows as i32 { break; }
            let offset = dr as usize * bytes_per_row;
            let ry = py + r as i16 * FONT_HEIGHT as i16;

            // Offset
            b = FmtBuf::new();
            let _ = write!(b, "{:04X} ", offset);
            canvas_text(&mut win.canvas, cw, ch, px, ry, b.as_str(), 0xFFFFCC00, CLIENT_BG);

            // Hex bytes
            let mut hx = px + 5 * FONT_WIDTH as i16 + 4;
            for byte in 0..bytes_per_row {
                if offset + byte < data_len {
                    b = FmtBuf::new();
                    let _ = write!(b, "{:02X} ", data_bytes[offset + byte]);
                    canvas_text(&mut win.canvas, cw, ch, hx, ry, b.as_str(), 0xFF00DDAA, CLIENT_BG);
                }
                hx += 3 * FONT_WIDTH as i16;
                if byte == 3 { hx += FONT_WIDTH as i16; }
            }

            // ASCII
            let ax = hx + FONT_WIDTH as i16;
            canvas_text(&mut win.canvas, cw, ch, ax, ry, "|", 0xFF555577, CLIENT_BG);
            let mut acx = ax + FONT_WIDTH as i16;
            for byte in 0..bytes_per_row {
                if offset + byte >= data_len { break; }
                let c = data_bytes[offset + byte];
                let ch_byte = if c >= 0x20 && c < 0x7F { c } else { b'.' };
                let fg = if c >= 0x20 && c < 0x7F { 0xFFCCCCCC } else { 0xFF555555 };
                let asc_buf = [ch_byte];
                let asc_str = core::str::from_utf8(&asc_buf).unwrap_or(".");
                canvas_text(&mut win.canvas, cw, ch, acx, ry, asc_str, fg, CLIENT_BG);
                acx += FONT_WIDTH as i16;
            }
            canvas_text(&mut win.canvas, cw, ch, acx, ry, "|", 0xFF555577, CLIENT_BG);
        }

        if data_len == 0 {
            canvas_text(&mut win.canvas, cw, ch, px, py, "(empty)", 0xFF666666, CLIENT_BG);
        }
    }
}

fn oi_event(win: &mut Window, ev: &mut GuiEvent) {
    if ev.etype == EventType::Close { wm_destroy_window(win.id); return; }

    unsafe {
        if ev.etype == EventType::KeyDown {
            match ev.key {
                keyboard::KEY_DOWN | keyboard::KEY_PGDN => {
                    OI_HEX_SCROLL += if ev.key == keyboard::KEY_PGDN { 10 } else { 1 };
                    return;
                }
                keyboard::KEY_UP | keyboard::KEY_PGUP => {
                    OI_HEX_SCROLL -= if ev.key == keyboard::KEY_PGUP { 10 } else { 1 };
                    if OI_HEX_SCROLL < 0 { OI_HEX_SCROLL = 0; }
                    return;
                }
                _ => {}
            }
        }

        let action = OI_WIDGETS.dispatch(ev);
        match action {
            WidgetAction::Clicked(idx) if idx == OI_REFRESH_BTN || idx == OI_FILTER_BTN => {
                oi_refresh();
            }
            WidgetAction::Selected(idx, sel) if idx == OI_OBJ_LIST => {
                // Parse name from "[type] name" and load
                if let Some(ref lv) = OI_WIDGETS.widgets[OI_OBJ_LIST] {
                    let item_str = {
                        let buf = &lv.lv_items[sel as usize];
                        let len = buf.iter().position(|&b| b == 0).unwrap_or(LISTVIEW_ITEM_MAX);
                        core::str::from_utf8(&buf[..len]).unwrap_or("")
                    };
                    if let Some(bracket_end) = item_str.find(']') {
                        let name = &item_str[bracket_end + 2..]; // skip "] "
                        let mut sql = FmtBuf::new();
                        let _ = write!(sql, "SELECT * FROM ObjectTable WHERE name = '{}'", name);
                        let result = query_execute(sql.as_str(), 0);
                        if !result.rows.is_empty() {
                            let row = &result.rows[0];
                            OI_OBJ_ID = match row.fields[0] { Some(FieldValue::U64(v)) => v, _ => 0 };
                            let name_fv = match row.fields[1] { Some(FieldValue::Str(ref s)) => s.as_str(), _ => "" };
                            let type_fv = match row.fields[2] { Some(FieldValue::Str(ref s)) => s.as_str(), _ => "" };
                            let data_fv = match row.fields[3] { Some(FieldValue::Str(ref s)) => s.as_str(), _ => "" };
                            OI_OBJ_OWNER = match row.fields[4] { Some(FieldValue::U64(v)) => v, _ => 0 };
                            OI_OBJ_CREATED = match row.fields[6] { Some(FieldValue::U64(v)) => v, _ => 0 };

                            OI_OBJ_NAME = [0; 64];
                            let len = name_fv.len().min(63);
                            OI_OBJ_NAME[..len].copy_from_slice(&name_fv.as_bytes()[..len]);

                            OI_OBJ_TYPE = [0; 64];
                            let len = type_fv.len().min(63);
                            OI_OBJ_TYPE[..len].copy_from_slice(&type_fv.as_bytes()[..len]);

                            OI_OBJ_DATA = [0; 256];
                            let len = data_fv.len().min(255);
                            OI_OBJ_DATA[..len].copy_from_slice(&data_fv.as_bytes()[..len]);

                            OI_HEX_SCROLL = 0;
                            OI_HAS_SELECTION = true;
                        }
                    }
                }
            }
            _ => {}
        }
    }
}

fn open_object_inspector() {
    unsafe {
        OI_WIDGETS.clear();
        OI_HAS_SELECTION = false;
        OI_HEX_SCROLL = 0;
    }
    let id = match wm_create_window("Object Inspector", 80, 50, 620, 420, Some(oi_event), Some(oi_paint)) {
        Some(v) => v,
        None => return,
    };
    let ch;
    if let Some(win) = wm_get_window(id) { ch = win.client_h; } else { return; }

    unsafe {
        OI_WIDGETS.add_button(4, 2, 72, 22, "Refresh");
        OI_WIDGETS.add_textbox(80, 2, 180, 22);
        OI_WIDGETS.add_button(264, 2, 60, 22, "Filter");
        OI_WIDGETS.add_listview(4, 28, 152, ch as i16 - 34);
    }
    oi_refresh();
}

// ===========================================================================
// ---- Taskbar ----
// ===========================================================================
fn draw_taskbar() {
    let sw = gfx_width();
    let sh = gfx_height();
    let ty = sh as i16 - TASKBAR_HEIGHT as i16;

    // Background
    gfx_fill_rect(0, ty, sw, TASKBAR_HEIGHT, TASKBAR_BG);
    gfx_draw_hline(0, ty, sw, TASKBAR_BORDER);

    // VaultOS button
    gfx_fill_rect(2, ty + 2, 80, TASKBAR_HEIGHT - 4, 0xFF1A3366);
    gfx_draw_rect(2, ty + 2, 80, TASKBAR_HEIGHT - 4, 0xFF4466AA);
    gfx_draw_text(10, ty + 6, "VaultOS", 0xFFFFCC00, 0xFF1A3366);

    // Window buttons
    let z = wm_get_z_order();
    let mut bx: i16 = 90;
    for &id in z {
        if let Some(win) = wm_get_window(id) {
            let bg = if win.focused { 0xFF2A4466 } else { 0xFF1A1A3A };
            let border = if win.focused { 0xFF5588CC } else { 0xFF444466 };
            let text_color = if win.minimized { 0xFF666666 } else { 0xFFCCCCCC };
            gfx_fill_rect(bx, ty + 2, 120, TASKBAR_HEIGHT - 4, bg);
            gfx_draw_rect(bx, ty + 2, 120, TASKBAR_HEIGHT - 4, border);

            let title = win.title_str();
            let trunc = if title.len() > 14 { &title[..14] } else { title };
            gfx_draw_text(bx + 4, ty + 6, trunc, text_color, bg);
            bx += 124;
        }
    }

    // Right side: status
    let ms = pit::pit_get_uptime_ms();
    let mut status = FmtBuf::new();
    let _ = write!(status, "{}.{}s  {}KB", ms / 1000, (ms % 1000) / 100, heap::heap_used() / 1024);
    let sx = sw as i16 - (status.pos as i16) * FONT_WIDTH as i16 - 8;
    gfx_draw_text(sx, ty + 6, status.as_str(), 0xFF808080, TASKBAR_BG);

    // Security badges
    let rng_tag = if random::random_hw_available() { "RNG:HW" } else { "RNG:SW" };
    let rng_color = if random::random_hw_available() { 0xFF00CC66 } else { 0xFFCCCC00 };
    let badge_x = sx - (rng_tag.len() as i16) * FONT_WIDTH as i16 - 12;
    gfx_draw_text(badge_x, ty + 6, rng_tag, rng_color, TASKBAR_BG);

    let enc_x = badge_x - 4 * FONT_WIDTH as i16 - 8;
    gfx_draw_text(enc_x, ty + 6, "ENC", 0xFF00CC66, TASKBAR_BG);
}

// ===========================================================================
// ---- Menu ----
// ===========================================================================
fn menu_is_separator(idx: usize) -> bool {
    idx < MENU_ITEMS && MENU_LABELS[idx].starts_with("---")
}

fn draw_menu() {
    unsafe { if !MENU_OPEN { return; } }

    let sh = gfx_height();
    let mx: i16 = 2;
    let my = sh as i16 - TASKBAR_HEIGHT as i16 - (MENU_ITEMS as i16) * 24 - 4;
    let mw: u16 = 180;

    gfx_fill_rect(mx, my, mw, (MENU_ITEMS as u16) * 24 + 4, 0xFF1A1A2E);
    gfx_draw_rect(mx, my, mw, (MENU_ITEMS as u16) * 24 + 4, 0xFF555577);

    for i in 0..MENU_ITEMS {
        let iy = my + 2 + (i as i16) * 24;
        if menu_is_separator(i) {
            gfx_draw_hline(mx + 4, iy + 12, mw - 8, 0xFF444466);
        } else {
            gfx_draw_text(mx + 8, iy + 4, MENU_LABELS[i], 0xFF00DDAA, 0xFF1A1A2E);
        }
    }
}

fn menu_hit_test(mx: i16, my: i16) -> i32 {
    let sh = gfx_height();
    let menu_x: i16 = 2;
    let menu_y = sh as i16 - TASKBAR_HEIGHT as i16 - (MENU_ITEMS as i16) * 24 - 4;

    if mx < menu_x || mx >= menu_x + 180 { return -1; }
    if my < menu_y || my >= menu_y + (MENU_ITEMS as i16) * 24 + 4 { return -1; }

    let idx = ((my - menu_y - 2) / 24) as usize;
    if idx < MENU_ITEMS && !menu_is_separator(idx) { idx as i32 } else { -1 }
}

// ===========================================================================
// ---- Main GUI Loop ----
// ===========================================================================
pub fn gui_main() {
    serial_println!("[GUI] Starting desktop...");

    // Expand heap for back buffer + window canvases (~8 MiB)
    if heap::heap_expand(8 * 1024 * 1024) < 0 {
        serial_println!("[GUI] Failed to expand heap, aborting");
        return;
    }

    // Initialize subsystems
    gfx_init();
    mouse::mouse_init();
    mouse::mouse_set_bounds(gfx_width(), gfx_height());
    event_init();
    wm_init();
    comp_init();

    // Disable TUI content region (we own the framebuffer now)
    unsafe { crate::drivers::framebuffer::fb_set_content_region(0, 0); }

    serial_println!("[GUI] Desktop ready");

    unsafe {
        GUI_RUNNING = true;
        MENU_OPEN = false;
    }

    // Auto-open terminal window
    open_terminal();

    loop {
        unsafe { if !GUI_RUNNING { break; } }

        // Pump events from hardware
        event_pump();

        // Process events
        let mut ev = GuiEvent::empty();
        while event_poll(&mut ev) {
            // Check taskbar clicks
            if ev.etype == EventType::MouseDown {
                let sh = gfx_height();
                let ty = sh as i16 - TASKBAR_HEIGHT as i16;

                if ev.mouse_y >= ty {
                    // VaultOS button
                    if ev.mouse_x >= 2 && ev.mouse_x < 82 {
                        unsafe { MENU_OPEN = !MENU_OPEN; }
                        continue;
                    }

                    // Window buttons in taskbar
                    let z = wm_get_z_order();
                    let mut bx: i16 = 90;
                    for &id in z {
                        if ev.mouse_x >= bx && ev.mouse_x < bx + 120 {
                            if let Some(win) = wm_get_window_mut(id) {
                                if win.minimized {
                                    win.visible = true;
                                    win.minimized = false;
                                }
                            }
                            wm_bring_to_front(id);
                            break;
                        }
                        bx += 124;
                    }
                    continue;
                }

                // Menu click
                unsafe {
                    if MENU_OPEN {
                        let idx = menu_hit_test(ev.mouse_x, ev.mouse_y);
                        MENU_OPEN = false;
                        if idx >= 0 {
                            match idx {
                                0  => open_terminal(),
                                1  => open_query_console(),
                                2  => open_table_browser(),
                                3  => open_data_grid_stub(),
                                5  => open_vaultpad_stub(),
                                6  => open_calculator(),
                                7  => open_object_inspector(),
                                9  => open_security_dashboard(),
                                10 => open_audit_viewer(),
                                11 => open_cap_manager(),
                                12 => open_object_manager(),
                                14 => open_process_manager(),
                                15 => open_system_status(),
                                17 => { GUI_RUNNING = false; }
                                _ => {}
                            }
                            continue;
                        }
                    }
                }
            }

            // Close menu on any click outside
            if ev.etype == EventType::MouseDown {
                unsafe { if MENU_OPEN { MENU_OPEN = false; } }
            }

            // Forward to window manager
            wm_dispatch_event(&mut ev);
        }

        // Render
        comp_render();
        draw_taskbar();
        draw_menu();
        // Flip taskbar+menu area
        let sh = gfx_height();
        gfx_flip_rect(0, sh as i16 - TASKBAR_HEIGHT as i16 - (MENU_ITEMS as i16) * 24 - 10,
                       gfx_width(), TASKBAR_HEIGHT + (MENU_ITEMS as u16) * 24 + 10);

        // Idle
        unsafe { cpu::hlt(); }
    }

    // Return to TUI
    serial_println!("[GUI] Returning to TUI...");
    unsafe { crate::drivers::framebuffer::fb_clear(); }
}

// ===========================================================================
// ---- Terminal (VaultShell in a GUI window) ----
// ===========================================================================

const TERM_COLS: usize = 78;
const TERM_ROWS: usize = 38;
const TERM_BG: u32 = 0xFF0A0A1A;
const TERM_FG: u32 = 0xFF00DDAA;
const TERM_PROMPT_FG: u32 = 0xFFFFCC00;

#[derive(Copy, Clone)]
struct TermCell {
    ch: u8,
    fg: u32,
}

const BLANK_CELL: TermCell = TermCell { ch: b' ', fg: 0xFF00DDAA };

static mut TERM_BUF: [[TermCell; TERM_COLS]; TERM_ROWS] = [[BLANK_CELL; TERM_COLS]; TERM_ROWS];
static mut TERM_CX: usize = 0;
static mut TERM_CY: usize = 0;
static mut TERM_CMD: [u8; 512] = [0; 512];
static mut TERM_CMD_LEN: usize = 0;
static mut TERM_WIN_ID: u32 = 0;

fn term_scroll_up() {
    unsafe {
        for r in 1..TERM_ROWS {
            TERM_BUF[r - 1] = TERM_BUF[r];
        }
        TERM_BUF[TERM_ROWS - 1] = [BLANK_CELL; TERM_COLS];
    }
}

fn term_putchar(c: u8) {
    unsafe {
        match c {
            b'\n' => {
                TERM_CX = 0;
                TERM_CY += 1;
                if TERM_CY >= TERM_ROWS {
                    term_scroll_up();
                    TERM_CY = TERM_ROWS - 1;
                }
            }
            b'\r' => {
                TERM_CX = 0;
            }
            0x08 => {
                // Backspace
                if TERM_CX > 0 && TERM_CY < TERM_ROWS {
                    TERM_CX -= 1;
                    TERM_BUF[TERM_CY][TERM_CX] = BLANK_CELL;
                }
            }
            b'\t' => {
                let spaces = 4 - (TERM_CX % 4);
                for _ in 0..spaces {
                    term_putchar(b' ');
                }
            }
            _ => {
                if c >= 0x20 && c < 0x7F {
                    // Defensive bounds clamp before writing to TERM_BUF
                    if TERM_CY >= TERM_ROWS { term_scroll_up(); TERM_CY = TERM_ROWS - 1; }
                    if TERM_CX >= TERM_COLS {
                        TERM_CX = 0;
                        TERM_CY += 1;
                        if TERM_CY >= TERM_ROWS { term_scroll_up(); TERM_CY = TERM_ROWS - 1; }
                    }
                    TERM_BUF[TERM_CY][TERM_CX] = TermCell { ch: c, fg: TERM_FG };
                    TERM_CX += 1;
                    if TERM_CX >= TERM_COLS {
                        TERM_CX = 0;
                        TERM_CY += 1;
                        if TERM_CY >= TERM_ROWS {
                            term_scroll_up();
                            TERM_CY = TERM_ROWS - 1;
                        }
                    }
                }
            }
        }
    }
}

fn term_print(s: &str) {
    for &b in s.as_bytes() {
        term_putchar(b);
    }
}

fn term_print_prompt() {
    unsafe {
        let prompt = "vault> ";
        for &b in prompt.as_bytes() {
            if TERM_CY >= TERM_ROWS { term_scroll_up(); TERM_CY = TERM_ROWS - 1; }
            if TERM_CX >= TERM_COLS {
                TERM_CX = 0;
                TERM_CY += 1;
                if TERM_CY >= TERM_ROWS { term_scroll_up(); TERM_CY = TERM_ROWS - 1; }
            }
            TERM_BUF[TERM_CY][TERM_CX] = TermCell { ch: b, fg: TERM_PROMPT_FG };
            TERM_CX += 1;
        }
    }
}

fn term_clear() {
    unsafe {
        TERM_BUF = [[BLANK_CELL; TERM_COLS]; TERM_ROWS];
        TERM_CX = 0;
        TERM_CY = 0;
    }
}

/// Output redirector for shell: write to terminal cell buffer + serial mirror
fn gui_term_putch(c: u8) {
    unsafe { serial::serial_putchar(c); }
    term_putchar(c);
}

fn gui_term_print(s: &str) {
    unsafe { serial::serial_print(s); }
    term_print(s);
}

fn term_paint(win: &mut Window) {
    let cw = win.client_w;
    let ch = win.client_h;

    // Clear canvas directly via mutable access
    for pixel in win.canvas.iter_mut() {
        *pixel = TERM_BG;
    }

    let mx: i16 = 4;
    let my: i16 = 4;

    unsafe {
        // Draw cells directly into canvas using safe indexing
        for r in 0..TERM_ROWS {
            let py = my + (r as i16) * FONT_HEIGHT as i16;
            if py + FONT_HEIGHT as i16 > ch as i16 { break; }
            for c in 0..TERM_COLS {
                let px = mx + (c as i16) * FONT_WIDTH as i16;
                if px + FONT_WIDTH as i16 > cw as i16 { break; }
                let cell = &TERM_BUF[r][c];
                if cell.ch != b' ' {
                    // Draw character glyph directly into canvas
                    let glyph = &crate::drivers::font::FONT_8X16[cell.ch as usize];
                    for gy in 0..FONT_HEIGHT as i16 {
                        let bits = glyph[gy as usize];
                        for gx in 0..FONT_WIDTH as i16 {
                            let ppx = px + gx;
                            let ppy = py + gy;
                            if ppx >= 0 && (ppx as u16) < cw && ppy >= 0 && (ppy as u16) < ch {
                                let idx = ppy as usize * cw as usize + ppx as usize;
                                if idx < win.canvas.len() {
                                    if bits & (0x80 >> gx) != 0 {
                                        win.canvas[idx] = cell.fg;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Blinking block cursor
        let cx_px = mx + (TERM_CX as i16) * FONT_WIDTH as i16;
        let cy_px = my + (TERM_CY as i16) * FONT_HEIGHT as i16;
        if cx_px + FONT_WIDTH as i16 <= cw as i16 && cy_px + FONT_HEIGHT as i16 <= ch as i16 {
            let uptime = pit::pit_get_uptime_ms();
            if (uptime / 500) % 2 == 0 {
                for gy in 0..FONT_HEIGHT as i16 {
                    for gx in 0..FONT_WIDTH as i16 {
                        let idx = (cy_px + gy) as usize * cw as usize + (cx_px + gx) as usize;
                        if idx < win.canvas.len() {
                            win.canvas[idx] = TERM_FG;
                        }
                    }
                }
            }
        }
    }
}

fn term_event(win: &mut Window, ev: &mut GuiEvent) {
    if ev.etype == EventType::Close {
        wm_destroy_window(win.id);
        unsafe { TERM_WIN_ID = 0; }
        return;
    }

    if ev.etype != EventType::KeyDown { return; }

    let key = ev.key;
    unsafe {
        match key {
            // Enter — execute command
            b'\n' | b'\r' => {
                term_putchar(b'\n');
                if TERM_CMD_LEN > 0 {
                    let cmd = core::str::from_utf8(&TERM_CMD[..TERM_CMD_LEN]).unwrap_or("");
                    // Redirect shell output to terminal cell buffer
                    shell_main::set_output(gui_term_putch, gui_term_print);
                    let is_clear = shell_main::execute_command(cmd);
                    shell_main::restore_output();
                    if is_clear {
                        term_clear();
                    }
                }
                TERM_CMD_LEN = 0;
                term_print_prompt();
            }
            // Backspace
            0x08 | 0x7F => {
                if TERM_CMD_LEN > 0 {
                    TERM_CMD_LEN -= 1;
                    term_putchar(0x08);  // moves cursor back and blanks
                }
            }
            // Printable ASCII
            0x20..=0x7E => {
                if TERM_CMD_LEN < 510 {
                    TERM_CMD[TERM_CMD_LEN] = key;
                    TERM_CMD_LEN += 1;
                    term_putchar(key);
                }
            }
            _ => {}
        }
    }
}

fn open_terminal() {
    unsafe {
        // If terminal already exists, just bring it to front
        if TERM_WIN_ID != 0 {
            if wm_get_window(TERM_WIN_ID).is_some() {
                wm_bring_to_front(TERM_WIN_ID);
                return;
            }
            TERM_WIN_ID = 0;
        }

        // Clear buffer
        term_clear();
        TERM_CMD_LEN = 0;
    }

    let id = wm_create_window("Terminal", 30, 20, 660, 660,
        Some(term_event), Some(term_paint));
    if let Some(id) = id {
        unsafe { TERM_WIN_ID = id; }

        // Print banner
        term_print("  VaultOS Terminal v0.1.0-rs\n");
        term_print("  Type 'help' for commands\n");
        term_print("\n");
        term_print_prompt();
    }
}

// Stub openers for apps that need more complex custom rendering
// (Data Grid and VaultPad are too complex for the initial port — will be added later)
fn open_data_grid_stub() {
    let id = wm_create_window("Data Grid", 40, 30, 500, 300,
        Some(stub_event), Some(stub_paint));
    let _ = id;
}

fn open_vaultpad_stub() {
    let id = wm_create_window("VaultPad Editor", 60, 40, 500, 300,
        Some(stub_event), Some(stub_paint));
    let _ = id;
}

fn stub_paint(win: &mut Window) {
    wm_clear_canvas(win, CLIENT_BG);
    let cw = win.client_w;
    let ch = win.client_h;
    canvas_text(&mut win.canvas, cw, ch, 20, 20, "Coming soon...", 0xFF808080, CLIENT_BG);
}

fn stub_event(win: &mut Window, ev: &mut GuiEvent) {
    if ev.etype == EventType::Close { wm_destroy_window(win.id); }
}
