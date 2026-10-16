#include "sn_board.h"
#include "sn_dsp.h"
#include <string.h>

/* 默认F407VGT6、8MHz外部晶振。具体板子必须按原理图核对。
 * PA0=ADC1_IN0；PA4=DAC1；PA9=USART1_TX；PA2/PA3=USART2；PB1=RS485 DE+/RE。
 * DMA缓冲均在普通SRAM，链接脚本不把.bss放进CCM。 */
static uint16_t adc_dma[SN_N];
static uint16_t dac_dma[160];
static uint8_t rtu_bytes[256];
static uint16_t rtu_count;
static uint32_t rtu_last_us;
static uint8_t rtu_invalid,rtu_transmitting;
volatile uint32_t sn_adc_errors,sn_dac_errors;

void sn_board_fault(void){__disable_irq();for(;;){}}
uint32_t sn_board_cycles(void){return DWT->CYCCNT;}
int sn_board_adc_half_valid(const uint16_t *samples)
{
    uint16_t remaining=DMA_GetCurrDataCounter(DMA2_Stream0);
    return samples==adc_dma ? (remaining>0 && remaining<=SN_HOP) : remaining>SN_HOP;
}

static void irq_config(uint8_t channel,uint8_t priority)
{
    NVIC_InitTypeDef nvic;
    nvic.NVIC_IRQChannel=channel;nvic.NVIC_IRQChannelPreemptionPriority=priority;
    nvic.NVIC_IRQChannelSubPriority=0;nvic.NVIC_IRQChannelCmd=ENABLE;
    NVIC_Init(&nvic);
}

static void clock_init(void)
{
    uint32_t timeout=2000000u;
    RCC_DeInit();RCC_HSEConfig(RCC_HSE_ON);
    if(RCC_WaitForHSEStartUp()!=SUCCESS)sn_board_fault();
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR,ENABLE);
    PWR_MainRegulatorModeConfig(PWR_Regulator_Voltage_Scale1);
    FLASH_SetLatency(FLASH_Latency_5);
    FLASH_PrefetchBufferCmd(ENABLE);FLASH_InstructionCacheCmd(ENABLE);FLASH_DataCacheCmd(ENABLE);
    RCC_HCLKConfig(RCC_SYSCLK_Div1);RCC_PCLK1Config(RCC_HCLK_Div4);RCC_PCLK2Config(RCC_HCLK_Div2);
    RCC_PLLConfig(RCC_PLLSource_HSE,8,336,2,7);RCC_PLLCmd(ENABLE);
    while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY)==RESET && --timeout){}
    if(!timeout)sn_board_fault();
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);timeout=2000000u;
    while(RCC_GetSYSCLKSource()!=0x08 && --timeout){}
    if(!timeout)sn_board_fault();
    SystemCoreClockUpdate();
    if(SystemCoreClock!=168000000u)sn_board_fault();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0;DWT->CTRL|=1u;
}

