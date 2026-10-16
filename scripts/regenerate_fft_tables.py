"""从已固定版本的CMSIS原始表生成M4F的1024点RFFT最小集合；不改变数值。"""
from pathlib import Path
import re
p=Path(__file__).resolve().parent.parent
source=(p/'vendor/cmsis-dsp/Source/CommonTables/arm_common_tables.c').read_text(encoding='utf-8-sig')
parts=[source[:source.index('#include')],
       '/* SignalNode: generated fixed-1024 RFFT table subset; original values unchanged. */\n',
       '#include "arm_math_types.h"\n#include "arm_common_tables.h"\n']
for name in ['twiddleCoef_512','armBitRevIndexTable512','twiddleCoef_rfft_1024']:
    match=re.search(r'const\s+(?:float32_t|uint16_t)\s+'+name+r'\[[^\]]+\][^=]*=\s*\{.*?\};',source,re.S)
    if not match:raise RuntimeError(f'缺少预期表：{name}，请核对CMSIS版本')
    parts.append(match.group(0)+'\n')
(p/'firmware/target/sn_fft_tables.c').write_text('\n'.join(parts),encoding='utf-8')
