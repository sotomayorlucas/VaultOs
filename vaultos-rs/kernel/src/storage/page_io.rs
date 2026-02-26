// Page I/O for VaultOS CoW B-tree Persistence
//
// Serializes/deserializes B-tree nodes and encrypted records
// to/from 4 KiB disk blocks.

use crate::db::btree::{BtreeNode, BTREE_MAX_KEYS, BTREE_ORDER};
use crate::db::record::EncryptedRecord;
use crate::storage::disk_alloc::*;
use alloc::boxed::Box;
use alloc::vec;
use alloc::vec::Vec;
use vaultos_shared::error_codes::*;

pub const PAGE_NODE_MAGIC: u32 = 0x4E4F4445;   // "NODE"
pub const PAGE_RECORD_MAGIC: u32 = 0x52454344;  // "RECD"

pub const RECORD_PAGE_PAYLOAD: usize = 4080;  // 4096 - 16 byte header
/// Header in payload: row_id(8) + table_id(4) + ciphertext_len(4) + iv(16) + mac(32) = 64
pub const RECORD_PAYLOAD_HDR: usize = 64;

/// On-disk node page layout (4096 bytes)
#[repr(C)]
struct NodePage {
    magic: u32,
    num_keys: u32,
    is_leaf: u8,
    table_id: u8,
    pad: [u8; 2],
    checksum: u32,
    keys: [u64; BTREE_MAX_KEYS],       // 63 * 8 = 504
    value_lbas: [u64; BTREE_MAX_KEYS],  // 63 * 8 = 504
    child_lbas: [u64; BTREE_ORDER],     // 64 * 8 = 512
    reserved: [u8; 4096 - 1536],
}

const _: () = assert!(core::mem::size_of::<NodePage>() == 4096);

/// On-disk record page layout (4096 bytes, chainable)
#[repr(C)]
struct RecordPage {
    magic: u32,
    payload_len: u32,
    next_block: u64,
    payload: [u8; RECORD_PAGE_PAYLOAD],
}

const _: () = assert!(core::mem::size_of::<RecordPage>() == 4096);

/// Simple additive checksum over node page (excluding checksum field at offset 12..16).
fn compute_node_checksum(pg: &NodePage) -> u32 {
    let data = unsafe {
        core::slice::from_raw_parts(pg as *const NodePage as *const u8, 4096)
    };
    let mut sum: u32 = 0;
    for i in 0..4096 {
        if i >= 12 && i < 16 { continue; }
        sum = sum.wrapping_add(data[i] as u32);
    }
    sum
}

/// Write a B-tree node to a new disk block. Returns block index, 0 on failure.
pub fn page_write_node(node: &mut BtreeNode, table_id: u32) -> u64 {
    let block = disk_alloc_block();
    if block == 0 {
        crate::serial_println!("[PAGE] ERROR: Failed to allocate block for node");
        return 0;
    }

    let mut pg = unsafe { core::mem::zeroed::<NodePage>() };
    pg.magic = PAGE_NODE_MAGIC;
    pg.num_keys = node.num_keys;
    pg.is_leaf = if node.is_leaf { 1 } else { 0 };
    pg.table_id = table_id as u8;

    for i in 0..node.num_keys as usize {
        pg.keys[i] = node.keys[i];
        pg.value_lbas[i] = node.value_lbas[i];
    }

    if !node.is_leaf {
        for i in 0..=(node.num_keys as usize) {
            pg.child_lbas[i] = node.child_lbas[i];
        }
    }

    pg.checksum = compute_node_checksum(&pg);

    let buf = unsafe {
        core::slice::from_raw_parts(&pg as *const NodePage as *const u8, 4096)
    };
    let ret = disk_write_block(block, buf);
    if ret != VOS_OK {
        crate::serial_println!("[PAGE] ERROR: Failed to write node block");
        disk_free_block(block);
        return 0;
    }

    block
}

