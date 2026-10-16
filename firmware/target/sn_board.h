#ifndef SN_BOARD_H
#define SN_BOARD_H
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
void sn_board_init(void);
void sn_board_start(void);
void sn_board_raw_send(const uint8_t *data,uint16_t length);
void sn_board_rtu_send(const uint8_t *data,uint16_t length);
uint32_t sn_board_cycles(void);
void sn_board_watchdog_start(void);
void sn_board_watchdog_feed(void);
void sn_board_fault(void);
int sn_board_adc_half_valid(const uint16_t *samples);
extern TaskHandle_t sn_raw_task_handle,sn_comm_task_handle;
extern volatile uint32_t sn_adc_errors,sn_dac_errors;
void sn_app_adc_isr(const uint16_t *samples);
void sn_app_rtu_isr(const uint8_t *data,uint16_t length);
void sn_app_dac_fill_isr(uint16_t *samples,uint16_t count);
#endif
