################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
/Users/jrjohn/Documents/projects/arcana-embedded-stm32/Shared/Src/ats/ArcanaTsDb.cpp 

OBJS += \
./Shared/Src/ats/ArcanaTsDb.o 

CPP_DEPS += \
./Shared/Src/ats/ArcanaTsDb.d 


# Each subdirectory must supply rules for building sources it contributes
Shared/Src/ats/ArcanaTsDb.o: /Users/jrjohn/Documents/projects/arcana-embedded-stm32/Shared/Src/ats/ArcanaTsDb.cpp Shared/Src/ats/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m3 -std=gnu++20 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xE -c -I../Core/Inc -I../Services/Controller -I../Services/Service -I../Services/Service/impl -I../Services/ViewModel -I../Services/View -I../Services/Driver -I../Services/Model -I../Services/Common -I../Services/Command -I../../../Shared/Inc -I../../../Shared/Inc/ats -I../../../Shared/Inc/display -I../Middlewares/Third_Party/FatFs/src -I../Middlewares/Third_Party/FlashDB/inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3 -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O2 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Shared-2f-Src-2f-ats

clean-Shared-2f-Src-2f-ats:
	-$(RM) ./Shared/Src/ats/ArcanaTsDb.cyclo ./Shared/Src/ats/ArcanaTsDb.d ./Shared/Src/ats/ArcanaTsDb.o ./Shared/Src/ats/ArcanaTsDb.su

.PHONY: clean-Shared-2f-Src-2f-ats

