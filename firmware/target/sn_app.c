#include "sn_board.h"
#include "sn_dsp.h"
#include "sn_bfsk.h"
#include "sn_wire.h"
#include "sn_modbus.h"
#include "sn_cmsis_fft.h"
#include "queue.h"
#include <string.h>

#ifndef SN_ENABLE_SPECTRUM
#define SN_ENABLE_SPECTRUM 1
#endif
#ifndef SN_AUTO_BFSK_TX
#define SN_AUTO_BFSK_TX 1
#endif

/* 四个采样块只通过空闲/待处理队列交接，运行期不malloc。
 * 采样池与导出池分开：电脑慢只能丢导出副本，不能长期扣住采样块。 */
typedef struct {uint64_t first;uint16_t samples[SN_HOP];} sample_block_t;
typedef struct {uint16_t length;uint8_t bytes[SN_WIRE_MAX_FRAME];} raw_slot_t;
typedef struct {uint16_t length;uint8_t bytes[256];} rtu_frame_t;
static sample_block_t blocks[4];static raw_slot_t raw_slots[2];
static StaticQueue_t free_qcb,ready_qcb,raw_free_qcb,raw_ready_qcb,rtu_qcb;
static uint8_t free_qmem[4*sizeof(sample_block_t *)],ready_qmem[4*sizeof(sample_block_t *)];
static uint8_t raw_free_qmem[2*sizeof(raw_slot_t *)],raw_ready_qmem[2*sizeof(raw_slot_t *)];
static uint8_t rtu_qmem[2*sizeof(rtu_frame_t)];
static QueueHandle_t free_q,ready_q,raw_free_q,raw_ready_q,rtu_q;
static StaticTask_t dsp_tcb,comm_tcb,raw_tcb,monitor_tcb,idle_tcb;
static StackType_t dsp_stack[2048],comm_stack[1024],raw_stack[768],monitor_stack[512],idle_stack[256];
TaskHandle_t sn_raw_task_handle,sn_comm_task_handle;
static TaskHandle_t dsp_handle,monitor_handle;
static volatile uint32_t capture_blocks,capture_drops,processed_blocks,export_drops,rtu_drops;
static uint32_t max_dsp_cycles,max_adc_isr_cycles;
static uint64_t next_sample;
static sn_bfsk_tx_t bfsk_tx;
static sn_bfsk_rx_t bfsk_rx;
#if SN_ENABLE_SPECTRUM
static sn_dsp_t dsp;
static sn_cmsis_fft_t fft;
#endif
static sn_features_t published;
static uint16_t requested_cutoff,requested_hann=1,configuration_generation;
static uint16_t applied_generation;
static uint16_t tx_sequence;
static uint32_t last_bfsk_sequence,bfsk_payload_bit_errors,bfsk_payload_bits;
static uint8_t result_valid;
volatile uint32_t sn_stack_watermarks[4];

void sn_app_adc_isr(const uint16_t *samples)
{
    BaseType_t wake=pdFALSE;sample_block_t *block;uint32_t begin=sn_board_cycles();
    uint64_t first=next_sample;next_sample+=SN_HOP;++capture_blocks;
    if(xQueueReceiveFromISR(free_q,&block,&wake)==pdPASS){
        block->first=first;memcpy(block->samples,samples,sizeof(block->samples));
        if(!sn_board_adc_half_valid(samples)){
            ++capture_drops;++sn_adc_errors;
            configASSERT(xQueueSendFromISR(free_q,&block,&wake)==pdPASS);
        }else configASSERT(xQueueSendFromISR(ready_q,&block,&wake)==pdPASS);
    }else ++capture_drops;
    begin=sn_board_cycles()-begin;if(begin>max_adc_isr_cycles)max_adc_isr_cycles=begin;
    portYIELD_FROM_ISR(wake);
}
void sn_app_rtu_isr(const uint8_t *data,uint16_t length)
{
    BaseType_t wake=pdFALSE;rtu_frame_t frame;
    if(!data || length>sizeof(frame.bytes)){++rtu_drops;return;}
    memset(&frame,0,sizeof(frame));
    frame.length=length;memcpy(frame.bytes,data,length);
    if(xQueueSendFromISR(rtu_q,&frame,&wake)!=pdPASS)++rtu_drops;
    portYIELD_FROM_ISR(wake);
}
void sn_app_dac_fill_isr(uint16_t *samples,uint16_t count)
{uint16_t i;for(i=0;i<count;++i)samples[i]=sn_bfsk_tx_sample(&bfsk_tx);}

