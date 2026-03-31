################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Services/Service/impl/AtsStorageServiceImpl.cpp \
../Services/Service/impl/BleServiceImpl.cpp \
../Services/Service/impl/CommandBridgeImpl.cpp \
../Services/Service/impl/LcdServiceImpl.cpp \
../Services/Service/impl/LedServiceImpl.cpp \
../Services/Service/impl/LightServiceImpl.cpp \
../Services/Service/impl/MqttServiceImpl.cpp \
../Services/Service/impl/SdBenchmarkServiceImpl.cpp \
../Services/Service/impl/SensorServiceImpl.cpp \
../Services/Service/impl/TimerServiceImpl.cpp \
../Services/Service/impl/WifiServiceImpl.cpp 

OBJS += \
./Services/Service/impl/AtsStorageServiceImpl.o \
./Services/Service/impl/BleServiceImpl.o \
./Services/Service/impl/CommandBridgeImpl.o \
./Services/Service/impl/LcdServiceImpl.o \
./Services/Service/impl/LedServiceImpl.o \
./Services/Service/impl/LightServiceImpl.o \
./Services/Service/impl/MqttServiceImpl.o \
./Services/Service/impl/SdBenchmarkServiceImpl.o \
./Services/Service/impl/SensorServiceImpl.o \
./Services/Service/impl/TimerServiceImpl.o \
./Services/Service/impl/WifiServiceImpl.o 

CPP_DEPS += \
./Services/Service/impl/AtsStorageServiceImpl.d \
./Services/Service/impl/BleServiceImpl.d \
./Services/Service/impl/CommandBridgeImpl.d \
./Services/Service/impl/LcdServiceImpl.d \
./Services/Service/impl/LedServiceImpl.d \
./Services/Service/impl/LightServiceImpl.d \
./Services/Service/impl/MqttServiceImpl.d \
./Services/Service/impl/SdBenchmarkServiceImpl.d \
./Services/Service/impl/SensorServiceImpl.d \
./Services/Service/impl/TimerServiceImpl.d \
./Services/Service/impl/WifiServiceImpl.d 


# Each subdirectory must supply rules for building sources it contributes
Services/Service/impl/%.o Services/Service/impl/%.su Services/Service/impl/%.cyclo: ../Services/Service/impl/%.cpp Services/Service/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m3 -std=gnu++20 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xE -c -I../Core/Inc -I../Services/Controller -I../Services/Service -I../Services/Service/impl -I../Services/ViewModel -I../Services/View -I../Services/Driver -I../Services/Model -I../Services/Common -I../../../Shared/Inc -I../../../Shared/Inc/ats -I../../../Shared/Inc/display -I../Middlewares/Third_Party/FatFs/src -I../Middlewares/Third_Party/FlashDB/inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3 -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O2 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Services-2f-Service

clean-Services-2f-Service:
	-$(RM)../Services/Service/impl/AtsStorageServiceImpl.cyclo ./Services/Service/impl/AtsStorageServiceImpl.d ./Services/Service/AtsStorageServiceImpl.o ./Services/Service/AtsStorageServiceImpl.su ./Services/Service/LcdServiceImpl.cyclo ./Services/Service/LcdServiceImpl.d ./Services/Service/LcdServiceImpl.o ./Services/Service/LcdServiceImpl.su ./Services/Service/LedServiceImpl.cyclo ./Services/Service/LedServiceImpl.d ./Services/Service/LedServiceImpl.o ./Services/Service/LedServiceImpl.su ./Services/Service/LightServiceImpl.cyclo ./Services/Service/LightServiceImpl.d ./Services/Service/LightServiceImpl.o ./Services/Service/LightServiceImpl.su ./Services/Service/MqttServiceImpl.cyclo ./Services/Service/MqttServiceImpl.d ./Services/Service/MqttServiceImpl.o ./Services/Service/MqttServiceImpl.su ./Services/Service/SdBenchmarkServiceImpl.cyclo ./Services/Service/SdBenchmarkServiceImpl.d ./Services/Service/SdBenchmarkServiceImpl.o ./Services/Service/SdBenchmarkServiceImpl.su ./Services/Service/SdStorageServiceImpl.cyclo ./Services/Service/SdStorageServiceImpl.d ./Services/Service/SdStorageServiceImpl.o ./Services/Service/SdStorageServiceImpl.su ./Services/Service/SensorServiceImpl.cyclo ./Services/Service/SensorServiceImpl.d ./Services/Service/SensorServiceImpl.o ./Services/Service/SensorServiceImpl.su ./Services/Service/TimerServiceImpl.cyclo ./Services/Service/TimerServiceImpl.d ./Services/Service/TimerServiceImpl.o ./Services/Service/TimerServiceImpl.su ./Services/Service/WifiServiceImpl.cyclo ./Services/Service/WifiServiceImpl.d ./Services/Service/WifiServiceImpl.o ./Services/Service/WifiServiceImpl.su

.PHONY: clean-Services-2f-Service

