# 依赖、参考与许可

自有应用和核心代码为C，外设层使用SPL，没有使用HAL。自有部分按MIT发布；第三方文件保留原许可，不由根目录MIT替换。

| 内容 | 固定来源 | 本地使用范围 |
|---|---|---|
| STM32F4 SPL V1.8.0 / device | [ST软件说明](https://www.st.com/en/embedded-software/stsw-stm32065.html)，[镜像提交413abc8](https://github.com/whimsicalraps/STM32F4xx_DSP_StdPeriph_Lib/commit/413abc8af439fc11d21d22dfe7d77f8cb80ac177) | vendor/spl、device、legacy-core，ST许可见vendor/ST-LICENSE.pdf |
| FreeRTOS V11.1.0 | [dbf70559](https://github.com/FreeRTOS/FreeRTOS-Kernel/commit/dbf70559b27d39c1fdb68dfb9a32140b6a6777a0) | tasks/queue/list、头文件、GCC/RVDS CM4F端口，MIT |
| CMSIS-DSP | [83a2d7bc](https://github.com/ARM-software/CMSIS-DSP/commit/83a2d7bc98c81b4bbe4a6f48b1f2ecf179868a0b) | RFFT/CFFT所需源文件和头文件，Apache-2.0 |
| CMSIS-Core | [CMSIS 5.9.0](https://github.com/ARM-software/CMSIS_5/tree/5.9.0) | Cortex-M编译器抽象和Core头文件，Apache-2.0 |

ST下载地址在本次访问中返回非ZIP内容，因此使用上述明确版本的公开镜像，并保留ST版权和许可。没有把镜像描述为ST官方托管仓库。

本地变更：CMSIS arm_compiler_specific.h仅补末尾换行，消除ARMCC警告；sn_fft_tables.c从固定版本原始common tables抽取512点CFFT/位逆序及1024点RFFT三张表，数值不改，保留ARM许可头；ARM启动文件将MSP改为4096、heap改为0。原始源文件仍在vendor可追溯。目标FFT初始化专用于标量M4配置，不适用MVE/NEON。

项目构思参考：

- [Spekky-Matrix 28cabcfa](https://github.com/jc-SpaceXp/Spekky-Matrix/commit/28cabcfa9f5aeb6286f00eacbaee92dacba0ebd8)：连续采集、RTOS、FFT和测试组织方式。未复制它的应用实现或继承其实测结论。
- [Modbus-STM32-HAL-FreeRTOS 9c022fd2](https://github.com/alejoseb/Modbus-STM32-HAL-FreeRTOS/commit/9c022fd2538edb5b53e2e07c98ebbe9a4685546c)：参考RTOS设备协议结构；因本项目明确使用SPL，实际采用新编写、主机测试的最小C RTU核心，没有引入其HAL/LGPL实现。

依据：[ARM RFFT文档](https://arm-software.github.io/CMSIS-DSP/latest/group__RealFFT.html)、[DF2T文档](https://arm-software.github.io/CMSIS-DSP/latest/group__BiquadCascadeDF2T.html)、[F407规格](https://www.st.com/en/microcontrollers-microprocessors/stm32f407vg.html)、[ST AN4031 DMA架构](https://www.st.com/resource/en/application_note/dm00046011-using-the-direct-memory-access-controller-on-stm32-microcontrollers-stmicroelectronics.pdf)、[Modbus规范入口](https://www.modbus.org/modbus-specifications)。
