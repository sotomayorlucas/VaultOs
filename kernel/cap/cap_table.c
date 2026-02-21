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

    /* Find empty slot by cap_id == 0 */
    for (int i = 0; i < CAP_TABLE_SIZE; i++) {
        if (table[i].cap_id == 0) {
            table[i] = *cap;
            count++;
            return VOS_OK;
        }
    }
    return VOS_ERR_FULL;
}

capability_t *cap_table_lookup(uint64_t cap_id) {
    if (cap_id == 0) return NULL;
    for (int i = 0; i < CAP_TABLE_SIZE; i++) {
        if (table[i].cap_id == cap_id) return &table[i];
    }
    return NULL;
}

int cap_table_remove(uint64_t cap_id) {
    for (int i = 0; i < CAP_TABLE_SIZE; i++) {
        if (table[i].cap_id == cap_id) {
            memset(&table[i], 0, sizeof(capability_t));
            count--;
            return VOS_OK;
        }
    }
    return VOS_ERR_NOTFOUND;
}

uint64_t cap_table_count(void) {
    return count;
}
