#ifndef VAULTOS_CAPABILITY_H
#define VAULTOS_CAPABILITY_H

#include "../../include/vaultos/capability_types.h"
#include "../../include/vaultos/error_codes.h"

void cap_init(void);

/* Create a new capability (kernel internal) */
capability_t cap_create(uint64_t object_id, cap_object_type_t type,
                         uint64_t owner_pid, uint32_t rights,
                         uint64_t parent_cap_id);

/* Validate capability HMAC */
bool cap_validate(const capability_t *cap);

/* Check if a process has rights on an object */
bool cap_check(uint64_t pid, uint64_t object_id, uint32_t required_rights);

/* Grant: create derived capability with subset of rights */
int cap_grant(uint64_t granter_pid, uint64_t cap_id,
               uint64_t target_pid, uint32_t rights);

/* Revoke: invalidate capability and all descendants */
int cap_revoke(uint64_t owner_pid, uint64_t cap_id);

/* Delegate: transfer ownership */
int cap_delegate(uint64_t from_pid, uint64_t cap_id, uint64_t to_pid);

/* Get current tick for timestamps */
uint64_t cap_get_ticks(void);

#endif /* VAULTOS_CAPABILITY_H */