static uint8_t test_payload(unsigned index){return (uint8_t)(0xa5u^(index*17u));}
static int request_transmit(void)
{
    sn_bfsk_tx_t prepared;uint8_t payload[16];unsigned i;int result=0;
    for(i=0;i<sizeof(payload);++i)payload[i]=test_payload(i);
    sn_bfsk_tx_start(&prepared,tx_sequence,payload,sizeof(payload));
    taskENTER_CRITICAL();
    if(sn_bfsk_tx_busy(&bfsk_tx))result=-1;
    else{bfsk_tx=prepared;++tx_sequence;}
    taskEXIT_CRITICAL();return result;
}
static void received_bfsk(void *ctx,const sn_bfsk_frame_t *frame)
{
    unsigned i,b;(void)ctx;
    /* 已知测试payload才能统计位错误，任意业务数据不能猜测“正确答案”。
     * CRC失败但完整组装的帧也参与诊断统计；漏同步/非法长度由其他计数器体现。 */
    last_bfsk_sequence=frame->sequence;
    if(frame->length==16u){
        for(i=0;i<16;++i){uint8_t delta=(uint8_t)(frame->payload[i]^test_payload(i));
            for(b=0;b<8;++b)bfsk_payload_bit_errors+=(delta>>b)&1u;
        }
        bfsk_payload_bits+=128;
    }
}

static void export_raw(const sample_block_t *block)
{
    static uint32_t sequence;raw_slot_t *slot;uint8_t payload[1024];unsigned i;
    uint32_t frame_sequence=sequence++;
    if(xQueueReceive(raw_free_q,&slot,0)!=pdPASS){++export_drops;return;}
    for(i=0;i<SN_HOP;++i){payload[2*i]=(uint8_t)block->samples[i];payload[2*i+1]=(uint8_t)(block->samples[i]>>8);}
    slot->length=(uint16_t)sn_wire_encode(slot->bytes,sizeof(slot->bytes),1,frame_sequence,
                                         block->first,SN_FS,payload,sizeof(payload));
    configASSERT(xQueueSend(raw_ready_q,&slot,0)==pdPASS);
}

static void dsp_task(void *argument)
{
    sample_block_t *block;static float volts[SN_HOP];unsigned i;
#if SN_ENABLE_SPECTRUM
    sn_config_t cfg={3.3f,0.0f,1};uint8_t warm_outputs=0;
    configASSERT(sn_cmsis_fft_init(&fft)==0);
    configASSERT(sn_dsp_init(&dsp,&cfg,sn_cmsis_fft,&fft)==0);
#endif
    (void)argument;sn_bfsk_rx_init(&bfsk_rx);sn_board_start();
    for(;;){
        uint32_t begin,elapsed;
        configASSERT(xQueueReceive(ready_q,&block,portMAX_DELAY)==pdPASS);begin=sn_board_cycles();
        for(i=0;i<SN_HOP;++i)volts[i]=(float)block->samples[i]*3.3f/4095.0f;
        /* BFSK始终看原始输入，不让用户为频谱选择的1kHz低通误伤1500Hz音。 */
        sn_bfsk_rx_push(&bfsk_rx,volts,SN_HOP,block->first,received_bfsk,NULL);
#if SN_ENABLE_SPECTRUM
        if(applied_generation!=configuration_generation){
            taskENTER_CRITICAL();cfg.lowpass_hz=requested_cutoff;cfg.hann=(uint8_t)requested_hann;
            applied_generation=configuration_generation;taskEXIT_CRITICAL();
            configASSERT(sn_dsp_init(&dsp,&cfg,sn_cmsis_fft,&fft)==0);
            warm_outputs=(uint8_t)(cfg.lowpass_hz>0.0f);result_valid=0;
        }
        {sn_features_t features;uint32_t old_gaps=dsp.discontinuities;
            int status=sn_dsp_push_adc(&dsp,block->samples,block->first,&features);
            configASSERT(status>=0);
            if(dsp.discontinuities!=old_gaps){warm_outputs=(uint8_t)(cfg.lowpass_hz>0.0f);result_valid=0;}
            if(status==1){
                taskENTER_CRITICAL();published=features;
                result_valid=(uint8_t)(warm_outputs==0);taskEXIT_CRITICAL();
                if(warm_outputs)--warm_outputs;
            }
        }
#endif
        export_raw(block);configASSERT(xQueueSend(free_q,&block,0)==pdPASS);
        elapsed=sn_board_cycles()-begin;if(elapsed>max_dsp_cycles)max_dsp_cycles=elapsed;
        ++processed_blocks;
    }
}

