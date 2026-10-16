#include "stm32f4xx.h"
uint32_t SystemCoreClock=16000000u;
/* 启动文件在C运行库初始化前调用SystemInit，只设置内核必需状态。
 * 实际8MHz HSE ->168MHz的SPL配置在main调用sn_board_init中执行。 */
void SystemInit(void)
{
    SCB->CPACR|=(0xful<<20);
    __DSB();__ISB();
    SCB->VTOR=FLASH_BASE;
}
#if defined(__GNUC__)
void _init(void){}
void _fini(void){}
#endif
void SystemCoreClockUpdate(void)
{
    RCC_ClocksTypeDef clocks;
    RCC_GetClocksFreq(&clocks);
    SystemCoreClock=clocks.HCLK_Frequency;
}
