#include "cap_table.h"
#include "../../include/vaultos/error_codes.h"
#include "../lib/string.h"

/* Simple array-based capability table for bootstrap phase */
static capability_t table[CAP_TABLE_SIZE];
static uint64_t     count = 0;

void cap_table_init(void) {
    memset(table, 0, sizeof(table));
    count = 0;
}

int cap_table_insert(const capability_t *cap) {
    if (count >= CAP_TABLE_SIZE) return VOS_ERR_FULL;
    if (cap->cap_id == 0 || cap->cap_id > CAP_TABLE_SIZE) return VOS_ERR_FULL;

    /* O(1) direct indexing: cap_id maps to slot cap_id-1 */
    uint64_t idx = cap->cap_id - 1;
    table[idx] = *cap;
    count++;
    return VOS_OK;
}

capability_t *cap_table_lookup(uint64_t cap_id) {
    /* O(1) direct indexing instead of O(N) linear scan */
    if (cap_id == 0 || cap_id > CAP_TABLE_SIZE) return NULL;
    capability_t *cap = &table[cap_id - 1];
    return (cap->cap_id == cap_id) ? cap : NULL;
}

int cap_table_remove(uint64_t cap_id) {
    if (cap_id == 0 || cap_id > CAP_TABLE_SIZE) return VOS_ERR_NOTFOUND;
    uint64_t idx = cap_id - 1;
    if (table[idx].cap_id != cap_id) return VOS_ERR_NOTFOUND;
    memset(&table[idx], 0, sizeof(capability_t));
    count--;
    return VOS_OK;
}

uint64_t cap_table_count(void) {
    return count;
}
