#include "btree.h"
#include "../mm/heap.h"
#include "../lib/string.h"
#include "../lib/assert.h"

static btree_node_t *btree_create_node(bool is_leaf) {
    btree_node_t *node = kzalloc(sizeof(btree_node_t));
    ASSERT(node != NULL);
    node->is_leaf = is_leaf;
    node->num_keys = 0;
    return node;
}

void btree_init(btree_t *tree, uint32_t table_id) {
    tree->root = btree_create_node(true);
    tree->count = 0;
    tree->table_id = table_id;
}

static void *btree_search_node(btree_node_t *node, uint64_t key) {
    if (!node) return NULL;

    uint32_t i = 0;
    while (i < node->num_keys && key > node->keys[i]) i++;

    if (i < node->num_keys && key == node->keys[i]) {
        return node->values[i];
    }

    if (node->is_leaf) return NULL;
    return btree_search_node(node->children[i], key);
}

void *btree_search(btree_t *tree, uint64_t key) {
    return btree_search_node(tree->root, key);
}

static void btree_split_child(btree_node_t *parent, uint32_t index) {
    btree_node_t *child = parent->children[index];
    uint32_t mid = BTREE_MAX_KEYS / 2;

    btree_node_t *new_node = btree_create_node(child->is_leaf);
    new_node->num_keys = child->num_keys - mid - 1;

    /* Copy upper half of keys/values to new node */
    for (uint32_t j = 0; j < new_node->num_keys; j++) {
        new_node->keys[j] = child->keys[mid + 1 + j];
        new_node->values[j] = child->values[mid + 1 + j];
    }

    if (!child->is_leaf) {
        for (uint32_t j = 0; j <= new_node->num_keys; j++) {
            new_node->children[j] = child->children[mid + 1 + j];
        }
    }

    /* Shift parent's keys/children right */
    for (int j = (int)parent->num_keys; j > (int)index; j--) {
        parent->keys[j] = parent->keys[j - 1];
        parent->values[j] = parent->values[j - 1];
        parent->children[j + 1] = parent->children[j];
    }

    /* Insert median key into parent */
    parent->keys[index] = child->keys[mid];
    parent->values[index] = child->values[mid];
    parent->children[index + 1] = new_node;
    parent->num_keys++;

    child->num_keys = mid;
}

static void btree_insert_nonfull(btree_node_t *node, uint64_t key, void *value) {
    int i = (int)node->num_keys - 1;

    if (node->is_leaf) {
        /* Shift keys right to make room */
        while (i >= 0 && key < node->keys[i]) {
            node->keys[i + 1] = node->keys[i];
            node->values[i + 1] = node->values[i];
            i--;
        }
        /* Check for duplicate key (update in place) */
        if (i >= 0 && node->keys[i] == key) {
            node->values[i] = value;
            return;
        }
        node->keys[i + 1] = key;
        node->values[i + 1] = value;
        node->num_keys++;
    } else {
        while (i >= 0 && key < node->keys[i]) i--;
        if (i >= 0 && node->keys[i] == key) {
            node->values[i] = value;
            return;
        }
        i++;
        if (node->children[i]->num_keys == BTREE_MAX_KEYS) {
            btree_split_child(node, (uint32_t)i);
            if (key > node->keys[i]) i++;
            if (key == node->keys[i]) {
                node->values[i] = value;
                return;
            }
        }
        btree_insert_nonfull(node->children[i], key, value);
    }
}

int btree_insert(btree_t *tree, uint64_t key, void *value) {
    btree_node_t *root = tree->root;

    if (root->num_keys == BTREE_MAX_KEYS) {
        btree_node_t *new_root = btree_create_node(false);
        new_root->children[0] = root;
        btree_split_child(new_root, 0);
        tree->root = new_root;

        int i = (key > new_root->keys[0]) ? 1 : 0;
        if (key == new_root->keys[0]) {
            new_root->values[0] = value;
        } else {
            btree_insert_nonfull(new_root->children[i], key, value);
        }
    } else {
        btree_insert_nonfull(root, key, value);
    }

    tree->count++;
    return 0;
}

/* Simple delete: mark as NULL (lazy deletion for MVP) */
int btree_delete(btree_t *tree, uint64_t key) {
    /* Find and remove from leaf (simplified) */
    btree_node_t *node = tree->root;
    while (node) {
        uint32_t i = 0;
        while (i < node->num_keys && key > node->keys[i]) i++;

        if (i < node->num_keys && key == node->keys[i]) {
            if (node->is_leaf) {
                /* Shift keys left */
                for (uint32_t j = i; j < node->num_keys - 1; j++) {
                    node->keys[j] = node->keys[j + 1];
                    node->values[j] = node->values[j + 1];
                }
                node->num_keys--;
                tree->count--;
                return 0;
            }
            /* For internal nodes in MVP: mark value as NULL */
            node->values[i] = NULL;
            tree->count--;
            return 0;
        }

        if (node->is_leaf) return -1; /* Not found */
        node = node->children[i];
    }
    return -1;
}

static void btree_scan_node(btree_node_t *node, btree_iter_fn callback, void *ctx) {
    if (!node) return;

    for (uint32_t i = 0; i < node->num_keys; i++) {
        if (!node->is_leaf) {
            btree_scan_node(node->children[i], callback, ctx);
        }
        if (node->values[i]) {
            callback(node->keys[i], node->values[i], ctx);
        }
    }
    if (!node->is_leaf) {
        btree_scan_node(node->children[node->num_keys], callback, ctx);
    }
}

void btree_scan(btree_t *tree, btree_iter_fn callback, void *ctx) {
    btree_scan_node(tree->root, callback, ctx);
}

static void btree_destroy_node(btree_node_t *node) {
    if (!node) return;
    if (!node->is_leaf) {
        for (uint32_t i = 0; i <= node->num_keys; i++) {
            btree_destroy_node(node->children[i]);
        }
    }
    kfree(node);
}

void btree_destroy(btree_t *tree) {
    btree_destroy_node(tree->root);
    tree->root = NULL;
    tree->count = 0;
}
