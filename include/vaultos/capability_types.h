#ifndef VAULTOS_CAPABILITY_TYPES_H
#define VAULTOS_CAPABILITY_TYPES_H

#include "../../kernel/lib/types.h"

/* Capability rights (bitmask) */
#define CAP_READ      (1U << 0)
#define CAP_WRITE     (1U << 1)
#define CAP_EXECUTE   (1U << 2)
#define CAP_DELETE    (1U << 3)
#define CAP_GRANT     (1U << 4)   /* Can grant to others */
#define CAP_REVOKE    (1U << 5)   /* Can revoke delegated caps */
#define CAP_ALL       0x3F

/* Object types that capabilities reference */
typedef enum {
    CAP_OBJ_TABLE_ROW,     /* A specific row in a table */
    CAP_OBJ_TABLE,         /* An entire table */
    CAP_OBJ_PROCESS,       /* A process */
    CAP_OBJ_DEVICE,        /* A hardware device */
    CAP_OBJ_SYSTEM,        /* System-level operations */
} cap_object_type_t;

/* Capability token */
typedef struct {
    uint64_t          cap_id;
    uint64_t          object_id;
    cap_object_type_t object_type;
    uint64_t          owner_pid;
    uint32_t          rights;        /* Bitmask of CAP_READ, etc. */
    uint8_t           hmac[32];      /* HMAC-SHA256 seal */
    uint64_t          parent_cap_id; /* 0 if root capability */
    uint64_t          created_at;    /* Tick timestamp */
    uint64_t          expires_at;    /* 0 = never expires */
    bool              revoked;
} capability_t;

#endif /* VAULTOS_CAPABILITY_TYPES_H */
