# ============================================
# STM32F051C8TX CMake Wrapper (CI Build)
# Generated for CI pipeline — not part of source
# ============================================
cmake_minimum_required(VERSION 3.20)

# Toolchain must be set before project()
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

project(arcana-embedded-stm32 C CXX ASM)

set(MCU_FLAGS "-mcpu=cortex-m0 -mthumb -mfloat-abi=soft")
set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/STM32F051C8TX_FLASH.ld")

# Common flags
set(COMMON_FLAGS "${MCU_FLAGS} -Wall -fdata-sections -ffunction-sections")
set(CMAKE_C_FLAGS "${COMMON_FLAGS} -std=gnu11")
set(CMAKE_CXX_FLAGS "${COMMON_FLAGS} -std=gnu++17 -fno-exceptions -fno-rtti")
set(CMAKE_ASM_FLAGS "${MCU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS "-T${LINKER_SCRIPT} ${MCU_FLAGS} -Wl,--gc-sections -specs=nano.specs -specs=nosys.specs -lc -lm -lnosys -Wl,--print-memory-usage")

# Definitions
add_definitions(
    -DUSE_HAL_DRIVER
    -DSTM32F051x8
)

# Include directories
include_directories(
    Core/Inc
    Drivers/STM32F0xx_HAL_Driver/Inc
    Drivers/CMSIS/Device/ST/STM32F0xx/Include
    Drivers/CMSIS/Include
    Middlewares/Third_Party/FreeRTOS/Source/include
    Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
    Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM0
)

# Source files
file(GLOB_RECURSE CORE_SOURCES "Core/Src/*.c" "Core/Src/*.cpp")
file(GLOB_RECURSE HAL_SOURCES "Drivers/STM32F0xx_HAL_Driver/Src/*.c")
file(GLOB_RECURSE FREERTOS_SOURCES
    "Middlewares/Third_Party/FreeRTOS/Source/*.c"
    "Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/*.c"
    "Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM0/*.c"
    "Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c"
)
file(GLOB STARTUP_SOURCES "Core/Startup/*.s")

# Executable
add_executable(${PROJECT_NAME}.elf
    ${CORE_SOURCES}
    ${HAL_SOURCES}
    ${FREERTOS_SOURCES}
    ${STARTUP_SOURCES}
)

# Post-build: create .bin and .hex
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
)