/// Read a B-tree node from disk. Returns VOS_OK on success.
pub fn page_read_node(block: u64, node: &mut BtreeNode, table_id_out: &mut u8) -> i32 {
    let mut pg = unsafe { core::mem::zeroed::<NodePage>() };
    let buf = unsafe {
        core::slice::from_raw_parts_mut(&mut pg as *mut NodePage as *mut u8, 4096)
    };
    let ret = disk_read_block(block, buf);
    if ret != VOS_OK {
        crate::serial_println!("[PAGE] ERROR: Failed to read node block");
        return VOS_ERR_IO;
    }

    if pg.magic != PAGE_NODE_MAGIC {
        crate::serial_println!("[PAGE] ERROR: Bad node magic");
        return VOS_ERR_INVAL;
    }

    let expected = compute_node_checksum(&pg);
    if pg.checksum != expected {
        crate::serial_println!("[PAGE] ERROR: Node checksum mismatch");
        return VOS_ERR_INVAL;
    }

    node.num_keys = pg.num_keys;
    node.is_leaf = pg.is_leaf != 0;
    node.disk_lba = block;
    node.dirty = false;

    for i in 0..pg.num_keys as usize {
        node.keys[i] = pg.keys[i];
        node.value_lbas[i] = pg.value_lbas[i];
    }

    if !node.is_leaf {
        for i in 0..=(pg.num_keys as usize) {
            node.child_lbas[i] = pg.child_lbas[i];
        }
    }

    *table_id_out = pg.table_id;
    VOS_OK
}

/// Write an encrypted record to new disk block(s). Returns first block index, 0 on failure.
pub fn page_write_record(enc: &EncryptedRecord) -> u64 {
    // Build payload: [row_id:8][table_id:4][ciphertext_len:4][iv:16][mac:32][ciphertext:N]
    let total_payload = match RECORD_PAYLOAD_HDR.checked_add(enc.ciphertext_len as usize) {
        Some(len) => len,
        None => return 0, // Overflow
    };
    let mut payload_buf = vec![0u8; total_payload];

    // Header
    payload_buf[0..8].copy_from_slice(&enc.row_id.to_le_bytes());
    payload_buf[8..12].copy_from_slice(&enc.table_id.to_le_bytes());
    payload_buf[12..16].copy_from_slice(&enc.ciphertext_len.to_le_bytes());
    payload_buf[16..32].copy_from_slice(&enc.iv);
    payload_buf[32..64].copy_from_slice(&enc.mac);
    // Ciphertext
    payload_buf[64..64 + enc.ciphertext_len as usize]
        .copy_from_slice(&enc.ciphertext[..enc.ciphertext_len as usize]);

    let mut remaining = total_payload;
    let mut offset = 0usize;
    let mut first_block: u64 = 0;
    let mut prev_block: u64 = 0;
    let mut prev_pg: Option<RecordPage> = None;

    while remaining > 0 {
        let block = disk_alloc_block();
        if block == 0 {
            crate::serial_println!("[PAGE] ERROR: Failed to allocate block for record");
            if first_block != 0 {
                page_free_record_blocks(first_block);
            }
            return 0;
        }

        let mut pg = unsafe { core::mem::zeroed::<RecordPage>() };
        pg.magic = PAGE_RECORD_MAGIC;

        let chunk = remaining.min(RECORD_PAGE_PAYLOAD);
        pg.payload_len = chunk as u32;
        pg.next_block = 0;
        pg.payload[..chunk].copy_from_slice(&payload_buf[offset..offset + chunk]);

        // Link previous page to this one
        if let Some(ref mut prev) = prev_pg {
            prev.next_block = block;
            let buf = unsafe {
                core::slice::from_raw_parts(prev as *const RecordPage as *const u8, 4096)
            };
            let ret = disk_write_block(prev_block, buf);
            if ret != VOS_OK {
                crate::serial_println!("[PAGE] ERROR: Failed to write chained record block");
                disk_free_block(block);
                return 0;
            }
        }

        if first_block == 0 { first_block = block; }

        prev_pg = Some(pg);
        prev_block = block;
        offset += chunk;
        remaining -= chunk;
    }

    // Write last page
    if let Some(ref prev) = prev_pg {
        let buf = unsafe {
            core::slice::from_raw_parts(prev as *const RecordPage as *const u8, 4096)
        };
        let ret = disk_write_block(prev_block, buf);
        if ret != VOS_OK {
            crate::serial_println!("[PAGE] ERROR: Failed to write final record block");
            return 0;
        }
    }

    first_block
}

