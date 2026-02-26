// Order-64 B-tree for VaultOS-RS
// Direct port of kernel/db/btree.c

use alloc::boxed::Box;

pub const BTREE_ORDER: usize = 64;
pub const BTREE_MAX_KEYS: usize = BTREE_ORDER - 1;
pub const BTREE_MIN_KEYS: usize = BTREE_ORDER / 2 - 1;

pub struct BtreeNode {
    pub keys: [u64; BTREE_MAX_KEYS],
    pub values: [*mut u8; BTREE_MAX_KEYS],   // void* equivalent
    pub children: [*mut BtreeNode; BTREE_ORDER],
    pub num_keys: u32,
    pub is_leaf: bool,
    // Persistence
    pub disk_lba: u64,
    pub value_lbas: [u64; BTREE_MAX_KEYS],
    pub child_lbas: [u64; BTREE_ORDER],
    pub dirty: bool,
}

unsafe impl Send for BtreeNode {}
unsafe impl Sync for BtreeNode {}

impl BtreeNode {
    pub fn new(is_leaf: bool) -> *mut BtreeNode {
        let node = Box::new(BtreeNode {
            keys: [0u64; BTREE_MAX_KEYS],
            values: [core::ptr::null_mut(); BTREE_MAX_KEYS],
            children: [core::ptr::null_mut(); BTREE_ORDER],
            num_keys: 0,
            is_leaf,
            disk_lba: 0,
            value_lbas: [0u64; BTREE_MAX_KEYS],
            child_lbas: [0u64; BTREE_ORDER],
            dirty: true,
        });
        Box::into_raw(node)
    }
}

pub struct Btree {
    pub root: *mut BtreeNode,
    pub count: u64,
    pub table_id: u32,
}

unsafe impl Send for Btree {}
unsafe impl Sync for Btree {}

pub type BtreeIterFn = fn(u64, *mut u8, *mut u8);

pub fn btree_init(tree: &mut Btree, table_id: u32) {
    tree.root = BtreeNode::new(true);
    tree.count = 0;
    tree.table_id = table_id;
}

pub fn btree_search(tree: &Btree, key: u64) -> *mut u8 {
    unsafe { btree_search_node(tree.root, key) }
}

unsafe fn btree_search_node(node: *mut BtreeNode, key: u64) -> *mut u8 {
    if node.is_null() { return core::ptr::null_mut(); }

    let n = &*node;
    let mut i = 0u32;
    while i < n.num_keys && key > n.keys[i as usize] { i += 1; }

    if i < n.num_keys && key == n.keys[i as usize] {
        return n.values[i as usize];
    }

    if n.is_leaf { return core::ptr::null_mut(); }
    btree_search_node(n.children[i as usize], key)
}

unsafe fn btree_split_child(parent: *mut BtreeNode, index: u32) {
    let p = &mut *parent;
    // Guard: parent must not be full — caller invariant.  Prevents children[64] OOB.
    if p.num_keys >= BTREE_MAX_KEYS as u32 {
        return;
    }
    let child = &mut *p.children[index as usize];
    let mid = (BTREE_MAX_KEYS / 2) as u32;

    let new_node = BtreeNode::new(child.is_leaf);
    let nn = &mut *new_node;
    nn.num_keys = child.num_keys - mid - 1;

    // Copy upper half to new node
    for j in 0..nn.num_keys {
        nn.keys[j as usize] = child.keys[(mid + 1 + j) as usize];
        nn.values[j as usize] = child.values[(mid + 1 + j) as usize];
        nn.value_lbas[j as usize] = child.value_lbas[(mid + 1 + j) as usize];
    }

    if !child.is_leaf {
        for j in 0..=nn.num_keys {
            nn.children[j as usize] = child.children[(mid + 1 + j) as usize];
            nn.child_lbas[j as usize] = child.child_lbas[(mid + 1 + j) as usize];
        }
    }

    // Shift parent's keys/children right
    let mut j = p.num_keys as i32;
    while j > index as i32 {
        p.keys[j as usize] = p.keys[(j - 1) as usize];
        p.values[j as usize] = p.values[(j - 1) as usize];
        p.value_lbas[j as usize] = p.value_lbas[(j - 1) as usize];
        p.children[(j + 1) as usize] = p.children[j as usize];
        p.child_lbas[(j + 1) as usize] = p.child_lbas[j as usize];
        j -= 1;
    }

    // Insert median into parent
    p.keys[index as usize] = child.keys[mid as usize];
    p.values[index as usize] = child.values[mid as usize];
    p.value_lbas[index as usize] = child.value_lbas[mid as usize];
    p.children[(index + 1) as usize] = new_node;
    p.child_lbas[(index + 1) as usize] = 0;
    p.num_keys += 1;

    child.num_keys = mid;

    p.dirty = true;
    child.dirty = true;
    nn.dirty = true;
}

