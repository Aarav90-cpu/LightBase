#ifndef LIGHTBASE_TLV_H
#define LIGHTBASE_TLV_H

#include <stdint.h>
#include <stddef.h>

// Protocol Binary Tags
#define TLV_TAG_TARGET   0x01
#define LIGHTBASE_TAG_DB_PATH 0x02
#define TLV_TAG_QUERY    0x03
#define TLV_TAG_HOST     0x04
#define TLV_TAG_PATH     0x05
#define TLV_TAG_SCHEMA_SCAN 0x06

// Extracted Container Frame for Worker Routing
typedef struct {
    char target[64];
    char db_path[256];
    char query[1024];
    char hostname[256];
    char path[256];
} TLVCommandPacket;

// Protocol Extraction Function Interface Signature
int parse_binary_tlv_frame(const uint8_t* stream, size_t stream_len, TLVCommandPacket* out_packet);

#endif // LIGHTBASE_SYS_MONITOR_H