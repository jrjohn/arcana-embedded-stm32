# ============================================
# Arcana Embedded STM32 — Multi-Target CI Build
# Builds both F051 (Cortex-M0) and F103 (Cortex-M3)
# ============================================
cmake_minimum_required(VERSION 3.20)

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

# ============================================
# Target 1: STM32F051C8 (Cortex-M0)
# ============================================
set(F051_ROOT Targets/stm32f051c8)

set(F051_MCU_FLAGS -mcpu=cortex-m0 -mthumb -mfloat-abi=soft)
set(F051_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/${F051_ROOT}/STM32F051C8TX_FLASH.ld")

file(GLOB_RECURSE F051_CORE      "${F051_ROOT}/Core/Src/*.c")
file(GLOB_RECURSE F051_HAL       "${F051_ROOT}/Drivers/STM32F0xx_HAL_Driver/Src/*.c")
file(GLOB_RECURSE F051_RTOS      "${F051_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/*.c"
                                  "${F051_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/*.c"
                                  "${F051_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM0/*.c"
                                  "${F051_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c")
file(GLOB         F051_STARTUP   "${F051_ROOT}/Core/Startup/*.s")
file(GLOB_RECURSE F051_SERVICES  "${F051_ROOT}/Services/**/*.cpp")
file(GLOB_RECURSE F051_SHARED    "Shared/Src/*.cpp")

add_executable(arcana-f051.elf
    ${F051_CORE} ${F051_HAL} ${F051_RTOS} ${F051_STARTUP}
    ${F051_SERVICES} ${F051_SHARED}
)

target_compile_definitions(arcana-f051.elf PRIVATE USE_HAL_DRIVER STM32F051x8)
target_include_directories(arcana-f051.elf PRIVATE
    ${F051_ROOT}/Core/Inc
    ${F051_ROOT}/Drivers/STM32F0xx_HAL_Driver/Inc
    ${F051_ROOT}/Drivers/STM32F0xx_HAL_Driver/Inc/Legacy
    ${F051_ROOT}/Drivers/CMSIS/Device/ST/STM32F0xx/Include
    ${F051_ROOT}/Drivers/CMSIS/Include
    ${F051_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/include
    ${F051_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
    ${F051_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM0
    ${F051_ROOT}/Services/controller
    ${F051_ROOT}/Services/service
    ${F051_ROOT}/Services/command
    ${F051_ROOT}/Services/command/impl
    Shared/Inc
    Shared/Inc/ats
    Shared/Inc/display
)
target_compile_options(arcana-f051.elf PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${F051_MCU_FLAGS} -Wall -fdata-sections -ffunction-sections -std=gnu11>
    $<$<COMPILE_LANGUAGE:CXX>:${F051_MCU_FLAGS} -Wall -fdata-sections -ffunction-sections -std=gnu++20 -fno-exceptions -fno-rtti -fno-use-cxa-atexit>
    $<$<COMPILE_LANGUAGE:ASM>:${F051_MCU_FLAGS} -x assembler-with-cpp>
)
target_link_options(arcana-f051.elf PRIVATE
    "LINKER:-T${F051_LINKER_SCRIPT}"
    ${F051_MCU_FLAGS}
    -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs
    -Wl,--start-group -lc -lm -lstdc++ -lsupc++ -Wl,--end-group
    -Wl,--print-memory-usage
)

add_custom_command(TARGET arcana-f051.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:arcana-f051.elf> arcana-f051.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:arcana-f051.elf> arcana-f051.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:arcana-f051.elf>
)

# ============================================
# Target 2: STM32F103ZE (Cortex-M3)
# ============================================
set(F103_ROOT Targets/stm32f103ze)

set(F103_MCU_FLAGS -mcpu=cortex-m3 -mthumb -mfloat-abi=soft)
set(F103_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/${F103_ROOT}/STM32F103ZETX_FLASH.ld")

file(GLOB_RECURSE F103_CORE      "${F103_ROOT}/Core/Src/*.c")
file(GLOB_RECURSE F103_HAL       "${F103_ROOT}/Drivers/STM32F1xx_HAL_Driver/Src/*.c")
file(GLOB_RECURSE F103_RTOS      "${F103_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/*.c"
                                  "${F103_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/*.c"
                                  "${F103_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3/*.c"
                                  "${F103_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c")
file(GLOB         F103_STARTUP   "${F103_ROOT}/Core/Startup/*.s")
file(GLOB_RECURSE F103_FATFS     "${F103_ROOT}/Middlewares/Third_Party/FatFs/src/*.c")
file(GLOB_RECURSE F103_FLASHDB   "${F103_ROOT}/Middlewares/Third_Party/FlashDB/src/*.c")
file(GLOB_RECURSE F103_SERVICES  "${F103_ROOT}/Services/**/*.cpp")
file(GLOB_RECURSE F103_SHARED    "Shared/Src/*.cpp")
file(GLOB         F103_NANOPB    "Shared/Src/nanopb/*.c")
file(GLOB         F103_PROTO     "Shared/Src/*.c")
file(GLOB         F103_MBEDTLS   "Shared/Src/mbedtls/*.c")

# Exclude FatFs backup directory (duplicate ff.c)
list(FILTER F103_FATFS EXCLUDE REGEX ".*/backup/.*")

add_executable(arcana-f103.elf
    ${F103_CORE} ${F103_HAL} ${F103_RTOS} ${F103_STARTUP}
    ${F103_FATFS} ${F103_FLASHDB}
    ${F103_SERVICES} ${F103_SHARED}
    ${F103_NANOPB} ${F103_PROTO} ${F103_MBEDTLS}
)

target_compile_definitions(arcana-f103.elf PRIVATE
    USE_HAL_DRIVER STM32F103xE
)
target_include_directories(arcana-f103.elf PRIVATE
    ${F103_ROOT}/Core/Inc
    ${F103_ROOT}/Drivers/STM32F1xx_HAL_Driver/Inc
    ${F103_ROOT}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy
    ${F103_ROOT}/Drivers/CMSIS/Device/ST/STM32F1xx/Include
    ${F103_ROOT}/Drivers/CMSIS/Include
    ${F103_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/include
    ${F103_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
    ${F103_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3
    ${F103_ROOT}/Middlewares/Third_Party/FatFs/src
    ${F103_ROOT}/Middlewares/Third_Party/FlashDB/inc
    ${F103_ROOT}/Services/Controller
    ${F103_ROOT}/Services/Service
    ${F103_ROOT}/Services/Service/impl
    ${F103_ROOT}/Services/Driver
    ${F103_ROOT}/Services/Model
    ${F103_ROOT}/Services/Common
    ${F103_ROOT}/Services/View
    ${F103_ROOT}/Services/ViewModel
    ${F103_ROOT}/Services/Command
    Shared/Inc
    Shared/Inc/ats
    Shared/Inc/display
    Shared/Inc/nanopb
    Shared/Inc/mbedtls
)
target_compile_options(arcana-f103.elf PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${F103_MCU_FLAGS} -Wall -fdata-sections -ffunction-sections -std=gnu11 -Wno-dangling-pointer>
    $<$<COMPILE_LANGUAGE:CXX>:${F103_MCU_FLAGS} -Wall -fdata-sections -ffunction-sections -std=gnu++20 -fno-exceptions -fno-rtti -fno-use-cxa-atexit>
    $<$<COMPILE_LANGUAGE:ASM>:${F103_MCU_FLAGS} -x assembler-with-cpp>
)
target_link_options(arcana-f103.elf PRIVATE
    "LINKER:-T${F103_LINKER_SCRIPT}"
    ${F103_MCU_FLAGS}
    -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs
    -Wl,--start-group -lc -lm -lstdc++ -lsupc++ -Wl,--end-group
    -Wl,--print-memory-usage
)

add_custom_command(TARGET arcana-f103.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:arcana-f103.elf> arcana-f103.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:arcana-f103.elf> arcana-f103.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:arcana-f103.elf>
)
