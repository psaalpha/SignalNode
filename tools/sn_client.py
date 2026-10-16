"""SignalNode串口联调：原始数据保存、Modbus读结果、触发BFSK发送。
依赖pyserial；不自动扫描/打开其他串口，也不假定板子已接好。
"""
from pathlib import Path
import argparse,csv,json,struct,time

def crc16(data):
    crc=0xffff
    for x in data:
        crc^=x
        for _ in range(8):crc=(crc>>1)^(0xa001 if crc&1 else 0)
    return crc

def read_exact(port,size):
    data=bytearray();end=time.monotonic()+1
    while len(data)<size and time.monotonic()<end:
        data.extend(port.read(size-len(data)))
    if len(data)!=size:raise RuntimeError(f'接收超时：需要{size}字节，收到{len(data)}字节')
    return bytes(data)

def rtu(args):
    import serial
    with serial.Serial(args.port,19200,bytesize=8,parity='E',stopbits=1,timeout=.1) as port:
        port.reset_input_buffer()
        body=struct.pack('>BBHH',17,6,2,1) if args.action=='send-bfsk' else struct.pack('>BBHH',17,4,0,52)
        port.write(body+struct.pack('<H',crc16(body)));port.flush()
        head=read_exact(port,3)
        if head[0]!=17:raise RuntimeError('响应地址不符')
        if head[1]&0x80:
            reply=head+read_exact(port,2)
            if crc16(reply):raise RuntimeError('异常响应CRC错误')
            raise RuntimeError(f'Modbus异常码：{head[2]}')
        if args.action=='send-bfsk':
            reply=head+read_exact(port,5)
            if reply[:6]!=body or crc16(reply):raise RuntimeError('写响应内容/CRC错误')
            print('已提交BFSK测试帧发送请求');return
        if head[1]!=4 or head[2]!=104:raise RuntimeError('读响应功能码/长度不符')
        reply=head+read_exact(port,106)
        if crc16(reply):raise RuntimeError('读响应CRC错误')
        words=struct.unpack('>52H',reply[3:-2])
        def u32(i):return (words[i]<<16)|words[i+1]
        result={'sample_rate_hz':words[1],'fft_size':words[2],'hop':words[3],
            'spectrum_valid':bool(words[4]&1),'spectrum_enabled':bool(words[4]&0x100),'configuration':words[5],
            'processed_blocks':u32(6),'mean_mv':u32(12),'raw_rms_mv':u32(14),
            'filtered_rms_mv':u32(16),'peak_hz':u32(18)/1000,'capture_drops':u32(22),
            'export_drops':u32(24),'max_dsp_us':u32(26),'bfsk_syncs':u32(28),
            'bfsk_crc_ok_frames':u32(30),'bfsk_crc_errors':u32(32),'bfsk_length_errors':u32(34),
            'known_pattern_bit_errors':u32(36),'known_pattern_compared_bits':u32(38),
            'last_bfsk_sequence':u32(40),'adc_errors':u32(42),'max_adc_isr_us':u32(44),'rtu_queue_drops':u32(46),
            'tx_prepared_sequence_mod65536':u32(48),'dac_errors':u32(50)}
        print(json.dumps(result,ensure_ascii=False,indent=2))

def capture(args):
    import serial
    args.out.mkdir(parents=True,exist_ok=True)
    buffer=bytearray();frames=bad=sample_gaps=0;expected=None
    end=time.monotonic()+args.seconds
    with serial.Serial(args.port,921600,timeout=.1) as port, (args.out/'adc.csv').open('w',newline='') as file:
        writer=csv.writer(file);writer.writerow(['sample_index','adc_code'])
        while time.monotonic()<end:
            buffer.extend(port.read(4096))
            while len(buffer)>=20:
                position=buffer.find(b'SN')
                if position<0:
                    buffer[:]=buffer[-1:];break
                if position:del buffer[:position]
                if len(buffer)<20:break
                version,kind,length,seq,first,fs=struct.unpack_from('<BBHIQH',buffer,2)
                if version!=1 or length>1024 or not fs:del buffer[0];continue
                total=22+length
                if len(buffer)<total:break
                frame=bytes(buffer[:total])
                if crc16(frame):bad+=1;del buffer[0];continue
                del buffer[:total]
                if kind!=1 or length!=1024:continue
                values=struct.unpack('<512H',frame[20:-2])
                if expected is not None and first!=expected:sample_gaps+=1
                expected=first+512;frames+=1
                writer.writerows((first+i,x) for i,x in enumerate(values))
    result={'frames':frames,'samples':frames*512,'crc_errors':bad,'sample_index_gaps':sample_gaps,
            'note':'导出链路有缺口时需结合设备capture_drops/export_drops区分原因'}
    (args.out/'capture_report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))

def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=['read','send-bfsk','capture'])
    p.add_argument('--port',required=True);p.add_argument('--seconds',type=float,default=10)
    p.add_argument('--out',type=Path,default=Path('capture'))
    a=p.parse_args()
    if a.action=='capture':capture(a)
    else:rtu(a)
if __name__=='__main__':main()
