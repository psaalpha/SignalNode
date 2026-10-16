#ifndef SN_WIRE_H
#define SN_WIRE_H
#include <stddef.h>
#include <stdint.h>
#define SN_WIRE_HEADER 20u
#define SN_WIRE_MAX_PAYLOAD 1024u
#define SN_WIRE_MAX_FRAME (SN_WIRE_HEADER + SN_WIRE_MAX_PAYLOAD + 2u)
typedef struct {
    uint8_t buffer[SN_WIRE_MAX_FRAME];
    size_t used;
    uint32_t good_frames, crc_errors, discarded_bytes, timeouts;
} sn_wire_parser_t;
typedef void (*sn_wire_callback)(void *context, uint8_t type, uint32_t sequence,
                                uint64_t first_sample, uint16_t fs,
                                const uint8_t *payload, uint16_t length);
uint16_t sn_crc16(const uint8_t *bytes, size_t count);
size_t sn_wire_encode(uint8_t *out, size_t capacity, uint8_t type, uint32_t sequence,
                      uint64_t first_sample, uint16_t fs,
                      const uint8_t *payload, uint16_t length);
void sn_wire_push(sn_wire_parser_t *parser, const uint8_t *bytes, size_t count,
                   sn_wire_callback callback, void *context);
/* live UART的接收超时由平台时钟触发。不得永远等待损坏长度字段声明的数据。 */
void sn_wire_timeout(sn_wire_parser_t *parser);
#endif
