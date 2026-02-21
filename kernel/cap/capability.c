#include "capability.h"
#include "cap_table.h"
#include "../crypto/hmac.h"
#include "../crypto/random.h"
#include "../arch/x86_64/pit.h"
#include "../lib/printf.h"
#include "../lib/string.h"

/* Master key for capability HMAC - generated once at boot */
static uint8_t master_key[32];
static hmac_ctx_t master_hmac_ctx;  /* Pre-computed HMAC state for master_key */
static uint64_t next_cap_id = 1;

/* Validation cache: avoids recomputing HMAC for recently validated caps */
#define CAP_CACHE_SIZE 64
#define CAP_CACHE_TTL  1000  /* ticks of validity (~1 second) */

typedef struct {
    uint64_t cap_id;
    uint64_t validated_at;
    bool     valid;
    bool     occupied;
} cap_cache_entry_t;

static cap_cache_entry_t cap_cache[CAP_CACHE_SIZE];

static void cap_cache_invalidate(uint64_t cap_id) {
    uint64_t idx = cap_id % CAP_CACHE_SIZE;
    if (cap_cache[idx].cap_id == cap_id)
        cap_cache[idx].occupied = false;
}

void cap_init(void) {
    random_bytes(master_key, sizeof(master_key));
    hmac_ctx_init(&master_hmac_ctx, master_key, sizeof(master_key));
    memset(cap_cache, 0, sizeof(cap_cache));
    cap_table_init();

    /* Create root capability (system-wide, all rights) */
    capability_t root = cap_create(0, CAP_OBJ_SYSTEM, 0, CAP_ALL, 0);
    cap_table_insert(&root);

    kprintf("[CAP] Capability system initialized (root cap_id=%llu)\n", root.cap_id);
}

/* Compute HMAC for a capability using pre-computed master key context */
static void cap_compute_hmac(capability_t *cap) {
    uint8_t data[40];
    memcpy(data + 0,  &cap->cap_id, 8);
    memcpy(data + 8,  &cap->object_id, 8);
    memcpy(data + 16, &cap->owner_pid, 8);
    memcpy(data + 24, &cap->rights, 4);
    uint32_t otype = (uint32_t)cap->object_type;
    memcpy(data + 28, &otype, 4);
    memcpy(data + 32, &cap->parent_cap_id, 8);

    hmac_ctx_compute(&master_hmac_ctx, data, sizeof(data), cap->hmac);
}

capability_t cap_create(uint64_t object_id, cap_object_type_t type,
                         uint64_t owner_pid, uint32_t rights,
                         uint64_t parent_cap_id) {
    capability_t cap;
    memset(&cap, 0, sizeof(cap));

    cap.cap_id = next_cap_id++;
    cap.object_id = object_id;
    cap.object_type = type;
    cap.owner_pid = owner_pid;
    cap.rights = rights;
    cap.parent_cap_id = parent_cap_id;
    cap.created_at = pit_get_ticks();
    cap.expires_at = 0;  /* Never expires by default */
    cap.revoked = false;

    cap_compute_hmac(&cap);
    return cap;
}

bool cap_validate(const capability_t *cap) {
    if (cap->revoked) return false;
    if (cap->expires_at != 0 && pit_get_ticks() > cap->expires_at) return false;

    /* Check validation cache first */
    uint64_t idx = cap->cap_id % CAP_CACHE_SIZE;
    uint64_t now = pit_get_ticks();
    if (cap_cache[idx].occupied &&
        cap_cache[idx].cap_id == cap->cap_id &&
        (now - cap_cache[idx].validated_at) < CAP_CACHE_TTL) {
        return cap_cache[idx].valid;
    }

    /* Cache miss: verify HMAC */
    capability_t tmp = *cap;
    cap_compute_hmac(&tmp);
    bool valid = hmac_verify(cap->hmac, tmp.hmac, 32);

    /* Update cache */
    cap_cache[idx].cap_id = cap->cap_id;
    cap_cache[idx].validated_at = now;
    cap_cache[idx].valid = valid;
    cap_cache[idx].occupied = true;

    return valid;
}

bool cap_check(uint64_t pid, uint64_t object_id, uint32_t required_rights) {
    /* PID 0 (kernel) always has access */
    if (pid == 0) return true;

    /* Search for a matching capability */
    for (uint64_t i = 1; i < next_cap_id; i++) {
        capability_t *cap = cap_table_lookup(i);
        if (!cap) continue;
        if (cap->owner_pid != pid) continue;
        if (cap->object_id != object_id && cap->object_type != CAP_OBJ_SYSTEM) continue;
        if ((cap->rights & required_rights) != required_rights) continue;
        if (!cap_validate(cap)) continue;
        return true;
    }
    return false;
}

int cap_grant(uint64_t granter_pid, uint64_t cap_id,
               uint64_t target_pid, uint32_t rights) {
    capability_t *parent = cap_table_lookup(cap_id);
    if (!parent) return VOS_ERR_NOTFOUND;
    if (parent->owner_pid != granter_pid) return VOS_ERR_PERM;
    if (!cap_validate(parent)) return VOS_ERR_CAP_INVALID;
    if (!(parent->rights & CAP_GRANT)) return VOS_ERR_PERM;

    /* New cap can only have a subset of parent's rights */
    uint32_t granted = rights & parent->rights;

    capability_t child = cap_create(parent->object_id, parent->object_type,
                                     target_pid, granted, cap_id);
    return cap_table_insert(&child);
}

int cap_revoke(uint64_t owner_pid, uint64_t cap_id) {
    capability_t *cap = cap_table_lookup(cap_id);
    if (!cap) return VOS_ERR_NOTFOUND;
    if (cap->owner_pid != owner_pid && owner_pid != 0) return VOS_ERR_PERM;

    cap->revoked = true;
    cap_cache_invalidate(cap_id);

    /* Cascade: revoke all capabilities whose parent is this cap */
    for (uint64_t i = 1; i < next_cap_id; i++) {
        capability_t *child = cap_table_lookup(i);
        if (child && child->parent_cap_id == cap_id && !child->revoked) {
            cap_revoke(owner_pid, child->cap_id); /* Recursive cascade */
        }
    }

    return VOS_OK;
}

int cap_delegate(uint64_t from_pid, uint64_t cap_id, uint64_t to_pid) {
    capability_t *cap = cap_table_lookup(cap_id);
    if (!cap) return VOS_ERR_NOTFOUND;
    if (cap->owner_pid != from_pid) return VOS_ERR_PERM;
    if (!cap_validate(cap)) return VOS_ERR_CAP_INVALID;

    cap->owner_pid = to_pid;
    cap_compute_hmac(cap); /* Re-seal with new owner */
    cap_cache_invalidate(cap_id);
    return VOS_OK;
}

uint64_t cap_get_ticks(void) {
    return pit_get_ticks();
}