static void sample_init(void)
{
    GPIO_InitTypeDef gpio;DMA_InitTypeDef dma;ADC_InitTypeDef adc;
    ADC_CommonInitTypeDef common;TIM_TimeBaseInitTypeDef timer;DAC_InitTypeDef dac;
    unsigned i;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA|RCC_AHB1Periph_DMA1|RCC_AHB1Periph_DMA2,ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1,ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2|RCC_APB1Periph_TIM5|RCC_APB1Periph_TIM6|RCC_APB1Periph_DAC,ENABLE);
    GPIO_StructInit(&gpio);gpio.GPIO_Pin=GPIO_Pin_0|GPIO_Pin_4;gpio.GPIO_Mode=GPIO_Mode_AN;
    gpio.GPIO_PuPd=GPIO_PuPd_NOPULL;GPIO_Init(GPIOA,&gpio);
    TIM_TimeBaseStructInit(&timer);timer.TIM_Prescaler=83;timer.TIM_Period=124;
    TIM_TimeBaseInit(TIM2,&timer);TIM_SelectOutputTrigger(TIM2,TIM_TRGOSource_Update);
    TIM_TimeBaseInit(TIM6,&timer);TIM_SelectOutputTrigger(TIM6,TIM_TRGOSource_Update);
    /* APB1=42MHz但TIM2/5/6输入=84MHz。TIM5作为1MHz自由运行的RTU时基。 */
    timer.TIM_Period=0xffffffffu;TIM_TimeBaseInit(TIM5,&timer);
    ADC_CommonStructInit(&common);common.ADC_Prescaler=ADC_Prescaler_Div4;ADC_CommonInit(&common);
    ADC_StructInit(&adc);adc.ADC_ExternalTrigConv=ADC_ExternalTrigConv_T2_TRGO;
    adc.ADC_ExternalTrigConvEdge=ADC_ExternalTrigConvEdge_Rising;
    adc.ADC_ContinuousConvMode=DISABLE;ADC_Init(ADC1,&adc);
    ADC_RegularChannelConfig(ADC1,ADC_Channel_0,1,ADC_SampleTime_56Cycles);
    DMA_StructInit(&dma);dma.DMA_Channel=DMA_Channel_0;
    dma.DMA_PeripheralBaseAddr=(uint32_t)&ADC1->DR;dma.DMA_Memory0BaseAddr=(uint32_t)adc_dma;
    dma.DMA_DIR=DMA_DIR_PeripheralToMemory;dma.DMA_BufferSize=SN_N;
    dma.DMA_MemoryInc=DMA_MemoryInc_Enable;dma.DMA_PeripheralDataSize=DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize=DMA_MemoryDataSize_HalfWord;dma.DMA_Mode=DMA_Mode_Circular;
    dma.DMA_Priority=DMA_Priority_VeryHigh;DMA_Init(DMA2_Stream0,&dma);
    DMA_ITConfig(DMA2_Stream0,DMA_IT_HT|DMA_IT_TC|DMA_IT_TE,ENABLE);
    irq_config(DMA2_Stream0_IRQn,6);
    ADC_DMARequestAfterLastTransferCmd(ADC1,ENABLE);ADC_DMACmd(ADC1,ENABLE);
    ADC_ITConfig(ADC1,ADC_IT_OVR,ENABLE);irq_config(ADC_IRQn,6);
    DAC_StructInit(&dac);dac.DAC_Trigger=DAC_Trigger_T6_TRGO;DAC_Init(DAC_Channel_1,&dac);
    for(i=0;i<160;++i)dac_dma[i]=2048;
    DMA_StructInit(&dma);dma.DMA_Channel=DMA_Channel_7;
    dma.DMA_PeripheralBaseAddr=(uint32_t)&DAC->DHR12R1;dma.DMA_Memory0BaseAddr=(uint32_t)dac_dma;
    dma.DMA_DIR=DMA_DIR_MemoryToPeripheral;dma.DMA_BufferSize=160;
    dma.DMA_MemoryInc=DMA_MemoryInc_Enable;dma.DMA_PeripheralDataSize=DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize=DMA_MemoryDataSize_HalfWord;dma.DMA_Mode=DMA_Mode_Circular;
    dma.DMA_Priority=DMA_Priority_Medium;DMA_Init(DMA1_Stream5,&dma);
    DMA_ITConfig(DMA1_Stream5,DMA_IT_HT|DMA_IT_TC|DMA_IT_TE,ENABLE);irq_config(DMA1_Stream5_IRQn,7);
    DAC_DMACmd(DAC_Channel_1,ENABLE);
}

static void serial_init(void)
{
    GPIO_InitTypeDef gpio;USART_InitTypeDef serial;DMA_InitTypeDef dma;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB,ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE);RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2,ENABLE);
    GPIO_StructInit(&gpio);gpio.GPIO_Pin=GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_9;
    gpio.GPIO_Mode=GPIO_Mode_AF;gpio.GPIO_Speed=GPIO_Speed_50MHz;gpio.GPIO_PuPd=GPIO_PuPd_UP;
    GPIO_Init(GPIOA,&gpio);GPIO_PinAFConfig(GPIOA,GPIO_PinSource2,GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA,GPIO_PinSource3,GPIO_AF_USART2);GPIO_PinAFConfig(GPIOA,GPIO_PinSource9,GPIO_AF_USART1);
    gpio.GPIO_Pin=GPIO_Pin_1;gpio.GPIO_Mode=GPIO_Mode_OUT;gpio.GPIO_OType=GPIO_OType_PP;
    gpio.GPIO_PuPd=GPIO_PuPd_NOPULL;GPIO_Init(GPIOB,&gpio);GPIO_ResetBits(GPIOB,GPIO_Pin_1);
    USART_StructInit(&serial);serial.USART_BaudRate=921600;serial.USART_Mode=USART_Mode_Tx;USART_Init(USART1,&serial);
    serial.USART_BaudRate=19200;serial.USART_Mode=USART_Mode_Tx|USART_Mode_Rx;
    serial.USART_WordLength=USART_WordLength_9b;serial.USART_Parity=USART_Parity_Even;USART_Init(USART2,&serial);
    /* SPL的9bit字长含校验位，因此这里实际承载8bit数据+偶校验+1停止位。 */
    DMA_StructInit(&dma);dma.DMA_Channel=DMA_Channel_4;dma.DMA_DIR=DMA_DIR_MemoryToPeripheral;
    dma.DMA_MemoryInc=DMA_MemoryInc_Enable;dma.DMA_PeripheralBaseAddr=(uint32_t)&USART1->DR;
    dma.DMA_Priority=DMA_Priority_Low;DMA_Init(DMA2_Stream7,&dma);
    dma.DMA_PeripheralBaseAddr=(uint32_t)&USART2->DR;dma.DMA_Priority=DMA_Priority_High;
    DMA_Init(DMA1_Stream6,&dma);
    DMA_ITConfig(DMA2_Stream7,DMA_IT_TC|DMA_IT_TE,ENABLE);irq_config(DMA2_Stream7_IRQn,7);
    DMA_ITConfig(DMA1_Stream6,DMA_IT_TC|DMA_IT_TE,ENABLE);irq_config(DMA1_Stream6_IRQn,7);
    irq_config(USART2_IRQn,6);irq_config(TIM5_IRQn,6);
}

