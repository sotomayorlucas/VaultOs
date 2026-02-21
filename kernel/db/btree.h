#ifndef VAULTOS_BTREE_H
#define VAULTOS_BTREE_H

#include "../lib/types.h"

#define BTREE_ORDER     64
#define BTREE_MAX_KEYS  (BTREE_ORDER - 1)
#define BTREE_MIN_KEYS  (BTREE_ORDER / 2 - 1)

typedef struct btree_node {
    uint64_t            keys[BTREE_MAX_KEYS];
    void               *values[BTREE_MAX_KEYS];
    struct btree_node  *children[BTREE_ORDER];
    uint32_t            num_keys;
    bool                is_leaf;
} btree_node_t;

typedef struct {
    btree_node_t *root;
    uint64_t      count;
    uint32_t      table_id;
} btree_t;

typedef void (*btree_iter_fn)(uint64_t key, void *value, void *ctx);

void  btree_init(btree_t *tree, uint32_t table_id);
void *btree_search(btree_t *tree, uint64_t key);
int   btree_insert(btree_t *tree, uint64_t key, void *value);
int   btree_delete(btree_t *tree, uint64_t key);
void  btree_scan(btree_t *tree, btree_iter_fn callback, void *ctx);
void  btree_destroy(btree_t *tree);

#endif /* VAULTOS_BTREE_H */
