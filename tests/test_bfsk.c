#include "sn_bfsk.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
static sn_bfsk_tx_t tx;
static sn_bfsk_rx_t rx;
static unsigned got,bad;
static uint8_t expected[SN_BFSK_MAX_PAYLOAD];
static uint64_t cursor;
static void receive(void *ctx,const sn_bfsk_frame_t *frame)
{
    (void)ctx;
    if(frame->crc_ok && frame->sequence==0x1234 && frame->length==sizeof(expected) &&
       memcmp(frame->payload,expected,sizeof(expected))==0)++got;else ++bad;
}
static void push(float value)
{
    sn_bfsk_rx_push(&rx,&value,1,cursor++,receive,NULL);
}
static void send_frame(void)
{
    sn_bfsk_tx_start(&tx,0x1234,expected,sizeof(expected));
    while(sn_bfsk_tx_busy(&tx)){
        float value=(float)sn_bfsk_tx_sample(&tx)*3.3f/4095;
        push(value);
    }
}
int main(void)
{
    unsigned offset,i;
    for(i=0;i<sizeof(expected);++i)expected[i]=(uint8_t)(0x5a^i);
    CHECK(sn_bfsk_tx_start(NULL,0,NULL,0)==-1);
    CHECK(sn_bfsk_tx_start(&tx,0,expected,33)==-1);
    /* 任意初始0..79样点偏移均需要从前导和同步字恢复，非已知边界判决。 */
    for(offset=0;offset<80;++offset){
        sn_bfsk_rx_init(&rx);cursor=0;got=bad=0;
        for(i=0;i<offset;++i)push(1.65f);
        send_frame();
        for(i=0;i<160;++i)push(1.65f);
        CHECK(got==1 && bad==0 && rx.frames_ok==1);
    }
    sn_bfsk_rx_init(&rx);cursor=0;got=bad=0;
    sn_bfsk_tx_start(&tx,0x1234,expected,sizeof(expected));
    tx.bytes[16]^=0x80; /* 不重算CRC，生成能够完整判决但校验失败的一帧。 */
    while(sn_bfsk_tx_busy(&tx))push((float)sn_bfsk_tx_sample(&tx)*3.3f/4095);
    for(i=0;i<800;++i)push(1.65f);
    CHECK(got==0 && bad==1 && rx.crc_errors==1);
    send_frame();for(i=0;i<160;++i)push(1.65f);
    CHECK(got==1); /* 坏帧后重新同步。 */
    cursor+=512;push(1.65f);CHECK(rx.discontinuities==1);
    puts("PASS BFSK: all 80 initial offsets, CRC rejection, reacquisition, sample gap");
    return 0;
}
