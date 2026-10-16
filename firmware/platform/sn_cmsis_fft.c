#include "sn_dsp.h"
#include "sn_cmsis_fft.h"
#include "arm_common_tables.h"

/* 固定1024点M4F后端。按CMSIS官方1024 RFFT初始化设置，仅保留所需3张表。
 * 不调用通用长度初始化，以免把32..4096各长度旋转表一起链接进来。
 * 该结构设置只针对本工程的标量M4实现；不能直接用于MVE/NEON平台。
 * FFT会修改input，所以核心使用独立工作区，保留原始滑窗数据。 */
int sn_cmsis_fft_init(sn_cmsis_fft_t *backend)
{
    if(!backend)return -1;
    backend->instance.Sint.fftLen=512;
    backend->instance.Sint.pTwiddle=twiddleCoef_512;
    backend->instance.Sint.pBitRevTable=armBitRevIndexTable512;
    backend->instance.Sint.bitRevLength=ARMBITREVINDEXTABLE_512_TABLE_LENGTH;
    backend->instance.fftLenRFFT=SN_N;
    backend->instance.pTwiddleRFFT=(float32_t *)twiddleCoef_rfft_1024;
    return 0;
}
int sn_cmsis_fft(void *context,float *input,float *packed)
{
    sn_cmsis_fft_t *backend=(sn_cmsis_fft_t *)context;
    if(!backend)return -1;
    arm_rfft_fast_f32(&backend->instance,input,packed,0);
    return 0;
}
