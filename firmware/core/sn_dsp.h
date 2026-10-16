#ifndef SN_DSP_H
#define SN_DSP_H
#include <stdint.h>

/* 第一版固定采样率和帧长，先验证单通道，再扩展动态配置和多通道。
 * N/Fs=128 ms 是观测窗长度；HOP/Fs=64 ms 是更新间隔，两者不能混为时延。
 * 相邻窗重叠50%，但滤波器只处理新增512点，不能把重叠样本重复滤一遍。 */
#define SN_FS 8000u
#define SN_N 1024u
#define SN_HOP 512u

typedef int (*sn_fft_fn)(void *context, float *input, float *packed_output);
typedef struct {
    float vref;                 /* 初始3.3V只是标称值；上板后需测量/校准。 */
    float lowpass_hz;           /* 0表示旁路；允许100..2000 Hz。 */
    uint8_t hann;              /* 1: periodic Hann；0: rectangular。 */
} sn_config_t;

typedef struct {
    uint64_t first_sample;
    uint32_t discontinuities;
    uint32_t rail_samples;      /* 接近ADC上下轨的点数，不能直接等同于模拟削顶次数。 */
    float mean_v;
    float raw_ac_rms_v;
    float filtered_ac_rms_v;
    float peak_bin_hz;
    float peak_hz;              /* Hann时可做三点log幅度插值，不改变真实频率分辨能力。 */
    float peak_amplitude_v;     /* 按窗coherent gain校正的峰值；非整bin仍有栅栏误差。 */
    float band_power_v2;        /* 20..2000 Hz内bin功率和，单位V²，不是功率谱密度。 */
} sn_features_t;

typedef struct {
    sn_config_t config;
    sn_fft_fn fft;
    void *fft_context;
    float raw[SN_N], filtered[SN_N], window[SN_N];
    float fft_input[SN_N], fft_output[SN_N];
    float sum_window, sum_window_squared;
    float b0, b1, b2, a1, a2, d1, d2;
    uint64_t expected_sample;
    uint32_t valid_samples, discontinuities;
    uint8_t have_sequence;
} sn_dsp_t;

int sn_dsp_init(sn_dsp_t *dsp, const sn_config_t *config, sn_fft_fn fft, void *context);
/* 返回1:产生结果；0:尚未攒够一窗；-1:参数/FFT后端错误。
 * 第一块的first_sample通常为0。索引不连续会清空滑窗和滤波状态，重新预热。
 * 本对象只有DSP任务一个写入者；通信任务读取发布后的结果快照。 */
int sn_dsp_push_adc(sn_dsp_t *dsp, const uint16_t samples[SN_HOP],
                    uint64_t first_sample, sn_features_t *features);
#endif
