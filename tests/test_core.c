#include "sn_dsp.h"
#include "sn_host_fft.h"
#include "sn_wire.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
static sn_dsp_t dsp;
static uint16_t adc[SN_HOP];
static unsigned callbacks;
static uint32_t last_seq;
static uint8_t last_payload[SN_WIRE_MAX_PAYLOAD];
static uint16_t last_length;
static uint64_t last_first;
static void capture(void *ctx,uint8_t type,uint32_t seq,uint64_t first,uint16_t fs,
                    const uint8_t *payload,uint16_t length)
{
    (void)ctx;(void)type;(void)fs;
    ++callbacks;last_seq=seq;last_length=length;last_first=first;
    if(length)memcpy(last_payload,payload,length);
}

static int test_crc_and_wire(void)
{
    sn_wire_parser_t p={0};
    uint8_t payload[SN_WIRE_MAX_PAYLOAD],frame[SN_WIRE_MAX_FRAME],bad[SN_WIRE_MAX_FRAME];
    size_t i,n;uint32_t rng=0x12345678u;
    CHECK(sn_crc16((const uint8_t *)"123456789",9)==0x4b37u);
    for(i=0;i<sizeof(payload);++i)payload[i]=(uint8_t)i;
    n=sn_wire_encode(frame,sizeof(frame),1,0xffffffffu,0x100000004ull,8000,payload,sizeof(payload));
    CHECK(n==sizeof(frame));
    CHECK(sn_wire_encode(frame,n-1,1,0,0,8000,payload,sizeof(payload))==0);
    CHECK(sn_wire_encode(frame,n,1,0,0,0,payload,sizeof(payload))==0);
    callbacks=0;
    /* 一个字节一个字节喂入，覆盖最大帧、32位序号边界和64位采样索引。 */
    for(i=0;i<n;++i)sn_wire_push(&p,frame+i,1,capture,NULL);
    CHECK(callbacks==1 && last_seq==0xffffffffu && last_first==0x100000004ull);
    CHECK(last_length==sizeof(payload) && memcmp(payload,last_payload,sizeof(payload))==0);
    memcpy(bad,frame,n);bad[32]^=0x80;
    sn_wire_push(&p,bad,n,capture,NULL);
    sn_wire_push(&p,frame,n,capture,NULL);
    CHECK(callbacks==2 && p.crc_errors>0);
    /* 多帧连续输入不依赖UART回调正好按整帧触发。 */
    sn_wire_push(&p,frame,n,capture,NULL);sn_wire_push(&p,frame,n,capture,NULL);
    CHECK(callbacks==4);
    /* 固定种子的随机噪声：有界缓存、随机截断、恢复后的合法帧。 */
    for(i=0;i<20000;++i){uint8_t b;rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;b=(uint8_t)rng;
        sn_wire_push(&p,&b,1,capture,NULL);CHECK(p.used<=sizeof(p.buffer));}
    sn_wire_timeout(&p);
    callbacks=0;sn_wire_push(&p,frame,n,capture,NULL);CHECK(callbacks==1);
    /* 损坏长度可导致等待：实时链路需要平台触发超时，而非无限挂起。 */
    sn_wire_push(&p,frame,30,capture,NULL);CHECK(p.used==30);
    sn_wire_timeout(&p);CHECK(p.used==0 && p.timeouts>0);
    return 0;
}

static int test_fft_layout(void)
{
    float x[SN_N],packed[SN_N];uint32_t i;
    for(i=0;i<SN_N;++i)x[i]=1.0f+((i&1u)?-0.25f:0.25f);
    CHECK(sn_host_fft(NULL,x,packed)==0);
    CHECK(fabsf(packed[0]-1024.0f)<0.01f);
    CHECK(fabsf(packed[1]-256.0f)<0.01f);
    for(i=2;i<SN_N;++i)CHECK(fabsf(packed[i])<0.01f);
    return 0;
}

static void make_tone(uint64_t first,float hz)
{
    uint32_t i;
    for(i=0;i<SN_HOP;++i){float x=1.65f+0.6f*cosf(6.28318530718f*hz*(float)(first+i)/SN_FS);
        adc[i]=(uint16_t)lroundf(x*4095.0f/3.3f);}
}

static int test_dsp_contracts(void)
{
    sn_config_t cfg={3.3f,0,1};sn_features_t f;
    CHECK(sn_dsp_init(NULL,&cfg,sn_host_fft,NULL)==-1);
    cfg.vref=NAN;CHECK(sn_dsp_init(&dsp,&cfg,sn_host_fft,NULL)==-1);cfg.vref=3.3f;
    cfg.lowpass_hz=4000;CHECK(sn_dsp_init(&dsp,&cfg,sn_host_fft,NULL)==-1);cfg.lowpass_hz=0;
    CHECK(sn_dsp_init(&dsp,&cfg,sn_host_fft,NULL)==0);
    make_tone(0,625);CHECK(sn_dsp_push_adc(&dsp,adc,0,&f)==0);
    make_tone(512,625);CHECK(sn_dsp_push_adc(&dsp,adc,512,&f)==1);
    CHECK(f.first_sample==0 && f.discontinuities==0);
    CHECK(fabsf(f.raw_ac_rms_v-0.6f/sqrtf(2.0f))<0.001f);
    CHECK(fabsf(f.peak_hz-625.0f)<0.05f && fabsf(f.peak_amplitude_v-0.6f)<0.001f);
    CHECK(fabsf(f.band_power_v2-0.18f)<0.001f);
    adc[0]=4096;CHECK(sn_dsp_push_adc(&dsp,adc,1024,&f)==-1);
    CHECK(dsp.expected_sample==1024); /* 坏输入没有推进半块状态。 */
    make_tone(2048,1000);CHECK(sn_dsp_push_adc(&dsp,adc,2048,&f)==0);
    make_tone(2560,1000);CHECK(sn_dsp_push_adc(&dsp,adc,2560,&f)==1);
    CHECK(f.first_sample==2048 && f.discontinuities==1 && fabsf(f.peak_hz-1000)<0.05f);
    CHECK(sn_dsp_push_adc(&dsp,adc,UINT64_MAX,&f)==-1);
    return 0;
}

int main(void)
{
    if(test_crc_and_wire())return 1;puts("PASS wire_crc_fragmentation_corruption_timeout_fuzz");
    if(test_fft_layout())return 1;puts("PASS fft_dc_nyquist_packing");
    if(test_dsp_contracts())return 1;puts("PASS dsp_rms_amplitude_power_gap_and_input_contracts");
    return 0;
}
