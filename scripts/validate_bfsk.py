"""独立NumPy发射器 -> C Goertzel/完整接收机。分别报告已同步BER与完整链路PER。"""
from pathlib import Path
import argparse,csv,io,json,math,subprocess
import numpy as np

def crc16(data):
    crc=0xffff
    for byte in data:
        crc^=byte
        for _ in range(8):crc=(crc>>1)^(0xa001 if crc&1 else 0)
    return crc

def tx_frame(sequence,payload):
    body=sequence.to_bytes(2,'big')+bytes([len(payload)])+payload
    crc=crc16(body)
    return b'\x55'*8+bytes.fromhex('d391a65c')+body+crc.to_bytes(2,'little')

def run(exe,wsl,wave,mode=None):
    command=(['wsl.exe','--exec',exe] if wsl else [exe])+([mode] if mode else [])
    data='\n'.join(f'{x:.8g}' for x in wave)+'\n'
    r=subprocess.run(command,input=data,encoding='utf-8',errors='replace',capture_output=True,check=True)
    return r.stdout

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--executable',required=True)
    parser.add_argument('--wsl',action='store_true');parser.add_argument('--out',type=Path,default=Path('validation/generated/bfsk'))
    a=parser.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    rng=np.random.default_rng(20261016)
    report={'seed':20261016,'sample_rate':8000,'bit_rate':100,'known_timing_ber':[],'packet_receiver':[]}
    # 非相干正交BFSK的理想AWGN模型：实噪声方差Eb/(2*EbN0)。不包含ADC限幅和同步。
    bits=rng.integers(0,2,2000)
    tone_index=np.where(bits==0,5,15)
    n=np.arange(80)
    phase=rng.uniform(0,2*np.pi,len(bits))
    symbols=np.sin(2*np.pi*tone_index[:,None]*n/80+phase[:,None])
    for db in [-4,0,4,8,12]:
        gamma=10**(db/10);sigma=math.sqrt(80/(4*gamma))
        wave=symbols+rng.normal(0,sigma,symbols.shape)
        decisions=np.array([int(v) for v in run(a.executable,a.wsl,wave.ravel(),'symbols').splitlines()])
        assert len(decisions)==len(bits)
        errors=int(np.count_nonzero(decisions!=bits));ber=errors/len(bits)
        theory=.5*math.exp(-gamma/2)
        tolerance=5*math.sqrt(theory*(1-theory)/len(bits))+.002
        assert abs(ber-theory)<tolerance,(db,ber,theory)
        report['known_timing_ber'].append({'eb_n0_db':db,'bits':len(bits),'errors':errors,'ber':ber,'theory':theory})
    payload=bytes((0xa5^(i*17))&255 for i in range(16))
    count=6
    chunks=[np.full(37,1.65)]
    for seq in range(count):
        frame=tx_frame(seq,payload);frame_bits=np.unpackbits(np.frombuffer(frame,dtype=np.uint8))
        frequencies=np.repeat(np.where(frame_bits==0,500.,1500.),80)
        # 独立构造的连续相位正弦，未复用C发射器16点LUT。
        phase=np.cumsum(2*np.pi*frequencies/8000)
        chunks.append(1.65+.75*np.sin(phase));chunks.append(np.full(800,1.65))
    base=np.concatenate(chunks)
    for name,noise,ppm in [('clean',0,0),('noise_005',.05,0),('noise_020',.2,0),
                           ('clock_plus_200ppm',.03,200),('clock_minus_200ppm',.03,-200),
                           ('clock_plus_1000ppm',.03,1000),('strong_noise',2.,0)]:
        ratio=1+ppm/1e6
        positions=np.arange(math.ceil(len(base)/ratio))*ratio
        wave=np.interp(positions,np.arange(len(base)),base,left=1.65,right=1.65)
        wave=wave+rng.normal(0,noise,len(wave))
        # 完整链路实验包含12bit ADC量化和0..3.3V限幅。
        wave=np.clip(np.rint(wave*4095/3.3),0,4095)*3.3/4095
        rows=list(csv.DictReader(io.StringIO(run(a.executable,a.wsl,wave))))
        valid={int(r['sequence']) for r in rows if r['crc_ok']=='1' and int(r['sequence'])<count and bytes.fromhex(r['payload_hex'])==payload}
        matched=[r for r in rows if int(r['sequence'])<count and int(r['length'])==len(payload)]
        errors=sum((x^y).bit_count() for r in matched for x,y in zip(bytes.fromhex(r['payload_hex']),payload))
        compared=len(matched)*len(payload)*8
        record={'case':name,'noise_sigma_v':noise,'clock_error_ppm':ppm,'sent_frames':count,
                'valid_frames':len(valid),'packet_error_rate':1-len(valid)/count,
                'reassembled_frames':len(rows),'compared_payload_bits':compared,
                'observed_payload_bit_errors':errors,'observed_payload_ber':errors/compared if compared else None}
        # ±1000ppm和强噪声是边界观测，不预设通过；当前未实现持续时钟跟踪。
        if name in ('clean','noise_005','noise_020','clock_plus_200ppm','clock_minus_200ppm'):
            assert len(valid)==count,record
        report['packet_receiver'].append(record)
    report['limitations']=['known_timing_ber assumes known symbol boundaries',
        'observed_payload_ber covers only fully reassembled matched frames; missing frames counted in PER',
        'no continuous clock-recovery PLL; no hardware/radio measurement']
    (a.out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
