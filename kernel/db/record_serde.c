#include "record_serde.h"
#include "../lib/string.h"
#include "../../include/vaultos/db_types.h"

/* Little-endian writers */
static void wr_u8(uint8_t *buf, size_t *off, uint8_t v) {
    buf[*off] = v;
    *off += 1;
}

static void wr_u16(uint8_t *buf, size_t *off, uint16_t v) {
    buf[*off + 0] = (v >> 0) & 0xFF;
    buf[*off + 1] = (v >> 8) & 0xFF;
    *off += 2;
}

static void wr_u32(uint8_t *buf, size_t *off, uint32_t v) {
    buf[*off + 0] = (v >> 0) & 0xFF;
    buf[*off + 1] = (v >> 8) & 0xFF;
    buf[*off + 2] = (v >> 16) & 0xFF;
    buf[*off + 3] = (v >> 24) & 0xFF;
    *off += 4;
}

static void wr_u64(uint8_t *buf, size_t *off, uint64_t v) {
    for (int i = 0; i < 8; i++)
        buf[*off + i] = (v >> (i * 8)) & 0xFF;
    *off += 8;
}

/* Little-endian readers */
static uint8_t rd_u8(const uint8_t *buf, size_t *off) {
    uint8_t v = buf[*off];
    *off += 1;
    return v;
}

static uint16_t rd_u16(const uint8_t *buf, size_t *off) {
    uint16_t v = (uint16_t)buf[*off] | ((uint16_t)buf[*off + 1] << 8);
    *off += 2;
    return v;
}

static uint32_t rd_u32(const uint8_t *buf, size_t *off) {
    uint32_t v = (uint32_t)buf[*off]
               | ((uint32_t)buf[*off + 1] << 8)
               | ((uint32_t)buf[*off + 2] << 16)
               | ((uint32_t)buf[*off + 3] << 24);
    *off += 4;
    return v;
}

static uint64_t rd_u64(const uint8_t *buf, size_t *off) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= (uint64_t)buf[*off + i] << (i * 8);
    *off += 8;
    return v;
}

size_t record_serialize(const record_t *rec, uint8_t *buf, size_t buf_size) {
    size_t off = 0;

    /* Header: row_id(8) + table_id(4) + field_count(4) = 16 bytes minimum */
    if (buf_size < 16) return 0;

    wr_u64(buf, &off, rec->row_id);
    wr_u32(buf, &off, rec->table_id);
    wr_u32(buf, &off, rec->field_count);

    for (uint32_t i = 0; i < rec->field_count; i++) {
        const field_value_t *f = &rec->fields[i];

        if (off + 4 > buf_size) return 0;
        wr_u32(buf, &off, (uint32_t)f->type);

        switch (f->type) {
        case COL_U64:
            if (off + 8 > buf_size) return 0;
            wr_u64(buf, &off, f->u64_val);
            break;
        case COL_I64:
            if (off + 8 > buf_size) return 0;
            wr_u64(buf, &off, (uint64_t)f->i64_val);
            break;
        case COL_U32:
            if (off + 4 > buf_size) return 0;
            wr_u32(buf, &off, f->u32_val);
            break;
        case COL_U8:
            if (off + 1 > buf_size) return 0;
            wr_u8(buf, &off, f->u8_val);
            break;
        case COL_BOOL:
            if (off + 1 > buf_size) return 0;
            wr_u8(buf, &off, f->bool_val ? 1 : 0);
            break;
        case COL_STR:
            if (off + 2 + f->str_val.length > buf_size) return 0;
            wr_u16(buf, &off, f->str_val.length);
            memcpy(buf + off, f->str_val.data, f->str_val.length);
            off += f->str_val.length;
            break;
        case COL_BLOB:
            if (off + 4 + f->blob_val.length > buf_size) return 0;
            wr_u32(buf, &off, f->blob_val.length);
            memcpy(buf + off, f->blob_val.data, f->blob_val.length);
            off += f->blob_val.length;
            break;
        }
    }

    return off;
}

size_t record_deserialize(record_t *rec, const uint8_t *buf, size_t buf_size) {
    size_t off = 0;

    if (buf_size < 16) return 0;

    memset(rec, 0, sizeof(*rec));
    rec->row_id = rd_u64(buf, &off);
    rec->table_id = rd_u32(buf, &off);
    rec->field_count = rd_u32(buf, &off);

    if (rec->field_count > MAX_COLUMNS) return 0;

    for (uint32_t i = 0; i < rec->field_count; i++) {
        field_value_t *f = &rec->fields[i];

        if (off + 4 > buf_size) return 0;
        f->type = (column_type_t)rd_u32(buf, &off);

        switch (f->type) {
        case COL_U64:
            if (off + 8 > buf_size) return 0;
            f->u64_val = rd_u64(buf, &off);
            break;
        case COL_I64:
            if (off + 8 > buf_size) return 0;
            f->i64_val = (int64_t)rd_u64(buf, &off);
            break;
        case COL_U32:
            if (off + 4 > buf_size) return 0;
            f->u32_val = rd_u32(buf, &off);
            break;
        case COL_U8:
            if (off + 1 > buf_size) return 0;
            f->u8_val = rd_u8(buf, &off);
            break;
        case COL_BOOL:
            if (off + 1 > buf_size) return 0;
            f->bool_val = rd_u8(buf, &off) ? true : false;
            break;
        case COL_STR: {
            if (off + 2 > buf_size) return 0;
            uint16_t len = rd_u16(buf, &off);
            if (len > MAX_STR_LEN || off + len > buf_size) return 0;
            memcpy(f->str_val.data, buf + off, len);
            f->str_val.data[len] = '\0';
            f->str_val.length = len;
            off += len;
            break;
        }
        case COL_BLOB: {
            if (off + 4 > buf_size) return 0;
            uint32_t len = rd_u32(buf, &off);
            if (len > MAX_BLOB_LEN || off + len > buf_size) return 0;
            memcpy(f->blob_val.data, buf + off, len);
            f->blob_val.length = len;
            off += len;
            break;
        }
        default:
            return 0;
        }
    }

    return off;
}
