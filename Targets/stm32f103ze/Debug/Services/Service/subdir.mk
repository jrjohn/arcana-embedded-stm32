################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Services/Service/AtsStorageServiceImpl.cpp \
../Services/Service/LcdServiceImpl.cpp \
../Services/Service/LedServiceImpl.cpp \
../Services/Service/LightServiceImpl.cpp \
../Services/Service/MqttServiceImpl.cpp \
../Services/Service/SdBenchmarkServiceImpl.cpp \
../Services/Service/SdStorageServiceImpl.cpp \
../Services/Service/SensorServiceImpl.cpp \
../Services/Service/TimerServiceImpl.cpp \
../Services/Service/WifiServiceImpl.cpp 

OBJS += \
./Services/Service/AtsStorageServiceImpl.o \
./Services/Service/LcdServiceImpl.o \
./Services/Service/LedServiceImpl.o \
./Services/Service/LightServiceImpl.o \
./Services/Service/MqttServiceImpl.o \
./Services/Service/SdBenchmarkServiceImpl.o \
./Services/Service/SdStorageServiceImpl.o \
./Services/Service/SensorServiceImpl.o \
./Services/Service/TimerServiceImpl.o \
./Services/Service/WifiServiceImpl.o 

CPP_DEPS += \
./Services/Service/AtsStorageServiceImpl.d \
./Services/Service/LcdServiceImpl.d \
./Services/Service/LedServiceImpl.d \
./Services/Service/LightServiceImpl.d \
./Services/Service/MqttServiceImpl.d \
./Services/Service/SdBenchmarkServiceImpl.d \
./Services/Service/SdStorageServiceImpl.d \
./Services/Service/SensorServiceImpl.d \
./Services/Service/TimerServiceImpl.d \
./Services/Service/WifiServiceImpl.d 


# Each subdirectory must supply rules for building sources it contributes
Services/Service/%.o Services/Service/%.su Services/Service/%.cyclo: ../Services/Service/%.cpp Services/Service/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m3 -std=gnu++14 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xE -c -I../Core/Inc -I../Services/Controller -I../Services/Service -I../Services/Driver -I../Services/Model -I../Services/View -I../Services/ViewModel -I../Services/Common -I../../../Shared/Inc -I../../../Shared/Inc/ats -I../Middlewares/Third_Party/FatFs/src -I../Middlewares/Third_Party/FlashDB/inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3 -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O2 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Services-2f-Service

clean-Services-2f-Service:
	-$(RM) ./Services/Service/AtsStorageServiceImpl.cyclo ./Services/Service/AtsStorageServiceImpl.d ./Services/Service/AtsStorageServiceImpl.o ./Services/Service/AtsStorageServiceImpl.su ./Services/Service/LcdServiceImpl.cyclo ./Services/Service/LcdServiceImpl.d ./Services/Service/LcdServiceImpl.o ./Services/Service/LcdServiceImpl.su ./Services/Service/LedServiceImpl.cyclo ./Services/Service/LedServiceImpl.d ./Services/Service/LedServiceImpl.o ./Services/Service/LedServiceImpl.su ./Services/Service/LightServiceImpl.cyclo ./Services/Service/LightServiceImpl.d ./Services/Service/LightServiceImpl.o ./Services/Service/LightServiceImpl.su ./Services/Service/MqttServiceImpl.cyclo ./Services/Service/MqttServiceImpl.d ./Services/Service/MqttServiceImpl.o ./Services/Service/MqttServiceImpl.su ./Services/Service/SdBenchmarkServiceImpl.cyclo ./Services/Service/SdBenchmarkServiceImpl.d ./Services/Service/SdBenchmarkServiceImpl.o ./Services/Service/SdBenchmarkServiceImpl.su ./Services/Service/SdStorageServiceImpl.cyclo ./Services/Service/SdStorageServiceImpl.d ./Services/Service/SdStorageServiceImpl.o ./Services/Service/SdStorageServiceImpl.su ./Services/Service/SensorServiceImpl.cyclo ./Services/Service/SensorServiceImpl.d ./Services/Service/SensorServiceImpl.o ./Services/Service/SensorServiceImpl.su ./Services/Service/TimerServiceImpl.cyclo ./Services/Service/TimerServiceImpl.d ./Services/Service/TimerServiceImpl.o ./Services/Service/TimerServiceImpl.su ./Services/Service/WifiServiceImpl.cyclo ./Services/Service/WifiServiceImpl.d ./Services/Service/WifiServiceImpl.o ./Services/Service/WifiServiceImpl.su

.PHONY: clean-Services-2f-Service