unsafe fn btree_insert_nonfull(node: *mut BtreeNode, key: u64, value: *mut u8) {
    let n = &mut *node;
    let mut i = n.num_keys as i32 - 1;

    if n.is_leaf {
        while i >= 0 && key < n.keys[i as usize] {
            n.keys[(i + 1) as usize] = n.keys[i as usize];
            n.values[(i + 1) as usize] = n.values[i as usize];
            n.value_lbas[(i + 1) as usize] = n.value_lbas[i as usize];
            i -= 1;
        }
        // Check for duplicate key (update in place)
        if i >= 0 && n.keys[i as usize] == key {
            n.values[i as usize] = value;
            n.value_lbas[i as usize] = 0;
            n.dirty = true;
            return;
        }
        n.keys[(i + 1) as usize] = key;
        n.values[(i + 1) as usize] = value;
        n.value_lbas[(i + 1) as usize] = 0;
        n.num_keys += 1;
        n.dirty = true;
    } else {
        while i >= 0 && key < n.keys[i as usize] { i -= 1; }
        if i >= 0 && n.keys[i as usize] == key {
            n.values[i as usize] = value;
            n.value_lbas[i as usize] = 0;
            n.dirty = true;
            return;
        }
        i += 1;
        if (*n.children[i as usize]).num_keys == BTREE_MAX_KEYS as u32 {
            btree_split_child(node, i as u32);
            if key > n.keys[i as usize] { i += 1; }
            if key == n.keys[i as usize] {
                n.values[i as usize] = value;
                n.value_lbas[i as usize] = 0;
                n.dirty = true;
                return;
            }
        }
        btree_insert_nonfull(n.children[i as usize], key, value);
    }
}

pub fn btree_insert(tree: &mut Btree, key: u64, value: *mut u8) -> i32 {
    unsafe {
        let root = tree.root;
        if (*root).num_keys == BTREE_MAX_KEYS as u32 {
            let new_root = BtreeNode::new(false);
            (*new_root).children[0] = root;
            (*new_root).child_lbas[0] = (*root).disk_lba;
            btree_split_child(new_root, 0);
            tree.root = new_root;

            let i = if key > (*new_root).keys[0] { 1 } else { 0 };
            if key == (*new_root).keys[0] {
                (*new_root).values[0] = value;
            } else {
                btree_insert_nonfull((*new_root).children[i], key, value);
            }
        } else {
            btree_insert_nonfull(root, key, value);
        }
    }
    tree.count += 1;
    0
}

pub fn btree_delete(tree: &mut Btree, key: u64) -> i32 {
    unsafe {
        let mut node = tree.root;
        while !node.is_null() {
            let n = &mut *node;
            let mut i = 0u32;
            while i < n.num_keys && key > n.keys[i as usize] { i += 1; }

            if i < n.num_keys && key == n.keys[i as usize] {
                if n.is_leaf {
                    for j in i..(n.num_keys - 1) {
                        n.keys[j as usize] = n.keys[(j + 1) as usize];
                        n.values[j as usize] = n.values[(j + 1) as usize];
                        n.value_lbas[j as usize] = n.value_lbas[(j + 1) as usize];
                    }
                    n.num_keys -= 1;
                    n.dirty = true;
                    tree.count -= 1;
                    return 0;
                }
                // Internal node: mark value NULL (lazy)
                n.values[i as usize] = core::ptr::null_mut();
                n.value_lbas[i as usize] = 0;
                n.dirty = true;
                tree.count -= 1;
                return 0;
            }

            if n.is_leaf { return -1; }
            node = n.children[i as usize];
        }
    }
    -1
}

unsafe fn btree_scan_node(node: *mut BtreeNode, callback: BtreeIterFn, ctx: *mut u8) {
    if node.is_null() { return; }
    let n = &*node;

    for i in 0..n.num_keys as usize {
        if !n.is_leaf {
            btree_scan_node(n.children[i], callback, ctx);
        }
        if !n.values[i].is_null() {
            callback(n.keys[i], n.values[i], ctx);
        }
    }
    if !n.is_leaf {
        btree_scan_node(n.children[n.num_keys as usize], callback, ctx);
    }
}

pub fn btree_scan(tree: &Btree, callback: BtreeIterFn, ctx: *mut u8) {
    unsafe { btree_scan_node(tree.root, callback, ctx); }
}

unsafe fn btree_destroy_node(node: *mut BtreeNode) {
    if node.is_null() { return; }
    let n = &*node;
    if !n.is_leaf {
        for i in 0..=n.num_keys as usize {
            btree_destroy_node(n.children[i]);
        }
    }
    let _ = Box::from_raw(node);
}

pub fn btree_destroy(tree: &mut Btree) {
    unsafe { btree_destroy_node(tree.root); }
    tree.root = core::ptr::null_mut();
    tree.count = 0;
}
