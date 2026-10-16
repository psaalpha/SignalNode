#include "sn_dsp.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

#define SN_PI 3.14159265358979323846f

static void reset_stream(sn_dsp_t *d)
{
    memset(d->raw, 0, sizeof(d->raw));
    memset(d->filtered, 0, sizeof(d->filtered));
    d->valid_samples = 0;
    d->d1 = d->d2 = 0.0f;
}

int sn_dsp_init(sn_dsp_t *d, const sn_config_t *cfg, sn_fft_fn fft, void *context)
{
    uint32_t i;
    if (!d || !cfg || !fft || !isfinite(cfg->vref) || cfg->vref <= 0.0f ||
        !isfinite(cfg->lowpass_hz) ||
        (cfg->lowpass_hz != 0.0f && (cfg->lowpass_hz < 100.0f || cfg->lowpass_hz > 2000.0f)) ||
        cfg->hann > 1u) return -1;
    memset(d, 0, sizeof(*d));
    d->config = *cfg;
    d->fft = fft;
    d->fft_context = context;
    for (i = 0; i < SN_N; ++i) {
        d->window[i] = cfg->hann ? 0.5f - 0.5f * cosf(2.0f * SN_PI * (float)i / SN_N) : 1.0f;
        d->sum_window += d->window[i];
        d->sum_window_squared += d->window[i] * d->window[i];
    }
    if (cfg->lowpass_hz > 0.0f) {
        /* 二阶Butterworth低通，双线性变换后的归一化系数，Q=1/sqrt(2)。
         * 本文件递推用 -a1*y、-a2*y；如果换CMSIS DF2T，要把反馈系数取反。
         * 系数只在初始化/配置切换时生成，不能逐个采样点重新计算sin/cos。 */
        float w = 2.0f * SN_PI * cfg->lowpass_hz / SN_FS;
        float c = cosf(w), alpha = sinf(w) / (2.0f * 0.7071067811865475f);
        float a0 = 1.0f + alpha;
        d->b0 = (1.0f - c) * 0.5f / a0;
        d->b1 = (1.0f - c) / a0;
        d->b2 = d->b0;
        d->a1 = -2.0f * c / a0;
        d->a2 = (1.0f - alpha) / a0;
    }
    return 0;
}

static float filter_one(sn_dsp_t *d, float x)
{
    float y;
    if (d->config.lowpass_hz == 0.0f) return x;
    y = d->b0 * x + d->d1;
    d->d1 = d->b1 * x - d->a1 * y + d->d2;
    d->d2 = d->b2 * x - d->a2 * y;
    return y;
}

static float bin_power(const float *packed, uint32_t k)
{
    /* arm_rfft_fast_f32的特殊打包：out[0]=DC，out[1]=Nyquist，
     * 其余out[2k],out[2k+1]为第k个复数bin，不能把前两个数当普通复数。 */
    if (k == 0u) return packed[0] * packed[0];
    if (k == SN_N / 2u) return packed[1] * packed[1];
    return packed[2u*k] * packed[2u*k] + packed[2u*k+1u] * packed[2u*k+1u];
}

int sn_dsp_push_adc(sn_dsp_t *d, const uint16_t samples[SN_HOP], uint64_t first,
                    sn_features_t *out)
{
    uint32_t i, k, peak_bin = 0;
    float raw_mean = 0.0f, filtered_mean = 0.0f, raw_variance = 0.0f, variance = 0.0f;
    float peak_power = 0.0f, total_band = 0.0f;
    if (!d || !samples || !out || !d->fft || first > UINT64_MAX - SN_HOP) return -1;
    /* 先校验整块，再修改状态，坏输入不会让滤波状态前进半块。 */
    for (i = 0; i < SN_HOP; ++i) if (samples[i] > 4095u) return -1;
    if (d->have_sequence && first != d->expected_sample) {
        ++d->discontinuities;
        reset_stream(d);
    }
    d->have_sequence = 1;
    d->expected_sample = first + SN_HOP;
    memmove(d->raw, d->raw + SN_HOP, SN_HOP * sizeof(float));
    memmove(d->filtered, d->filtered + SN_HOP, SN_HOP * sizeof(float));
    for (i = 0; i < SN_HOP; ++i) {
        float x = (float)samples[i] * d->config.vref / 4095.0f;
        d->raw[SN_HOP+i] = x;
        d->filtered[SN_HOP+i] = filter_one(d, x);
    }
    if (d->valid_samples < SN_N) d->valid_samples += SN_HOP;
    if (d->valid_samples < SN_N) return 0;
    memset(out, 0, sizeof(*out));
    out->first_sample = first - SN_HOP;
    out->discontinuities = d->discontinuities;
    for (i = 0; i < SN_N; ++i) {
        raw_mean += d->raw[i];
        filtered_mean += d->filtered[i];
        if (d->raw[i] <= 4.0f*d->config.vref/4095.0f ||
            d->raw[i] >= 4091.0f*d->config.vref/4095.0f) ++out->rail_samples;
    }
    raw_mean /= SN_N;
    filtered_mean /= SN_N;
    for (i = 0; i < SN_N; ++i) {
        float raw_ac = d->raw[i] - raw_mean;
        float ac = d->filtered[i] - filtered_mean;
        raw_variance += raw_ac * raw_ac;
        variance += ac * ac;
        /* RMS在未加窗数据上计算；窗函数只作用于频谱支路。 */
        d->fft_input[i] = ac * d->window[i];
    }
    out->mean_v = raw_mean;
    out->raw_ac_rms_v = sqrtf(raw_variance / SN_N);
    out->filtered_ac_rms_v = sqrtf(variance / SN_N);
    if (d->fft(d->fft_context, d->fft_input, d->fft_output) != 0) return -1;
    for (k = 1; k < SN_N/2u; ++k) {
        float hz = (float)k * SN_FS / SN_N;
        if (hz >= 20.0f && hz <= 2000.0f) {
            float p = bin_power(d->fft_output, k);
            total_band += 2.0f * p;
            if (p > peak_power) { peak_power = p; peak_bin = k; }
        }
    }
    /* 对功率用sum(w²)，对正弦峰值用sum(w)，两种归一化不能互换。 */
    out->band_power_v2 = total_band / ((float)SN_N * d->sum_window_squared);
    if (peak_power > 1.0e-16f) {
        float delta = 0.0f;
        out->peak_bin_hz = (float)peak_bin * SN_FS / SN_N;
        if (d->config.hann && peak_bin > 1u && peak_bin < SN_N/2u-1u) {
            float a = logf(fmaxf(bin_power(d->fft_output, peak_bin-1u), 1.0e-30f));
            float b = logf(peak_power);
            float c = logf(fmaxf(bin_power(d->fft_output, peak_bin+1u), 1.0e-30f));
            float denom = a - 2.0f*b + c;
            if (fabsf(denom) > 1.0e-6f) delta = 0.5f * (a-c) / denom;
            delta = fmaxf(-0.5f, fminf(delta, 0.5f));
        }
        out->peak_hz = ((float)peak_bin + delta) * SN_FS / SN_N;
        out->peak_amplitude_v = 2.0f * sqrtf(peak_power) / d->sum_window;
    }
    return 1;
}