/// Read an encrypted record from disk. Returns VOS_OK on success.
pub fn page_read_record(block: u64, enc: &mut EncryptedRecord) -> i32 {
    // First pass: count total payload length
    let mut total_len: usize = 0;
    let mut cur = block;

    while cur != 0 {
        let mut pg = unsafe { core::mem::zeroed::<RecordPage>() };
        let buf = unsafe {
            core::slice::from_raw_parts_mut(&mut pg as *mut RecordPage as *mut u8, 4096)
        };
        let ret = disk_read_block(cur, buf);
        if ret != VOS_OK { return VOS_ERR_IO; }
        if pg.magic != PAGE_RECORD_MAGIC { return VOS_ERR_INVAL; }
        total_len += pg.payload_len as usize;
        cur = pg.next_block;
    }

    if total_len < RECORD_PAYLOAD_HDR { return VOS_ERR_INVAL; }

    // Assemble full payload
    let mut payload_buf = vec![0u8; total_len];
    let mut offset = 0usize;
    cur = block;
    while cur != 0 {
        let mut pg = unsafe { core::mem::zeroed::<RecordPage>() };
        let buf = unsafe {
            core::slice::from_raw_parts_mut(&mut pg as *mut RecordPage as *mut u8, 4096)
        };
        let ret = disk_read_block(cur, buf);
        if ret != VOS_OK { return VOS_ERR_IO; }
        let len = pg.payload_len as usize;
        payload_buf[offset..offset + len].copy_from_slice(&pg.payload[..len]);
        offset += len;
        cur = pg.next_block;
    }

    // Parse header
    enc.row_id = u64::from_le_bytes(payload_buf[0..8].try_into().unwrap());
    enc.table_id = u32::from_le_bytes(payload_buf[8..12].try_into().unwrap());
    enc.ciphertext_len = u32::from_le_bytes(payload_buf[12..16].try_into().unwrap());
    enc.iv.copy_from_slice(&payload_buf[16..32]);
    enc.mac.copy_from_slice(&payload_buf[32..64]);

    let ct_end = match RECORD_PAYLOAD_HDR.checked_add(enc.ciphertext_len as usize) {
        Some(end) => end,
        None => return VOS_ERR_INVAL, // Overflow on corrupt data
    };
    if ct_end > total_len {
        return VOS_ERR_INVAL;
    }

    // Copy ciphertext
    let ct_len = enc.ciphertext_len as usize;
    enc.ciphertext = Vec::with_capacity(ct_len);
    enc.ciphertext.extend_from_slice(&payload_buf[64..64 + ct_len]);

    VOS_OK
}

/// Free all blocks in a record chain.
pub fn page_free_record_blocks(block: u64) {
    let mut cur = block;
    while cur != 0 {
        let mut pg = unsafe { core::mem::zeroed::<RecordPage>() };
        let buf = unsafe {
            core::slice::from_raw_parts_mut(&mut pg as *mut RecordPage as *mut u8, 4096)
        };
        let ret = disk_read_block(cur, buf);
        let next = if ret == VOS_OK && pg.magic == PAGE_RECORD_MAGIC {
            pg.next_block
        } else {
            0
        };
        disk_free_block(cur);
        cur = next;
    }
}
