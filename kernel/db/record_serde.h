#ifndef VAULTOS_RECORD_SERDE_H
#define VAULTOS_RECORD_SERDE_H

#include "record.h"

/*
 * Record serialization for encrypt-then-MAC pipeline.
 *
 * Wire format:
 *   [row_id:8][table_id:4][field_count:4]
 *   Per field: [type:4][data...]
 *     COL_U64/I64: 8 bytes
 *     COL_U32: 4 bytes
 *     COL_U8/BOOL: 1 byte
 *     COL_STR: [length:2][data:length]
 *     COL_BLOB: [length:4][data:length]
 */

/* Serialize record to byte buffer.
 * Returns bytes written, or 0 on error.
 * buf must be at least MAX_RECORD_SIZE bytes. */
size_t record_serialize(const record_t *rec, uint8_t *buf, size_t buf_size);

/* Deserialize record from byte buffer.
 * Returns bytes consumed, or 0 on error. */
size_t record_deserialize(record_t *rec, const uint8_t *buf, size_t buf_size);

#endif /* VAULTOS_RECORD_SERDE_H */
