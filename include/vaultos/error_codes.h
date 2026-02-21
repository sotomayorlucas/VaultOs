#ifndef VAULTOS_ERROR_CODES_H
#define VAULTOS_ERROR_CODES_H

#define VOS_OK               0
#define VOS_ERR_GENERIC     -1
#define VOS_ERR_NOMEM       -2   /* Out of memory */
#define VOS_ERR_INVAL       -3   /* Invalid argument */
#define VOS_ERR_NOTFOUND    -4   /* Record/object not found */
#define VOS_ERR_PERM        -5   /* Permission denied (capability check failed) */
#define VOS_ERR_EXISTS      -6   /* Already exists */
#define VOS_ERR_FULL        -7   /* Table/queue full */
#define VOS_ERR_SYNTAX      -8   /* Query syntax error */
#define VOS_ERR_CAP_INVALID -9   /* Capability HMAC verification failed */
#define VOS_ERR_CAP_EXPIRED -10  /* Capability expired */
#define VOS_ERR_CAP_REVOKED -11  /* Capability revoked */
#define VOS_ERR_TXN_ABORT   -12  /* Transaction aborted */
#define VOS_ERR_IO          -13  /* I/O error */
#define VOS_ERR_OVERFLOW    -14  /* Buffer overflow */
#define VOS_ERR_BUSY        -15  /* Resource busy */
#define VOS_ERR_NOSYS       -16  /* Syscall not implemented */

/* Convert error code to string */
static inline const char *vos_strerror(int err) {
    switch (err) {
        case VOS_OK:               return "OK";
        case VOS_ERR_GENERIC:      return "Generic error";
        case VOS_ERR_NOMEM:        return "Out of memory";
        case VOS_ERR_INVAL:        return "Invalid argument";
        case VOS_ERR_NOTFOUND:     return "Not found";
        case VOS_ERR_PERM:         return "Permission denied";
        case VOS_ERR_EXISTS:       return "Already exists";
        case VOS_ERR_FULL:         return "Table full";
        case VOS_ERR_SYNTAX:       return "Syntax error";
        case VOS_ERR_CAP_INVALID:  return "Invalid capability";
        case VOS_ERR_CAP_EXPIRED:  return "Capability expired";
        case VOS_ERR_CAP_REVOKED:  return "Capability revoked";
        case VOS_ERR_TXN_ABORT:    return "Transaction aborted";
        case VOS_ERR_IO:           return "I/O error";
        case VOS_ERR_OVERFLOW:     return "Buffer overflow";
        case VOS_ERR_BUSY:         return "Resource busy";
        case VOS_ERR_NOSYS:        return "Not implemented";
        default:                   return "Unknown error";
    }
}

#endif /* VAULTOS_ERROR_CODES_H */
