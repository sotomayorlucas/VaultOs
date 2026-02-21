#ifndef VAULTOS_DB_TYPES_H
#define VAULTOS_DB_TYPES_H

#include "../../kernel/lib/types.h"

/* Column types */
typedef enum {
    COL_U64,
    COL_I64,
    COL_STR,    /* Variable-length string (max 255 bytes) */
    COL_BLOB,   /* Variable-length binary data */
    COL_BOOL,
    COL_U32,
    COL_U8,
} column_type_t;

/* Query types */
typedef enum {
    QUERY_SELECT,
    QUERY_INSERT,
    QUERY_DELETE,
    QUERY_UPDATE,
    QUERY_SHOW_TABLES,
    QUERY_DESCRIBE,
    QUERY_GRANT,
    QUERY_REVOKE,
} query_type_t;

/* Comparison operators */
typedef enum {
    CMP_EQ,     /* = */
    CMP_NEQ,    /* != */
    CMP_LT,     /* < */
    CMP_GT,     /* > */
    CMP_LE,     /* <= */
    CMP_GE,     /* >= */
    CMP_LIKE,   /* LIKE */
} cmp_op_t;

/* Limits */
#define MAX_COLUMNS        16
#define MAX_TABLE_NAME     64
#define MAX_COLUMN_NAME    64
#define MAX_TABLES         16
#define MAX_RECORD_SIZE    4096
#define MAX_STR_LEN        255
#define MAX_BLOB_LEN       2048
#define MAX_WHERE_CONDS    8
#define MAX_SELECT_COLS    16
#define MAX_INSERT_VALS    16

/* System table IDs */
#define TABLE_ID_SYSTEM       0
#define TABLE_ID_PROCESS      1
#define TABLE_ID_CAPABILITY   2
#define TABLE_ID_OBJECT       3
#define TABLE_ID_MESSAGE      4
#define TABLE_ID_AUDIT        5

#endif /* VAULTOS_DB_TYPES_H */
