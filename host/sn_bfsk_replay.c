#include "sn_bfsk.h"
#include <stdio.h>
#include <string.h>
static sn_bfsk_rx_t rx;
static void frame(void *ctx,const sn_bfsk_frame_t *f)
{
    unsigned i;(void)ctx;
    printf("%u,%u,%u,%llu,",f->sequence,f->length,f->crc_ok,(unsigned long long)f->sync_end_sample);
    for(i=0;i<f->length;++i)printf("%02x",f->payload[i]);
    putchar('\n');
}
int main(int argc,char **argv)
{
    float samples[512];size_t count=0;uint64_t first=0;
    sn_bfsk_rx_init(&rx);
    if(argc>1 && strcmp(argv[1],"symbols")==0){
        while(scanf("%f",&samples[count])==1){
            if(++count==SN_BFSK_SPS){printf("%d\n",sn_bfsk_decide(samples,NULL,NULL));count=0;}
        }
        return count?1:0;
    }
    puts("sequence,length,crc_ok,sync_end_sample,payload_hex");
    while(scanf("%f",&samples[count])==1){
        if(++count==512){sn_bfsk_rx_push(&rx,samples,count,first,frame,NULL);first+=count;count=0;}
    }
    if(count)sn_bfsk_rx_push(&rx,samples,count,first,frame,NULL);
    fprintf(stderr,"syncs=%u ok=%u crc_bad=%u len_bad=%u lost=%u\n",rx.syncs,rx.frames_ok,rx.crc_errors,rx.length_errors,rx.lost_sync);
    return 0;
}