static void word32(uint16_t *r,unsigned offset,uint32_t value)
{r[offset]=(uint16_t)(value>>16);r[offset+1]=(uint16_t)value;}
static uint8_t read_registers(void *ctx,uint16_t first,uint16_t count,uint16_t *out)
{
    uint16_t registers[52];sn_features_t f;unsigned i;(void)ctx;
    if((uint32_t)first+count>52u)return 2;
    memset(registers,0,sizeof(registers));
    taskENTER_CRITICAL();f=published;
    registers[0]=1;registers[1]=SN_FS;registers[2]=SN_N;registers[3]=SN_HOP;
    registers[4]=(uint16_t)(result_valid|(SN_ENABLE_SPECTRUM?0x0100u:0u));registers[5]=applied_generation;
    registers[8]=(uint16_t)(f.first_sample>>48);registers[9]=(uint16_t)(f.first_sample>>32);
    registers[10]=(uint16_t)(f.first_sample>>16);registers[11]=(uint16_t)f.first_sample;
    word32(registers,6,processed_blocks);word32(registers,12,(uint32_t)(f.mean_v*1000.0f));
    word32(registers,14,(uint32_t)(f.raw_ac_rms_v*1000.0f));
    word32(registers,16,(uint32_t)(f.filtered_ac_rms_v*1000.0f));
    word32(registers,18,(uint32_t)(f.peak_hz*1000.0f));word32(registers,20,f.rail_samples);
    word32(registers,22,capture_drops);word32(registers,24,export_drops);
    word32(registers,26,max_dsp_cycles/(SystemCoreClock/1000000u));
    word32(registers,28,bfsk_rx.syncs);word32(registers,30,bfsk_rx.frames_ok);
    word32(registers,32,bfsk_rx.crc_errors);word32(registers,34,bfsk_rx.length_errors);
    word32(registers,36,bfsk_payload_bit_errors);word32(registers,38,bfsk_payload_bits);
    word32(registers,40,last_bfsk_sequence);word32(registers,42,sn_adc_errors);
    word32(registers,44,max_adc_isr_cycles/(SystemCoreClock/1000000u));word32(registers,46,rtu_drops);
    word32(registers,48,tx_sequence);word32(registers,50,sn_dac_errors);
    taskEXIT_CRITICAL();
    for(i=0;i<count;++i)out[i]=registers[first+i];
    return 0;
}
static uint8_t write_registers(void *ctx,uint16_t first,uint16_t count,const uint16_t *values)
{
    uint16_t cutoff=requested_cutoff,hann=requested_hann,send=0;unsigned i;(void)ctx;
    if((uint32_t)first+count>3u)return 2;
    for(i=0;i<count;++i){
        switch(first+i){
        case 0:if(values[i] && (values[i]<100 || values[i]>2000))return 3;cutoff=values[i];break;
        case 1:if(values[i]>1)return 3;hann=values[i];break;
        case 2:if(values[i]>1)return 3;send=values[i];break;
        default:return 2;
        }
    }
    /* 先验证所有值，再执行一次提交，避免多寄存器请求半成功。 */
    if(send && request_transmit()!=0)return 6;
    taskENTER_CRITICAL();
    if(cutoff!=requested_cutoff || hann!=requested_hann){
        requested_cutoff=cutoff;requested_hann=hann;++configuration_generation;
    }
    taskEXIT_CRITICAL();return 0;
}
static void comm_task(void *argument)
{
    static rtu_frame_t request;static uint8_t response[256];(void)argument;
    for(;;){
        size_t length;configASSERT(xQueueReceive(rtu_q,&request,portMAX_DELAY)==pdPASS);
        length=sn_mb_handle(17,request.bytes,request.length,response,sizeof(response),read_registers,write_registers,NULL);
        if(length){
            (void)ulTaskNotifyTake(pdTRUE,0);sn_board_rtu_send(response,(uint16_t)length);
            configASSERT(ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(300))!=0);
        }
    }
}
static void raw_task(void *argument)
{
    raw_slot_t *slot;(void)argument;
    for(;;){
        configASSERT(xQueueReceive(raw_ready_q,&slot,portMAX_DELAY)==pdPASS);
        (void)ulTaskNotifyTake(pdTRUE,0);sn_board_raw_send(slot->bytes,slot->length);
        configASSERT(ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(100))!=0);
        configASSERT(xQueueSend(raw_free_q,&slot,0)==pdPASS);
    }
}
static void monitor_task(void *argument)
{
    uint32_t previous_capture=0,previous_processed=0,rounds=0;(void)argument;
    sn_board_watchdog_start();
    for(;;){
        vTaskDelay(pdMS_TO_TICKS(250));
        if(capture_blocks!=previous_capture && processed_blocks!=previous_processed && !sn_adc_errors)
            sn_board_watchdog_feed();
        previous_capture=capture_blocks;previous_processed=processed_blocks;
        sn_stack_watermarks[0]=uxTaskGetStackHighWaterMark(dsp_handle);
        sn_stack_watermarks[1]=uxTaskGetStackHighWaterMark(sn_comm_task_handle);
        sn_stack_watermarks[2]=uxTaskGetStackHighWaterMark(sn_raw_task_handle);
        sn_stack_watermarks[3]=uxTaskGetStackHighWaterMark(monitor_handle);
        if(SN_AUTO_BFSK_TX && ++rounds%20u==0u)(void)request_transmit();
    }
}
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb,StackType_t **stack,configSTACK_DEPTH_TYPE *depth)
{*tcb=&idle_tcb;*stack=idle_stack;*depth=sizeof(idle_stack)/sizeof(idle_stack[0]);}
void vApplicationStackOverflowHook(TaskHandle_t task,char *name)
{(void)task;(void)name;sn_board_fault();}
int main(void)
{
    unsigned i;sn_board_init();
    free_q=xQueueCreateStatic(4,sizeof(sample_block_t *),free_qmem,&free_qcb);
    ready_q=xQueueCreateStatic(4,sizeof(sample_block_t *),ready_qmem,&ready_qcb);
    raw_free_q=xQueueCreateStatic(2,sizeof(raw_slot_t *),raw_free_qmem,&raw_free_qcb);
    raw_ready_q=xQueueCreateStatic(2,sizeof(raw_slot_t *),raw_ready_qmem,&raw_ready_qcb);
    rtu_q=xQueueCreateStatic(2,sizeof(rtu_frame_t),rtu_qmem,&rtu_qcb);
    configASSERT(free_q && ready_q && raw_free_q && raw_ready_q && rtu_q);
    for(i=0;i<4;++i){sample_block_t *block=&blocks[i];configASSERT(xQueueSend(free_q,&block,0)==pdPASS);}
    for(i=0;i<2;++i){raw_slot_t *slot=&raw_slots[i];configASSERT(xQueueSend(raw_free_q,&slot,0)==pdPASS);}
    dsp_handle=xTaskCreateStatic(dsp_task,"DSP+BFSK",2048,NULL,4,dsp_stack,&dsp_tcb);
    sn_comm_task_handle=xTaskCreateStatic(comm_task,"Modbus",1024,NULL,3,comm_stack,&comm_tcb);
    sn_raw_task_handle=xTaskCreateStatic(raw_task,"Raw",768,NULL,2,raw_stack,&raw_tcb);
    monitor_handle=xTaskCreateStatic(monitor_task,"Health",512,NULL,1,monitor_stack,&monitor_tcb);
    configASSERT(dsp_handle && sn_comm_task_handle && sn_raw_task_handle && monitor_handle);
    vTaskStartScheduler();sn_board_fault();return 0;
}
