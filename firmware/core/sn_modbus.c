#include "sn_modbus.h"
#include "sn_wire.h"
#include <string.h>
static uint16_t be16(const uint8_t *p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static void put16(uint8_t *p,uint16_t x){p[0]=(uint8_t)(x>>8);p[1]=(uint8_t)x;}
static size_t finish(uint8_t *p,size_t n){uint16_t crc=sn_crc16(p,n);p[n]=(uint8_t)crc;p[n+1]=(uint8_t)(crc>>8);return n+2;}
size_t sn_mb_handle(uint8_t address,const uint8_t *r,size_t n,uint8_t *out,size_t cap,
                    sn_mb_read_fn read,sn_mb_write_fn write,void *ctx)
{
    uint16_t offset,count,words[125];uint8_t error=0,function;size_t i,size=0;
    if(!r || !out || n<4 || n>256 || cap<5 || address==0 || address>247)return 0;
    if(r[0]!=0 && r[0]!=address)return 0;
    if(sn_crc16(r,n-2)!=(uint16_t)(r[n-2]|((uint16_t)r[n-1]<<8)))return 0;
    function=r[1];
    if(function==4u){
        if(r[0]==0)return 0; /* 广播不执行读、不发回复。 */
        if(n!=8)error=3;
        else{
            offset=be16(r+2);count=be16(r+4);
            if(count==0 || count>125 || (uint32_t)offset+count>65536u)error=3;
            else if(cap<5u+2u*count)return 0;
            else if(!read)error=1;
            else if((error=read(ctx,offset,count,words))==0){
                out[2]=(uint8_t)(2u*count);
                for(i=0;i<count;++i)put16(out+3+2*i,words[i]);
                size=3u+2u*count;
            }
        }
    }else if(function==6u){
        if(r[0]!=0 && cap<8)return 0;
        if(n!=8)error=3;
        else if(!write)error=1;
        else{
            offset=be16(r+2);words[0]=be16(r+4);error=write(ctx,offset,1,words);
            if(!error){if(cap<8)return 0;memcpy(out,r,6);size=6;}
        }
    }else if(function==16u){
        if(r[0]!=0 && cap<8)return 0;
        if(n<9)error=3;
        else{
            offset=be16(r+2);count=be16(r+4);
            if(count==0 || count>123 || r[6]!=2u*count || n!=9u+2u*count ||
               (uint32_t)offset+count>65536u)error=3;
            else if(!write)error=1;
            else{
                for(i=0;i<count;++i)words[i]=be16(r+7+2*i);
                error=write(ctx,offset,count,words);
                if(!error){if(cap<8)return 0;memcpy(out,r,6);size=6;}
            }
        }
    }else error=1;
    if(r[0]==0)return 0;
    out[0]=address;out[1]=function;
    if(error){out[1]|=0x80;out[2]=error;size=3;}
    return finish(out,size);
}
