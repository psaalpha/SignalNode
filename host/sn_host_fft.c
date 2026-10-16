#include "sn_dsp.h"
#include "sn_host_fft.h"
#include <math.h>
#include <stdint.h>

/* 仅供电脑验证的radix-2后端。MCU平台使用sn_cmsis_fft.c。
 * 两个后端共享CMSIS RFFT打包格式；NumPy作为独立数值参考。
 * 此处栈数组约8KB，不能直接搬进小栈RTOS任务。 */
int sn_host_fft(void *context, float *input, float *out)
{
    float re[SN_N], im[SN_N];
    uint32_t i, j = 0, len;
    (void)context;
    for (i = 0; i < SN_N; ++i) { re[i] = input[i]; im[i] = 0.0f; }
    for (i = 1; i < SN_N; ++i) {
        uint32_t bit = SN_N >> 1;
        float tmp;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j ^= bit;
        if (i < j) {
            tmp = re[i]; re[i] = re[j]; re[j] = tmp;
            tmp = im[i]; im[i] = im[j]; im[j] = tmp;
        }
    }
    for (len = 2; len <= SN_N; len <<= 1) {
        uint32_t start;
        for (start = 0; start < SN_N; start += len) {
            for (j = 0; j < len/2; ++j) {
                float angle = -6.2831853071795864769f * (float)j / (float)len;
                float wr = cosf(angle), wi = sinf(angle);
                uint32_t a = start+j, b = a+len/2;
                float tr = wr*re[b] - wi*im[b], ti = wr*im[b] + wi*re[b];
                re[b] = re[a]-tr; im[b] = im[a]-ti;
                re[a] += tr; im[a] += ti;
            }
        }
    }
    out[0] = re[0]; out[1] = re[SN_N/2];
    for (i = 1; i < SN_N/2; ++i) { out[2*i] = re[i]; out[2*i+1] = im[i]; }
    return 0;
}
