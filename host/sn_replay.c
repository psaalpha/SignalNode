#include "sn_dsp.h"
#include "sn_host_fft.h"
#ifdef SN_USE_CMSIS_HOST
#include "sn_cmsis_fft.h"
static sn_cmsis_fft_t cmsis;
#endif
#include <stdio.h>
#include <stdlib.h>

static sn_dsp_t dsp;
int main(int argc, char **argv)
{
    sn_config_t cfg={3.3f,0.0f,1};
    uint16_t samples[SN_HOP];
    uint64_t first=0;
    unsigned value, count=0;
    if(argc>1) cfg.lowpass_hz=(float)atof(argv[1]);
    if(argc>2) cfg.hann=(uint8_t)atoi(argv[2]);
#ifdef SN_USE_CMSIS_HOST
    if(sn_cmsis_fft_init(&cmsis)!=0 || sn_dsp_init(&dsp,&cfg,sn_cmsis_fft,&cmsis)!=0)return 2;
#else
    if(sn_dsp_init(&dsp,&cfg,sn_host_fft,NULL)!=0)return 2;
#endif
    puts("first_sample,mean_v,raw_ac_rms_v,filtered_ac_rms_v,peak_bin_hz,peak_hz,peak_amplitude_v,band_power_v2,rail_samples");
    while(scanf("%u",&value)==1) {
        sn_features_t f;
        int status;
        if(value>4095u)return 3;
        samples[count++]=(uint16_t)value;
        if(count!=SN_HOP)continue;
        count=0;
        status=sn_dsp_push_adc(&dsp,samples,first,&f);
        first+=SN_HOP;
        if(status<0)return 4;
        if(status==1)printf("%llu,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u\n",
            (unsigned long long)f.first_sample,f.mean_v,f.raw_ac_rms_v,
            f.filtered_ac_rms_v,f.peak_bin_hz,f.peak_hz,f.peak_amplitude_v,
            f.band_power_v2,(unsigned)f.rail_samples);
    }
    return count ? 5 : 0;
}
