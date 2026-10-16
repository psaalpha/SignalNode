#ifndef SN_MODBUS_H
#define SN_MODBUS_H
#include <stddef.h>
#include <stdint.h>
/* callbacks返回0成功，或Modbus异常码2/3/6。多寄存器写必须先全部校验再提交。 */
typedef uint8_t (*sn_mb_read_fn)(void *,uint16_t,uint16_t,uint16_t *);
typedef uint8_t (*sn_mb_write_fn)(void *,uint16_t,uint16_t,const uint16_t *);
size_t sn_mb_handle(uint8_t address,const uint8_t *request,size_t length,
                    uint8_t *response,size_t capacity,sn_mb_read_fn read,
                    sn_mb_write_fn write,void *context);
#endif
