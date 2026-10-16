#include "sn_bfsk.h"
#include "sn_wire.h"
#include <string.h>

static const int16_t sine16[16]={0,383,707,924,1000,924,707,383,0,-383,-707,-924,-1000,-924,-707,-383};

int sn_bfsk_tx_start(sn_bfsk_tx_t *tx,uint16_t seq,const uint8_t *payload,uint8_t length)
{
    unsigned i;uint16_t crc;uint8_t *body;
    if(!tx || length>SN_BFSK_MAX_PAYLOAD || (!payload && length))return -1;
    memset(tx,0,sizeof(*tx));
    for(i=0;i<SN_BFSK_PREAMBLE;++i)tx->bytes[i]=0x55;
    tx->bytes[8]=0xd3;tx->bytes[9]=0x91;tx->bytes[10]=0xa6;tx->bytes[11]=0x5c;
    body=tx->bytes+12;body[0]=(uint8_t)(seq>>8);body[1]=(uint8_t)seq;body[2]=length;
    if(length)memcpy(body+3,payload,length);
    crc=sn_crc16(body,3u+length);
    body[3u+length]=(uint8_t)crc;body[4u+length]=(uint8_t)(crc>>8);
    tx->byte_count=(uint16_t)(12u+5u+length);
    return 0;
}

int sn_bfsk_tx_busy(const sn_bfsk_tx_t *tx)
{
    return tx && tx->byte_index<tx->byte_count;
}

uint16_t sn_bfsk_tx_sample(sn_bfsk_tx_t *tx)
{
    uint8_t bit;uint16_t result;
    if(!sn_bfsk_tx_busy(tx))return 2048;
    bit=(uint8_t)((tx->bytes[tx->byte_index]>>(7u-tx->bit_index))&1u);
    result=(uint16_t)(2048+sine16[tx->phase]);
    tx->phase=(uint8_t)((tx->phase+(bit?3u:1u))&15u);
    if(++tx->sample_in_bit==SN_BFSK_SPS){
        tx->sample_in_bit=0;
        if(++tx->bit_index==8u){tx->bit_index=0;++tx->byte_index;}
    }
    return result;
}

static float goertzel(const float *x,float coefficient)
{
    unsigned i;float s1=0,s2=0;
    /* 系数=2cos(2πf/Fs)，固定频点预计算，采样循环里不调用三角函数。 */
    for(i=0;i<SN_BFSK_SPS;++i){float s=x[i]+coefficient*s1-s2;s2=s1;s1=s;}
    return (s1*s1+s2*s2-coefficient*s1*s2)/(SN_BFSK_SPS*SN_BFSK_SPS);
}

int sn_bfsk_decide(const float symbol[SN_BFSK_SPS],float *e0,float *e1)
{
    float a=goertzel(symbol,1.8477590650225735f);
    float b=goertzel(symbol,0.7653668647301797f);
    /* 浮点舍入可产生极小负数，能量输出限制到非负。 */
    if(a<0)a=0;
    if(b<0)b=0;
    if(e0)*e0=a;
    if(e1)*e1=b;
    return b>a;
}

static void search_reset(sn_bfsk_rx_t *rx)
{
    memset(rx->candidates,0,sizeof(rx->candidates));
    rx->locked=0;rx->body_count=0;rx->byte_bits=0;rx->current_byte=0;rx->quiet_symbols=0;
}

void sn_bfsk_rx_init(sn_bfsk_rx_t *rx)
{
    memset(rx,0,sizeof(*rx));
    rx->min_energy=0.0016f; /* 约80mV峰值的单音，真实硬件要根据量程校准。 */
    rx->min_confidence=0.2f;
}

