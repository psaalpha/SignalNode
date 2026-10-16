#ifndef SN_BFSK_H
#define SN_BFSK_H
#include <stddef.h>
#include <stdint.h>

/* 8kSa/s、100bit/s => 每符号80点。两个音分别落在80点DFT的5/15号bin。
 * 这是低速有线音频带链路，不是射频收发器，也不是调制算法性能认证。 */
#define SN_BFSK_SPS 80u
#define SN_BFSK_MAX_PAYLOAD 32u
#define SN_BFSK_PREAMBLE 8u
#define SN_BFSK_SYNC 0xd391a65cu
#define SN_BFSK_BODY_MAX (3u+SN_BFSK_MAX_PAYLOAD+2u)
#define SN_BFSK_FRAME_MAX (SN_BFSK_PREAMBLE+4u+SN_BFSK_BODY_MAX)

typedef struct {
    uint8_t bytes[SN_BFSK_FRAME_MAX];
    uint16_t byte_count, byte_index;
    uint8_t bit_index, phase;
    uint16_t sample_in_bit;
} sn_bfsk_tx_t;

typedef struct {
    uint32_t shift;
    uint8_t preamble_hold;
} sn_bfsk_candidate_t;

typedef struct {
    uint16_t sequence;
    uint8_t length, crc_ok;
    uint8_t payload[SN_BFSK_MAX_PAYLOAD];
    uint64_t sync_end_sample;
} sn_bfsk_frame_t;
typedef void (*sn_bfsk_callback)(void *context,const sn_bfsk_frame_t *frame);

typedef struct {
    float samples[SN_BFSK_SPS];
    sn_bfsk_candidate_t candidates[4];
    uint8_t body[SN_BFSK_BODY_MAX];
    uint64_t sample_count;
    uint64_t expected_sample, sync_end_sample;
    uint16_t write_index, body_count;
    uint8_t locked, locked_phase, current_byte, byte_bits, quiet_symbols;
    uint8_t have_sequence;
    float min_energy, min_confidence;
    uint32_t syncs, frames_ok, crc_errors, length_errors, lost_sync, discontinuities;
} sn_bfsk_rx_t;

/* CRC覆盖seq高字节、seq低字节、长度、payload；CRC低字节先发。
 * 各字节均MSB先发；8个0x55前导后跟D3 91 A6 5C同步字。 */
int sn_bfsk_tx_start(sn_bfsk_tx_t *tx,uint16_t seq,const uint8_t *payload,uint8_t length);
uint16_t sn_bfsk_tx_sample(sn_bfsk_tx_t *tx);
int sn_bfsk_tx_busy(const sn_bfsk_tx_t *tx);
void sn_bfsk_rx_init(sn_bfsk_rx_t *rx);
void sn_bfsk_rx_push(sn_bfsk_rx_t *rx,const float *volts,size_t count,uint64_t first_sample,
                     sn_bfsk_callback callback,void *context);
/* 诊断接口：已知符号边界下的两路Goertzel能量，用于独立BER实验。
 * e0/e1按N²归一化；单音A峰值时目标通道能量约A²/4。 */
int sn_bfsk_decide(const float symbol[SN_BFSK_SPS],float *e0,float *e1);
#endif
