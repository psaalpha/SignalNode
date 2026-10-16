#include "sn_modbus.h"
#include "sn_wire.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint16_t value=1000;static unsigned writes;
static uint8_t read_registers(void *ctx,uint16_t first,uint16_t count,uint16_t *out)
{unsigned i;(void)ctx;if((unsigned)first+count>28)return 2;for(i=0;i<count;++i)out[i]=(uint16_t)(first+i);return 0;}
static uint8_t write_registers(void *ctx,uint16_t first,uint16_t count,const uint16_t *in)
{(void)ctx;if(first!=0 || count!=1)return 2;if(in[0]>2000)return 3;value=in[0];++writes;return 0;}
static size_t crc(uint8_t *p,size_t n){uint16_t c=sn_crc16(p,n);p[n]=(uint8_t)c;p[n+1]=(uint8_t)(c>>8);return n+2;}
int main(void)
{
    uint8_t req[256]={17,4,0,0,0,28},out[256];size_t n,m;
    n=crc(req,6);m=sn_mb_handle(17,req,n,out,sizeof(out),read_registers,write_registers,NULL);
    CHECK(m==61 && out[2]==56 && out[58]==27 && sn_crc16(out,m)==0);
    req[4]=req[5]=0;n=crc(req,6);m=sn_mb_handle(17,req,n,out,sizeof(out),read_registers,write_registers,NULL);
    CHECK(m==5 && out[1]==0x84 && out[2]==3);
    req[1]=6;req[4]=3;req[5]=232;n=crc(req,6);
    CHECK(sn_mb_handle(17,req,n,out,5,read_registers,write_registers,NULL)==0 && writes==0);
    CHECK(sn_mb_handle(17,req,n,out,sizeof(out),read_registers,write_registers,NULL)==8 && value==1000);
    req[0]=0;n=crc(req,6);CHECK(sn_mb_handle(17,req,n,out,sizeof(out),read_registers,write_registers,NULL)==0 && writes==2);
    req[0]=17;req[1]=16;req[4]=0;req[5]=1;req[6]=2;req[7]=7;req[8]=209;n=crc(req,9);
    m=sn_mb_handle(17,req,n,out,sizeof(out),read_registers,write_registers,NULL);
    CHECK(m==5 && out[2]==3 && value==1000);
    req[n-1]^=1;CHECK(sn_mb_handle(17,req,n,out,sizeof(out),read_registers,write_registers,NULL)==0 && writes==2);
    puts("PASS Modbus: read, invalid range, write, broadcast, atomic rejection, CRC");return 0;
}
