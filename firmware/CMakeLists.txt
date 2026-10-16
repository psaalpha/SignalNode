cmake_minimum_required(VERSION 3.20)
project(SignalNode_F407 C ASM)
set(CMAKE_C_STANDARD 99)
get_filename_component(SN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
include(sources.cmake)
option(SN_ENABLE_SPECTRUM "Build spectrum plus BFSK" ON)
add_executable(SignalNode ${SN_SOURCES} target/startup_gcc.s
    ../vendor/freertos/portable/GCC/ARM_CM4F/port.c)
target_include_directories(SignalNode PRIVATE ${SN_INCLUDES}
    ../vendor/freertos/portable/GCC/ARM_CM4F)
target_compile_definitions(SignalNode PRIVATE USE_STDPERIPH_DRIVER STM32F40_41xxx HSE_VALUE=8000000
    ARM_MATH_CM4 DISABLEFLOAT16 SN_ENABLE_SPECTRUM=$<BOOL:${SN_ENABLE_SPECTRUM}>)
target_compile_options(SignalNode PRIVATE -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
    -Os -g3 -ffunction-sections -fdata-sections -Wall -Wextra)
target_link_options(SignalNode PRIVATE -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
    -T${CMAKE_CURRENT_SOURCE_DIR}/target/stm32f407vg.ld -nostartfiles --specs=nano.specs --specs=nosys.specs
    -Wl,--gc-sections,-Map=${CMAKE_CURRENT_BINARY_DIR}/SignalNode.map -Wl,--print-memory-usage)
target_link_libraries(SignalNode PRIVATE m c)
set_target_properties(SignalNode PROPERTIES SUFFIX ".elf")
add_custom_command(TARGET SignalNode POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:SignalNode> ${CMAKE_CURRENT_BINARY_DIR}/SignalNode.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:SignalNode>)
