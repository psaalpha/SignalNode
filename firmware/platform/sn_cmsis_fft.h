#ifndef SN_CMSIS_FFT_H
#define SN_CMSIS_FFT_H
#include "dsp/transform_functions.h"
typedef struct { arm_rfft_fast_instance_f32 instance; } sn_cmsis_fft_t;
int sn_cmsis_fft_init(sn_cmsis_fft_t *backend);
int sn_cmsis_fft(void *context, float *input, float *packed_output);
#endif
