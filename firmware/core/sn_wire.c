#include "sn_wire.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t x) { p[0]=(uint8_t)x; p[1]=(uint8_t)(x>>8); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
static void put32(uint8_t *p, uint32_t x) { unsigned i; for(i=0;i<4;++i) p[i]=(uint8_t)(x>>(8*i)); }
static uint32_t get32(const uint8_t *p) { unsigned i; uint32_t x=0; for(i=0;i<4;++i)x|=(uint32_t)p[i]<<(8*i);return x; }
static void put64(uint8_t *p, uint64_t x) { unsigned i; for(i=0;i<8;++i)p[i]=(uint8_t)(x>>(8*i)); }
static uint64_t get64(const uint8_t *p) { unsigned i;uint64_t x=0;for(i=0;i<8;++i)x|=(uint64_t)p[i]<<(8*i);return x; }

uint16_t sn_crc16(const uint8_t *p, size_t count)
{
    uint16_t crc=0xffffu;
    size_t i; unsigned bit;
    for(i=0;i<count;++i) {
        crc^=p[i];
        for(bit=0;bit<8;++bit) crc=(uint16_t)((crc>>1)^((crc&1u)?0xa001u:0u));
    }
    return crc;
}

size_t sn_wire_encode(uint8_t *out, size_t cap, uint8_t type, uint32_t seq,
                      uint64_t first, uint16_t fs, const uint8_t *payload, uint16_t len)
{
    size_t size=SN_WIRE_HEADER+(size_t)len+2u;
    if(!out || len>SN_WIRE_MAX_PAYLOAD || cap<size || (!payload && len) || !fs) return 0;
    out[0]=0x53;out[1]=0x4e;out[2]=1;out[3]=type;
    put16(out+4,len);put32(out+6,seq);put64(out+10,first);put16(out+18,fs);
    if(len) memcpy(out+SN_WIRE_HEADER,payload,len);
    put16(out+size-2,sn_crc16(out,size-2));
    return size;
}

static void discard(sn_wire_parser_t *p, size_t n)
{
    memmove(p->buffer,p->buffer+n,p->used-n); p->used-=n;
}

static void drain(sn_wire_parser_t *p, sn_wire_callback cb, void *ctx)
{
    while(p->used>=2u) {
        uint16_t len;size_t total;
        if(p->buffer[0]!=0x53 || p->buffer[1]!=0x4e) {
            ++p->discarded_bytes;discard(p,1);continue;
        }
        if(p->used<SN_WIRE_HEADER) return;
        len=get16(p->buffer+4);
        if(p->buffer[2]!=1 || len>SN_WIRE_MAX_PAYLOAD || get16(p->buffer+18)==0) {
            ++p->discarded_bytes;discard(p,1);continue;
        }
        total=SN_WIRE_HEADER+(size_t)len+2u;
        if(p->used<total) return;
        if(get16(p->buffer+total-2)!=sn_crc16(p->buffer,total-2)) {
            ++p->crc_errors;++p->discarded_bytes;discard(p,1);continue;
        }
        ++p->good_frames;
        /* payload指针只在回调期间有效。需要异步发送/存储时必须复制或转移所有权。
         * 回调不得递归调用同一个解析器。 */
        if(cb) cb(ctx,p->buffer[3],get32(p->buffer+6),get64(p->buffer+10),
                  get16(p->buffer+18),p->buffer+SN_WIRE_HEADER,len);
        discard(p,total);
    }
}

void sn_wire_push(sn_wire_parser_t *p,const uint8_t *bytes,size_t n,
                   sn_wire_callback cb,void *ctx)
{
    size_t i;
    if(!p || (!bytes && n))return;
    for(i=0;i<n;++i) {
        if(p->used==sizeof(p->buffer)){++p->discarded_bytes;discard(p,1);}
        p->buffer[p->used++]=bytes[i];
        drain(p,cb,ctx);
    }
}

void sn_wire_timeout(sn_wire_parser_t *p)
{
    if(p && p->used){p->discarded_bytes+=(uint32_t)p->used;p->used=0;++p->timeouts;}
}
