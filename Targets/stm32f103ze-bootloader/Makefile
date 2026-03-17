##############################################################################
# Makefile for STM32F103ZE OTA Bootloader
# Bare-metal C, no FreeRTOS, polling SDIO, minimal HAL
##############################################################################

# Toolchain
TC_PATH := /Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.100.202509120712/tools/bin
CC  := $(TC_PATH)/arm-none-eabi-gcc
AS  := $(TC_PATH)/arm-none-eabi-gcc
LD  := $(TC_PATH)/arm-none-eabi-gcc
OC  := $(TC_PATH)/arm-none-eabi-objcopy
SZ  := $(TC_PATH)/arm-none-eabi-size

# Directories
BUILD   := build
APP_DIR := ../stm32f103ze
DRV_DIR := $(APP_DIR)/Drivers
CMSIS   := $(DRV_DIR)/CMSIS
HAL_DIR := $(DRV_DIR)/STM32F1xx_HAL_Driver
FATFS   := $(APP_DIR)/Middlewares/Third_Party/FatFs/src
SHARED  := ../../Shared/Inc

# Linker script
LDSCRIPT := STM32F103ZETX_BOOTLOADER.ld

# MCU flags
MCU := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft

# C flags: size-optimized, no exceptions
CFLAGS := $(MCU) -std=c11 -Os -g \
	-DSTM32F103xE -DUSE_HAL_DRIVER \
	-ffunction-sections -fdata-sections \
	-Wall -Wno-unused-parameter \
	--specs=nano.specs

# Include paths
INCLUDES := \
	-ICore/Inc \
	-IBootloader \
	-IFatFs \
	-I$(SHARED) \
	-I$(HAL_DIR)/Inc \
	-I$(HAL_DIR)/Inc/Legacy \
	-I$(CMSIS)/Device/ST/STM32F1xx/Include \
	-I$(CMSIS)/Include \
	-I$(FATFS)

# Linker flags
LDFLAGS := $(MCU) -T$(LDSCRIPT) --specs=nosys.specs --specs=nano.specs \
	-Wl,-Map=$(BUILD)/bootloader.map \
	-Wl,--gc-sections \
	-static \
	-Wl,--start-group -lc -lm -Wl,--end-group

##############################################################################
# Sources
##############################################################################

# Bootloader sources
C_SRCS := \
	Core/Src/main.c \
	Core/Src/system_stm32f1xx.c \
	Core/Src/stm32f1xx_it.c \
	Core/Src/stm32f1xx_hal_msp.c \
	Bootloader/bl_flash.c \
	Bootloader/bl_uart.c \
	FatFs/bl_diskio.c

# HAL sources (minimal subset)
C_SRCS += \
	$(HAL_DIR)/Src/stm32f1xx_hal.c \
	$(HAL_DIR)/Src/stm32f1xx_hal_cortex.c \
	$(HAL_DIR)/Src/stm32f1xx_hal_rcc.c \
	$(HAL_DIR)/Src/stm32f1xx_hal_rcc_ex.c \
	$(HAL_DIR)/Src/stm32f1xx_hal_gpio.c \
	$(HAL_DIR)/Src/stm32f1xx_hal_gpio_ex.c \
	$(HAL_DIR)/Src/stm32f1xx_hal_flash.c \
	$(HAL_DIR)/Src/stm32f1xx_hal_flash_ex.c \
	$(HAL_DIR)/Src/stm32f1xx_hal_pwr.c \
	$(HAL_DIR)/Src/stm32f1xx_hal_dma.c \
	$(HAL_DIR)/Src/stm32f1xx_hal_sd.c \
	$(HAL_DIR)/Src/stm32f1xx_hal_uart.c \
	$(HAL_DIR)/Src/stm32f1xx_ll_sdmmc.c

# FatFS sources (from App project)
C_SRCS += \
	$(FATFS)/ff.c \
	$(FATFS)/ffunicode.c

# Startup assembly
ASM_SRCS := $(APP_DIR)/Core/Startup/startup_stm32f103zetx.s

##############################################################################
# Build rules
##############################################################################

# Object files: map source paths to build directory
# Local sources → build/local/path.o
# External sources → build/ext/basename.o
LOCAL_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(filter-out $(HAL_DIR)/%,$(filter-out $(FATFS)/%,$(C_SRCS))))
HAL_OBJS := $(patsubst $(HAL_DIR)/Src/%.c,$(BUILD)/hal/%.o,$(filter $(HAL_DIR)/%,$(C_SRCS)))
FATFS_OBJS := $(patsubst $(FATFS)/%.c,$(BUILD)/fatfs/%.o,$(filter $(FATFS)/%,$(C_SRCS)))
ASM_OBJS := $(BUILD)/startup.o

ALL_OBJS := $(LOCAL_OBJS) $(HAL_OBJS) $(FATFS_OBJS) $(ASM_OBJS)

TARGET := $(BUILD)/bootloader

.PHONY: all clean flash size

all: $(TARGET).elf $(TARGET).bin size

# Link
$(TARGET).elf: $(ALL_OBJS) $(LDSCRIPT)
	@echo "[LD] $@"
	@$(LD) $(ALL_OBJS) $(LDFLAGS) -o $@

# Binary
$(TARGET).bin: $(TARGET).elf
	@echo "[BIN] $@"
	@$(OC) -O binary $< $@

# Size report
size: $(TARGET).elf
	@$(SZ) $<

# Local C sources
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# HAL C sources
$(BUILD)/hal/%.o: $(HAL_DIR)/Src/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -Wno-dangling-pointer -c $< -o $@

# FatFS C sources — use bootloader's ffconf.h (listed first in -I)
$(BUILD)/fatfs/%.o: $(FATFS)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Startup assembly
$(BUILD)/startup.o: $(ASM_SRCS)
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	@$(AS) $(MCU) -c $< -o $@

# Flash via OpenOCD
flash: $(TARGET).elf
	/opt/homebrew/bin/openocd \
		-f interface/cmsis-dap.cfg \
		-c "transport select swd" \
		-f target/stm32f1x.cfg \
		-c "program $(TARGET).elf verify exit"

clean:
	rm -rf $(BUILD)
