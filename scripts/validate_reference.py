"""NumPy独立参考 + 双精度直接I型滤波，对照C实现。仅需numpy，不需scipy。

生成的是合成ADC码；结果证明数学/主机软件行为，不证明ADC、模拟电路或RTOS时序。
"""
from __future__ import annotations
import argparse
import csv
import io
import json
import math
from pathlib import Path
import subprocess
import numpy as np

FS, N, HOP, VREF = 8000, 1024, 512, 3.3


def reference(adc, cutoff, hann):
    raw = adc.astype(float) * VREF / 4095
    filt = raw.copy()
    if cutoff:
        w = 2 * math.pi * cutoff / FS
        alpha = math.sin(w) / math.sqrt(2)
        b = np.array([(1-math.cos(w))/2, 1-math.cos(w), (1-math.cos(w))/2])/(1+alpha)
        a = np.array([1., -2*math.cos(w)/(1+alpha), (1-alpha)/(1+alpha)])
        # 与C的DF2T结构不同，采用直接I型，跨块保持完整连续状态。
        x1 = x2 = y1 = y2 = 0.
        for i, x in enumerate(raw):
            y = b[0]*x+b[1]*x1+b[2]*x2-a[1]*y1-a[2]*y2
            filt[i] = y
            x2, x1, y2, y1 = x1, x, y1, y
    window = .5-.5*np.cos(2*np.pi*np.arange(N)/N) if hann else np.ones(N)
    freq = np.fft.rfftfreq(N, 1/FS)
    selected = (freq >= 20) & (freq <= 2000)
    rows = []
    for start in range(0, len(raw)-N+1, HOP):
        r, f = raw[start:start+N], filt[start:start+N]
        spectrum = np.fft.rfft((f-f.mean())*window)
        power = np.abs(spectrum)**2
        k = np.flatnonzero(selected)[np.argmax(power[selected])]
        delta = 0.
        if power[k] > 1e-16 and hann:
            v = np.log(np.maximum(power[k-1:k+2], 1e-30))
            denom = v[0]-2*v[1]+v[2]
            if abs(denom) > 1e-6:
                delta = float(np.clip(.5*(v[0]-v[2])/denom, -.5, .5))
        valid_peak = power[k] > 1e-16
        rows.append(dict(first_sample=start, mean_v=r.mean(), raw_ac_rms_v=r.std(),
                         filtered_ac_rms_v=f.std(), peak_bin_hz=freq[k] if valid_peak else 0.,
                         peak_hz=(k+delta)*FS/N if valid_peak else 0.,
                         peak_amplitude_v=2*np.sqrt(power[k])/window.sum() if valid_peak else 0.,
                         band_power_v2=2*power[selected].sum()/(N*np.square(window).sum()),
                         rail_samples=np.count_nonzero((adc[start:start+N]<=4)|(adc[start:start+N]>=4091))))
    return rows


def execute(exe, adc, cutoff, hann, wsl=False):
    text = '\n'.join(map(str, adc.tolist()))+'\n'
    command = (['wsl.exe', '--exec', str(exe)] if wsl else [str(exe)]) + [str(cutoff), str(hann)]
    result = subprocess.run(command, input=text, encoding='utf-8', errors='replace',
                            capture_output=True, check=True)
    return list(csv.DictReader(io.StringIO(result.stdout)))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--executable', required=True)
    p.add_argument('--out', type=Path, default=Path('validation/generated'))
    p.add_argument('--wsl', action='store_true', help='Windows Python调用WSL内的可执行文件')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    t = np.arange(HOP*32)/FS
    rng = np.random.default_rng(20261016)
    cases = [
        ('coherent_625_hann', 1.65+.6*np.cos(2*np.pi*625*t), 0, 1),
        ('coherent_625_rect', 1.65+.6*np.cos(2*np.pi*625*t), 0, 0),
        ('off_bin_637_3', 1.65+.6*np.cos(2*np.pi*637.3*t), 0, 1),
        ('mixture_hum_noise', 1.65+.6*np.cos(2*np.pi*625*t)+.12*np.sin(2*np.pi*50*t)+.01*rng.normal(size=len(t)), 0, 1),
        ('mixed_lowpass', 1.65+.6*np.cos(2*np.pi*625*t)+.2*np.sin(2*np.pi*2000*t), 1000, 1),
        ('tone_2k_bypass', 1.65+.6*np.cos(2*np.pi*2000*t), 0, 1),
        ('tone_2k_lowpass', 1.65+.6*np.cos(2*np.pi*2000*t), 1000, 1),
        ('dc_only', np.full(len(t), 1.65), 0, 1),
        ('near_rail', 1.65+2*np.cos(2*np.pi*625*t), 0, 1),
        ('alias_6500_to_1500', 1.65+.6*np.cos(2*np.pi*6500*t), 0, 1),
    ]
    metrics = []
    actual_cases = {}
    for name, signal, cutoff, hann in cases:
        adc = np.clip(np.rint(signal/VREF*4095), 0, 4095).astype(np.uint16)
        executable = args.executable if args.wsl else Path(args.executable).resolve()
        actual = execute(executable, adc, cutoff, hann, args.wsl)
        expected = reference(adc, cutoff, hann)
        assert len(actual) == len(expected) == 31
        errors = {}
        for field in expected[0]:
            av = np.array([float(row[field]) for row in actual])
            ev = np.array([float(row[field]) for row in expected])
            error = float(np.max(np.abs(av-ev)))
            errors[field] = error
            limit = .015 if field in ('peak_hz', 'peak_bin_hz') else .00003
            if field in ('first_sample', 'rail_samples'): limit = 0
            assert error <= limit, (name, field, error, limit)
        actual_cases[name] = actual
        metrics.append(dict(case=name, frames=len(actual), max_abs_error=errors))
        with (args.out/f'{name}.csv').open('w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=actual[0].keys());writer.writeheader();writer.writerows(actual)
        if name == 'mixture_hum_noise':
            np.savetxt(args.out/'demo_adc.txt', adc, fmt='%d')
    bypass = float(actual_cases['tone_2k_bypass'][-1]['peak_amplitude_v'])
    filtered = float(actual_cases['tone_2k_lowpass'][-1]['peak_amplitude_v'])
    attenuation = 20*math.log10(filtered/bypass)
    # Butterworth双线性变换的独立频响公式；比较稳态测量值。
    theory = -10*math.log10(1+(math.tan(math.pi*2000/FS)/math.tan(math.pi*1000/FS))**4)
    assert abs(attenuation-theory)<.02, (attenuation,theory)
    alias = float(actual_cases['alias_6500_to_1500'][-1]['peak_hz'])
    assert abs(alias-1500)<.05
    coherent = actual_cases['coherent_625_hann'][-1]
    off_bin = actual_cases['off_bin_637_3'][-1]
    report = dict(status='PASS', environment='host C + NumPy; synthetic ADC codes; no target measurements',
                  scenarios=len(cases), frames_compared=sum(m['frames'] for m in metrics),
                  coherent_625_last_frame=coherent, off_bin_637_3_last_frame=off_bin,
                  lowpass_2k_attenuation_db=attenuation, theoretical_attenuation_db=theory,
                  alias_6500_observed_hz=alias, cases=metrics)
    (args.out/'reference_report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='cases'}, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