static void packet_bit(sn_bfsk_rx_t *rx,int bit,sn_bfsk_callback callback,void *context)
{
    rx->current_byte=(uint8_t)((rx->current_byte<<1)|(unsigned)bit);
    if(++rx->byte_bits==8u){
        rx->body[rx->body_count++]=rx->current_byte;
        rx->current_byte=0;rx->byte_bits=0;
        if(rx->body_count==3u && rx->body[2]>SN_BFSK_MAX_PAYLOAD){
            ++rx->length_errors;search_reset(rx);return;
        }
        if(rx->body_count>=3u && rx->body_count==(uint16_t)(5u+rx->body[2])){
            sn_bfsk_frame_t frame;
            uint16_t n=rx->body_count;
            uint16_t crc=(uint16_t)(rx->body[n-2]|((uint16_t)rx->body[n-1]<<8));
            memset(&frame,0,sizeof(frame));
            frame.sequence=(uint16_t)(((uint16_t)rx->body[0]<<8)|rx->body[1]);
            frame.length=rx->body[2];frame.sync_end_sample=rx->sync_end_sample;
            frame.crc_ok=(uint8_t)(crc==sn_crc16(rx->body,n-2u));
            memcpy(frame.payload,rx->body+3,frame.length);
            if(frame.crc_ok)++rx->frames_ok;else ++rx->crc_errors;
            /* 诊断回调也能看到CRC失败的完整帧，应用层必须检查crc_ok。
             * 这样链路实验不会只统计成功帧，得到看似永远为0的BER。 */
            if(callback)callback(context,&frame);
            search_reset(rx);
        }
    }
}

void sn_bfsk_rx_push(sn_bfsk_rx_t *rx,const float *volts,size_t count,uint64_t first,
                     sn_bfsk_callback callback,void *context)
{
    size_t i;
    if(!rx || (!volts && count))return;
    if(rx->have_sequence && first!=rx->expected_sample){
        ++rx->discontinuities;search_reset(rx);rx->sample_count=0;rx->write_index=0;
    }
    rx->have_sequence=1;rx->expected_sample=first+count;
    for(i=0;i<count;++i){
        uint8_t phase;float symbol[SN_BFSK_SPS],mean=0,e0,e1,confidence,strong;
        unsigned j;int bit;
        rx->samples[rx->write_index++]=volts[i];
        if(rx->write_index==SN_BFSK_SPS)rx->write_index=0;
        ++rx->sample_count;
        if(rx->sample_count<SN_BFSK_SPS || rx->sample_count%20u)continue;
        phase=(uint8_t)(((rx->sample_count-SN_BFSK_SPS)/20u)&3u);
        if(rx->locked && phase!=rx->locked_phase)continue;
        /* 搜索期保留4种相差20点的符号起点；锁定后每80点判决一次。
         * 这是初始定时捕获，不是持续时钟恢复PLL；短帧漂移容限要实测。 */
        for(j=0;j<SN_BFSK_SPS;++j){
            symbol[j]=rx->samples[(rx->write_index+j)%SN_BFSK_SPS];mean+=symbol[j];
        }
        mean/=SN_BFSK_SPS;
        for(j=0;j<SN_BFSK_SPS;++j)symbol[j]-=mean;
        bit=sn_bfsk_decide(symbol,&e0,&e1);
        strong=e0>e1?e0:e1;
        confidence=(e0+e1)>0.0f?(strong-(e0<e1?e0:e1))/(e0+e1):0.0f;
        if(rx->locked){
            if(strong<rx->min_energy){
                if(++rx->quiet_symbols>=8u){++rx->lost_sync;search_reset(rx);continue;}
            }else rx->quiet_symbols=0;
            packet_bit(rx,bit,callback,context);
        }else{
            sn_bfsk_candidate_t *candidate=&rx->candidates[phase];
            if(strong<rx->min_energy || confidence<rx->min_confidence){
                candidate->shift=0;candidate->preamble_hold=0;continue;
            }
            candidate->shift=(candidate->shift<<1)|(uint32_t)bit;
            if(candidate->preamble_hold)--candidate->preamble_hold;
            if(candidate->shift==0x55555555u || candidate->shift==0xaaaaaaaau)
                candidate->preamble_hold=40;
            if(candidate->shift==SN_BFSK_SYNC && candidate->preamble_hold){
                rx->locked=1;rx->locked_phase=phase;rx->body_count=0;
                rx->current_byte=rx->byte_bits=rx->quiet_symbols=0;
                rx->sync_end_sample=first+i;
                ++rx->syncs;
            }
        }
    }
}