void sn_board_init(void){clock_init();sample_init();serial_init();}
void sn_board_start(void)
{
    /* 所有任务/队列已创建，且已经进入任务上下文后才开放会调用FromISR的IRQ。 */
    USART_Cmd(USART1,ENABLE);USART_Cmd(USART2,ENABLE);
    USART_ITConfig(USART2,USART_IT_RXNE,ENABLE);USART_ITConfig(USART2,USART_IT_PE,ENABLE);
    USART_ITConfig(USART2,USART_IT_ERR,ENABLE);TIM_Cmd(TIM5,ENABLE);
    DMA_Cmd(DMA2_Stream0,ENABLE);ADC_Cmd(ADC1,ENABLE);
    DMA_Cmd(DMA1_Stream5,ENABLE);DAC_Cmd(DAC_Channel_1,ENABLE);
    TIM_Cmd(TIM6,ENABLE);TIM_Cmd(TIM2,ENABLE);
}

static void dma_tx(DMA_Stream_TypeDef *stream,USART_TypeDef *uart,const uint8_t *data,uint16_t length)
{
    configASSERT(DMA_GetCmdStatus(stream)==DISABLE);
    stream->M0AR=(uint32_t)data;DMA_SetCurrDataCounter(stream,length);
    USART_DMACmd(uart,USART_DMAReq_Tx,ENABLE);DMA_Cmd(stream,ENABLE);
}
void sn_board_raw_send(const uint8_t *data,uint16_t length)
{dma_tx(DMA2_Stream7,USART1,data,length);}
void sn_board_rtu_send(const uint8_t *data,uint16_t length)
{
    rtu_transmitting=1;GPIO_SetBits(GPIOB,GPIO_Pin_1);
    USART_ClearFlag(USART2,USART_FLAG_TC);dma_tx(DMA1_Stream6,USART2,data,length);
}

