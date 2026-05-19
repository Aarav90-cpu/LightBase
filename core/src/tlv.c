#include "tlv.h"
#include <string.h>

int parse_binary_tlv_frame(const uint8_t* stream, size_t stream_len, TLVCommandPacket* out_packet) {
    if (!stream || stream_len == 0 || !out_packet) return -1;

    // Zero out the target wrapper structural boundaries
    memset(out_packet, 0, sizeof(TLVCommandPacket));

    size_t offset = 0;

    // Slide our pointer forward through the binary array chunk bounds sequentially
    while (offset < stream_len) {
        // Ensure there are at least 3 bytes remaining to extract Tag (1B) and Length (2B)
        if (offset + 3 > stream_len) return -1;

        uint8_t tag = stream[offset];
        uint16_t length = (stream[offset + 1] << 8) | stream[offset + 2]; // Big-Endian extraction
        offset += 3;

        // Safety boundary check against malformed overflow packets
        if (offset + length > stream_len) return -1;

        switch (tag) {
            case TLV_TAG_TARGET:
                if (length < sizeof(out_packet->target)) {
                    memcpy(out_packet->target, &stream[offset], length);
                    out_packet->target[length] = '\0';
                }
                break;
            case LIGHTBASE_TAG_DB_PATH:
                if (length < sizeof(out_packet->db_path)) {
                    memcpy(out_packet->db_path, &stream[offset], length);
                    out_packet->db_path[length] = '\0';
                }
                break;
            case TLV_TAG_QUERY:
                if (length < sizeof(out_packet->query)) {
                    memcpy(out_packet->query, &stream[offset], length);
                    out_packet->query[length] = '\0';
                }
                break;
            case TLV_TAG_HOST:
                if (length < sizeof(out_packet->hostname)) {
                    memcpy(out_packet->hostname, &stream[offset], length);
                    out_packet->hostname[length] = '\0';
                }
                break;
            case TLV_TAG_PATH:
                if (length < sizeof(out_packet->path)) {
                    memcpy(out_packet->path, &stream[offset], length);
                    out_packet->path[length] = '\0';
                }
                break;
            case TLV_TAG_HEADERS:
                if (length < sizeof(out_packet->headers)) {
                    memcpy(out_packet->headers, &stream[offset], length);
                    out_packet->headers[length] = '\0';
                }
                break;
            case TLV_TAG_METHOD:
                if (length < sizeof(out_packet->method)) {
                    memcpy(out_packet->method, &stream[offset], length);
                    out_packet->method[length] = '\0';
                }
                break;
            case TLV_TAG_BODY:
                if (length < sizeof(out_packet->body)) {
                    memcpy(out_packet->body, &stream[offset], length);
                    out_packet->body[length] = '\0';
                }
                break;
            case TLV_TAG_FORM:
                if (length < sizeof(out_packet->form_data)) {
                    memcpy(out_packet->form_data, &stream[offset], length);
                    out_packet->form_data[length] = '\0';
                }
                break;
            default:
                // Skip unknown tags dynamically to maintain backward compatibility
                break;
        }

        offset += length; // Slide past value bits instantly!
    }
    return 0; // Successfully unpacked binary packet layout frame
}