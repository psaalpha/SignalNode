# SignalNode

**基于 STM32F407、C、SPL 与 FreeRTOS 的信号采集、频谱分析和有线 BFSK 通信终端。**

仓库：https://github.com/psaalpha/SignalNode

硬件默认：STM32F407VGT6，8MHz外部晶振；使用ST标准外设库SPL，不使用HAL。BFSK是本版正式功能，与频谱分析共用8kSa/s连续采样链路。

当前状态：**完整目标代码已完成本机Keil/GCC编译链接，主机数值/协议测试通过；尚未烧录或进行任何硬件验证。** 编译成功不证明ADC时序、实际误码率、RS485互通或长时间稳定性达标。

## 功能

- TIM2触发ADC1，DMA循环双半缓冲；静态采样块池，丢块计数和64位采样索引。
- 连续IIR低通、AC RMS、Hann/矩形窗、1024点CMSIS实数FFT、主频及频段功率；128ms窗口、64ms更新。
- DAC+DMA生成500/1500Hz BFSK，100bit/s；8字节前导、32bit同步字、序号/长度/CRC，最大32字节payload。
- 接收端两路Goertzel，4种初始符号时相搜索，跨DMA块组帧，坏CRC/长度/采样断流后的恢复。
- RS485 Modbus RTU，地址17、19200 8E1，功能码04/06/16；特征一致性快照、参数按块边界生效。
- USART1独立原始数据导出，版本化帧、CRC16、传输序号/采样索引；电脑慢时丢导出副本并统计。
- FreeRTOS静态任务/队列、栈高水位、DWT最大耗时记录、采样与处理进度看门狗。
- Python读取结果、触发BFSK、保存原始ADC数据；NumPy频谱与BER/PER对照实验。

## 先从哪里读

| 文件 | 内容 |
|---|---|
| [快速联调与引脚](docs/FIRMWARE_GUIDE.md) | 开发板假设、接线、构建、串口命令、寄存器表 |
| [项目设计](docs/PROJECT_DESIGN.md) | 任务、采样所有权、信号处理和链路设计 |
| [BFSK协议与实验](docs/BFSK.md) | Goertzel、初始同步、BER与PER的区别 |
| [验证记录](docs/VALIDATION.md) | 实际编译/数值测试结果和硬件待验证范围 |
| [硬件验收](docs/HARDWARE_ACCEPTANCE.md) | 必须继续完成的板上测试 |
| [依赖与许可](docs/SOURCES.md) | SPL、FreeRTOS、CMSIS版本及修改边界 |

代码阅读顺序：`firmware/target/sn_app.c` → `sn_board_spl.c` → `firmware/core/sn_bfsk.c` / `sn_dsp.c` → `sn_modbus.c` / `sn_wire.c`。

## Keil

打开`keil/SignalNode.uvprojx`：

- `SignalNode_F407_Full`：频谱+BFSK+Modbus+原始导出。
- `SignalNode_F407_BFSK_Lite`：保留真实采样、BFSK、Modbus与原始导出，去掉频谱计算。

使用ARMCC5.06、C99、MicroLIB、M4F硬件浮点。当前完整配置在既有32KB评估限制内，但余量小；实际数字以验证记录为准。目标工程没有自动烧录或飞行功能。

## GCC目标构建

安装Arm GNU工具链、CMake；Windows可设置`ARM_GCC_BIN`到工具链bin目录。

```sh
cmake -S firmware -B build-arm -DCMAKE_TOOLCHAIN_FILE=$PWD/scripts/arm-gcc.cmake
cmake --build build-arm --parallel 2
```

输出`SignalNode.elf/.bin/.map`。普通SRAM限128KB，DMA缓冲不使用CCM。

## 主机验证

```sh
python -m pip install -r requirements.txt
bash scripts/build_and_test.sh
python scripts/validate_reference.py --executable build-host/sn_replay_cmsis --out validation/generated/cmsis
python scripts/validate_bfsk.py --executable build-host/sn_bfsk_replay --out validation/generated/bfsk
```

Windows Python调用WSL时加`--wsl`，并传入`/mnt/c/...`形式的可执行文件路径。

## 串口联调

```sh
python tools/sn_client.py read --port COM5
python tools/sn_client.py send-bfsk --port COM5
python tools/sn_client.py capture --port COM6 --seconds 10 --out capture
```

COM5示例指USB-RS485，COM6指独立3.3V USB-UART，必须替换成实际端口。默认每5秒自动准备一帧已知测试payload，`SN_AUTO_BFSK_TX=0`可关闭。单板DAC回环只能检查通路，不能证明独立时钟通信性能。

当前限制：初始时相搜索不含持续时钟跟踪PLL；非整bin幅值仍有栅栏误差；模拟前端、参考电压和RS485物理层未验证；没有工业认证或精密仪器精度结论。MIT许可覆盖自有代码，第三方文件保留原许可。