void DMA2_Stream0_IRQHandler(void)
{
    uint32_t flags=DMA2->LISR;
    if((flags&DMA_LISR_TEIF0) || ((flags&DMA_LISR_HTIF0)&&(flags&DMA_LISR_TCIF0))){
        /* 两半同时积压时不能证明哪份数据仍有效：停止采样并让监测任务看到故障。
         * 不把已被DMA改写的数据当成连续的正常块发布。 */
        ++sn_adc_errors;TIM_Cmd(TIM2,DISABLE);DMA_ClearITPendingBit(DMA2_Stream0,DMA_IT_TEIF0|DMA_IT_HTIF0|DMA_IT_TCIF0);return;
    }
    if(flags&DMA_LISR_HTIF0){DMA_ClearITPendingBit(DMA2_Stream0,DMA_IT_HTIF0);sn_app_adc_isr(adc_dma);}
    if(flags&DMA_LISR_TCIF0){DMA_ClearITPendingBit(DMA2_Stream0,DMA_IT_TCIF0);sn_app_adc_isr(adc_dma+SN_HOP);}
}
void ADC_IRQHandler(void)
{
    if(ADC_GetITStatus(ADC1,ADC_IT_OVR)!=RESET){
        ++sn_adc_errors;TIM_Cmd(TIM2,DISABLE);ADC_ClearITPendingBit(ADC1,ADC_IT_OVR);
    }
}
void DMA1_Stream5_IRQHandler(void)
{
    uint32_t flags=DMA1->HISR;
    if((flags&DMA_HISR_TEIF5) || ((flags&DMA_HISR_HTIF5)&&(flags&DMA_HISR_TCIF5))){
        ++sn_dac_errors;TIM_Cmd(TIM6,DISABLE);DMA_ClearITPendingBit(DMA1_Stream5,DMA_IT_TEIF5|DMA_IT_HTIF5|DMA_IT_TCIF5);return;
    }
    if(flags&DMA_HISR_HTIF5){DMA_ClearITPendingBit(DMA1_Stream5,DMA_IT_HTIF5);sn_app_dac_fill_isr(dac_dma,80);}
    if(flags&DMA_HISR_TCIF5){DMA_ClearITPendingBit(DMA1_Stream5,DMA_IT_TCIF5);sn_app_dac_fill_isr(dac_dma+80,80);}
}
static void rtu_complete(void)
{
    if(rtu_count && !rtu_invalid)sn_app_rtu_isr(rtu_bytes,rtu_count);
    rtu_count=0;rtu_invalid=0;
}
void USART2_IRQHandler(void)
{
    BaseType_t wake=pdFALSE;uint32_t sr=USART2->SR;
    if(sr&(USART_SR_RXNE|USART_SR_ORE|USART_SR_FE|USART_SR_NE|USART_SR_PE)){
        uint8_t byte=(uint8_t)USART2->DR;uint32_t now=TIM_GetCounter(TIM5);
        if(!rtu_transmitting){
            uint32_t gap=now-rtu_last_us;
            /* RXNE记录的是字节结束，间隔还含新字节自身573us。
             * t1.5判断加一个字符；帧结束采用t3.5+一个字符的保守等待，避免在新字节中途切帧。 */
            if(rtu_count && gap>=2579u)rtu_complete();
            else if(rtu_count && gap>1433u)rtu_invalid=1;
            if(sr&(USART_SR_ORE|USART_SR_FE|USART_SR_NE|USART_SR_PE))rtu_invalid=1;
            if(rtu_count<sizeof(rtu_bytes))rtu_bytes[rtu_count++]=byte;else rtu_invalid=1;
            rtu_last_us=now;TIM_ClearITPendingBit(TIM5,TIM_IT_CC1);
            TIM_SetCompare1(TIM5,now+2579u);TIM_ITConfig(TIM5,TIM_IT_CC1,ENABLE);
        }
    }
    if((sr&USART_SR_TC) && (USART2->CR1&USART_CR1_TCIE)){
        USART_ITConfig(USART2,USART_IT_TC,DISABLE);GPIO_ResetBits(GPIOB,GPIO_Pin_1);rtu_transmitting=0;
        vTaskNotifyGiveFromISR(sn_comm_task_handle,&wake);
    }
    portYIELD_FROM_ISR(wake);
}
void TIM5_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM5,TIM_IT_CC1)!=RESET){
        TIM_ClearITPendingBit(TIM5,TIM_IT_CC1);TIM_ITConfig(TIM5,TIM_IT_CC1,DISABLE);rtu_complete();
    }
}
void DMA2_Stream7_IRQHandler(void)
{
    BaseType_t wake=pdFALSE;
    if(DMA_GetITStatus(DMA2_Stream7,DMA_IT_TEIF7)!=RESET)sn_board_fault();
    DMA_ClearITPendingBit(DMA2_Stream7,DMA_IT_TCIF7|DMA_IT_TEIF7);
    USART_DMACmd(USART1,USART_DMAReq_Tx,DISABLE);DMA_Cmd(DMA2_Stream7,DISABLE);
    vTaskNotifyGiveFromISR(sn_raw_task_handle,&wake);portYIELD_FROM_ISR(wake);
}
void DMA1_Stream6_IRQHandler(void)
{
    if(DMA_GetITStatus(DMA1_Stream6,DMA_IT_TEIF6)!=RESET)sn_board_fault();
    DMA_ClearITPendingBit(DMA1_Stream6,DMA_IT_TCIF6|DMA_IT_TEIF6);
    USART_DMACmd(USART2,USART_DMAReq_Tx,DISABLE);DMA_Cmd(DMA1_Stream6,DISABLE);
    /* DMA完成只表示最后数据已写到USART；等移位寄存器真正TC后再释放DE。 */
    USART_ITConfig(USART2,USART_IT_TC,ENABLE);
}
void sn_board_watchdog_start(void)
{IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);IWDG_SetPrescaler(IWDG_Prescaler_64);IWDG_SetReload(2500);IWDG_ReloadCounter();IWDG_Enable();}
void sn_board_watchdog_feed(void){IWDG_ReloadCounter();}
