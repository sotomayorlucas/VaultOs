#ifndef VAULTOS_CAP_TABLE_H
#define VAULTOS_CAP_TABLE_H

#include "../../include/vaultos/capability_types.h"

#define CAP_TABLE_SIZE 1024

void           cap_table_init(void);
int            cap_table_insert(const capability_t *cap);
capability_t  *cap_table_lookup(uint64_t cap_id);
int            cap_table_remove(uint64_t cap_id);
uint64_t       cap_table_count(void);

#endif /* VAULTOS_CAP_TABLE_H */
