################################################################################
# Shared/Src — Observable + CryptoEngine (C++) + nanopb + mbedtls + proto (C)
################################################################################

SHARED := /Users/jrjohn/Documents/projects/arcana-embedded-stm32/Shared

# --- Include paths (add nanopb + mbedtls) ---
SHARED_INCS = \
  -I../../../Shared/Inc \
  -I../../../Shared/Inc/ats \
  -I../../../Shared/Inc/display \
  -I../../../Shared/Inc/nanopb \
  -I../../../Shared/Inc/mbedtls

MBEDTLS_DEFS = -DMBEDTLS_CONFIG_FILE='"mbedtls_config.h"'

# --- C++ sources ---
CPP_SRCS += \
$(SHARED)/Src/Observable.cpp \
$(SHARED)/Src/CryptoEngine.cpp \
$(SHARED)/Src/KeyExchangeManager.cpp

# --- C sources ---
C_SRCS += \
$(SHARED)/Src/arcana_cmd.pb.c \
$(SHARED)/Src/registration.pb.c \
$(SHARED)/Src/nanopb/pb_common.c \
$(SHARED)/Src/nanopb/pb_encode.c \
$(SHARED)/Src/nanopb/pb_decode.c \
$(SHARED)/Src/mbedtls/aes.c \
$(SHARED)/Src/mbedtls/ccm.c \
$(SHARED)/Src/mbedtls/sha256.c \
$(SHARED)/Src/mbedtls/cipher.c \
$(SHARED)/Src/mbedtls/cipher_wrap.c \
$(SHARED)/Src/mbedtls/platform_util.c \
$(SHARED)/Src/mbedtls/constant_time.c \
$(SHARED)/Src/mbedtls/ecp.c \
$(SHARED)/Src/mbedtls/ecp_curves.c \
$(SHARED)/Src/mbedtls/bignum.c \
$(SHARED)/Src/mbedtls/bignum_core.c \
$(SHARED)/Src/mbedtls/bignum_mod.c \
$(SHARED)/Src/mbedtls/bignum_mod_raw.c \
$(SHARED)/Src/mbedtls/ecdh.c \
$(SHARED)/Src/mbedtls/ecdsa.c \
$(SHARED)/Src/mbedtls/asn1parse.c \
$(SHARED)/Src/mbedtls/asn1write.c \
$(SHARED)/Src/mbedtls/entropy.c \
$(SHARED)/Src/mbedtls/entropy_poll.c \
$(SHARED)/Src/mbedtls/entropy_hardware_alt.c \
$(SHARED)/Src/mbedtls/ctr_drbg.c \
$(SHARED)/Src/uECC.c \
$(SHARED)/Src/mbedtls/md.c

OBJS += \
./Shared/Src/Observable.o \
./Shared/Src/CryptoEngine.o \
./Shared/Src/KeyExchangeManager.o \
./Shared/Src/arcana_cmd.pb.o \
./Shared/Src/registration.pb.o \
./Shared/Src/nanopb/pb_common.o \
./Shared/Src/nanopb/pb_encode.o \
./Shared/Src/nanopb/pb_decode.o \
./Shared/Src/mbedtls/aes.o \
./Shared/Src/mbedtls/ccm.o \
./Shared/Src/mbedtls/sha256.o \
./Shared/Src/mbedtls/cipher.o \
./Shared/Src/mbedtls/cipher_wrap.o \
./Shared/Src/mbedtls/platform_util.o \
./Shared/Src/mbedtls/constant_time.o \
./Shared/Src/mbedtls/ecp.o \
./Shared/Src/mbedtls/ecp_curves.o \
./Shared/Src/mbedtls/bignum.o \
./Shared/Src/mbedtls/bignum_core.o \
./Shared/Src/mbedtls/bignum_mod.o \
./Shared/Src/mbedtls/bignum_mod_raw.o \
./Shared/Src/mbedtls/ecdh.o \
./Shared/Src/mbedtls/ecdsa.o \
./Shared/Src/mbedtls/asn1parse.o \
./Shared/Src/mbedtls/asn1write.o \
./Shared/Src/mbedtls/entropy.o \
./Shared/Src/mbedtls/entropy_poll.o \
./Shared/Src/mbedtls/entropy_hardware_alt.o \
./Shared/Src/mbedtls/ctr_drbg.o \
./Shared/Src/uECC.o \
./Shared/Src/mbedtls/md.o

CPP_DEPS += \
./Shared/Src/Observable.d \
./Shared/Src/CryptoEngine.d

C_DEPS += \
./Shared/Src/arcana_cmd.pb.d \
./Shared/Src/nanopb/pb_common.d \
./Shared/Src/nanopb/pb_encode.d \
./Shared/Src/nanopb/pb_decode.d \
./Shared/Src/mbedtls/aes.d \
./Shared/Src/mbedtls/ccm.d \
./Shared/Src/mbedtls/sha256.d \
./Shared/Src/mbedtls/cipher.d \
./Shared/Src/mbedtls/cipher_wrap.d \
./Shared/Src/mbedtls/platform_util.d \
./Shared/Src/mbedtls/constant_time.d \
./Shared/Src/mbedtls/ecdsa.d \
./Shared/Src/mbedtls/asn1parse.d \
./Shared/Src/mbedtls/asn1write.d

# Common ARM flags
ARM_FLAGS = -mcpu=cortex-m3 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xE -O2 \
  -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity \
  --specs=nano.specs -mfloat-abi=soft -mthumb

# All include paths (project + shared)
ALL_INCS = -I../Core/Inc -I../Services/Controller -I../Services/Service \
  -I../Services/Service/impl -I../Services/ViewModel -I../Services/View \
  -I../Services/Driver -I../Services/Model -I../Services/Common -I../Services/Command \
  $(SHARED_INCS) \
  -I../Middlewares/Third_Party/FatFs/src -I../Middlewares/Third_Party/FlashDB/inc \
  -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy \
  -I../Middlewares/Third_Party/FreeRTOS/Source/include \
  -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 \
  -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3 \
  -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include

# --- C++ build rules ---
Shared/Src/%.o: $(SHARED)/Src/%.cpp Shared/Src/subdir.mk
	arm-none-eabi-g++ "$<" -std=gnu++20 $(ARM_FLAGS) -c $(ALL_INCS) $(MBEDTLS_DEFS) \
	  -fno-exceptions -fno-rtti -fno-use-cxa-atexit \
	  -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

# --- C build rules (nanopb, mbedtls, proto) ---
Shared/Src/%.o: $(SHARED)/Src/%.c Shared/Src/subdir.mk
	arm-none-eabi-gcc "$<" -std=gnu11 $(ARM_FLAGS) -c $(ALL_INCS) $(MBEDTLS_DEFS) \
	  -Wno-unused-function \
	  -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

clean: clean-Shared-2f-Src

clean-Shared-2f-Src:
	-$(RM) ./Shared/Src/*.cyclo ./Shared/Src/*.d ./Shared/Src/*.o ./Shared/Src/*.su
	-$(RM) ./Shared/Src/nanopb/*.cyclo ./Shared/Src/nanopb/*.d ./Shared/Src/nanopb/*.o ./Shared/Src/nanopb/*.su
	-$(RM) ./Shared/Src/mbedtls/*.cyclo ./Shared/Src/mbedtls/*.d ./Shared/Src/mbedtls/*.o ./Shared/Src/mbedtls/*.su

.PHONY: clean-Shared-2f-Src
