#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include "stm32f4xx.h"
extern uint32_t SystemCoreClock;
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configCPU_CLOCK_HZ SystemCoreClock
#define configTICK_RATE_HZ 1000u
#define configMAX_PRIORITIES 6
#define configMINIMAL_STACK_SIZE 256u
#define configMAX_TASK_NAME_LEN 12
#define configUSE_16_BIT_TICKS 0
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configUSE_MUTEXES 0
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_TASK_NOTIFICATIONS 1
#define configUSE_TIMERS 0
#define configUSE_TRACE_FACILITY 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 0
#define configIDLE_SHOULD_YIELD 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_vTaskSuspend 1
#define configPRIO_BITS 4
/* 任务优先级越大越高；NVIC数值越小越紧迫，二者不能混用。
 * 本工程所有调用FromISR的外设IRQ均为6或7，处于允许调用内核的范围。 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
/* RVDS端口把该值放进汇编立即数，使用无u后缀的常量。 */
#define configKERNEL_INTERRUPT_PRIORITY 240
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 80
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#define configASSERT(x) do{if(!(x)){__disable_irq();for(;;){}}}while(0)
#endif
