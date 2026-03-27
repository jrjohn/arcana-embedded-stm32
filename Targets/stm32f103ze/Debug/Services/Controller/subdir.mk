################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Services/Controller/Controller.cpp \
../Services/Controller/F103App.cpp 

OBJS += \
./Services/Controller/Controller.o \
./Services/Controller/F103App.o 

CPP_DEPS += \
./Services/Controller/Controller.d \
./Services/Controller/F103App.d 


# Each subdirectory must supply rules for building sources it contributes
Services/Controller/%.o Services/Controller/%.su Services/Controller/%.cyclo: ../Services/Controller/%.cpp Services/Controller/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m3 -std=gnu++14 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xE -c -I../Core/Inc -I../Services/Controller -I../Services/Service -I../Services/Service/impl -I../Services/ViewModel -I../Services/View -I../Services/Driver -I../Services/Model -I../Services/Common -I../Services/Command -I../../../Shared/Inc -I../../../Shared/Inc/ats -I../../../Shared/Inc/display -I../../../Shared/Inc/nanopb -I../../../Shared/Inc/mbedtls -I../Middlewares/Third_Party/FatFs/src -I../Middlewares/Third_Party/FlashDB/inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3 -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O2 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fno-exceptions -fno-rtti -fno-use-cxa-atexit -std=gnu++20 -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Services-2f-Controller

clean-Services-2f-Controller:
	-$(RM) ./Services/Controller/Controller.cyclo ./Services/Controller/Controller.d ./Services/Controller/Controller.o ./Services/Controller/Controller.su ./Services/Controller/F103App.cyclo ./Services/Controller/F103App.d ./Services/Controller/F103App.o ./Services/Controller/F103App.su

.PHONY: clean-Services-2f-Controller

