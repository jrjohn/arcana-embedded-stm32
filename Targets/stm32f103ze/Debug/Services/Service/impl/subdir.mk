################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Services/Service/impl/AtsStorageServiceImpl.cpp \
../Services/Service/impl/BleServiceImpl.cpp \
../Services/Service/impl/CommandBridge.cpp \
../Services/Service/impl/LcdServiceImpl.cpp \
../Services/Service/impl/LedServiceImpl.cpp \
../Services/Service/impl/LightServiceImpl.cpp \
../Services/Service/impl/MqttServiceImpl.cpp \
../Services/Service/impl/SdBenchmarkServiceImpl.cpp \
../Services/Service/impl/SdStorageServiceImpl.cpp \
../Services/Service/impl/SensorServiceImpl.cpp \
../Services/Service/impl/TimerServiceImpl.cpp \
../Services/Service/impl/WifiServiceImpl.cpp 

OBJS += \
./Services/Service/impl/AtsStorageServiceImpl.o \
./Services/Service/impl/BleServiceImpl.o \
./Services/Service/impl/CommandBridge.o \
./Services/Service/impl/LcdServiceImpl.o \
./Services/Service/impl/LedServiceImpl.o \
./Services/Service/impl/LightServiceImpl.o \
./Services/Service/impl/MqttServiceImpl.o \
./Services/Service/impl/SdBenchmarkServiceImpl.o \
./Services/Service/impl/SdStorageServiceImpl.o \
./Services/Service/impl/SensorServiceImpl.o \
./Services/Service/impl/TimerServiceImpl.o \
./Services/Service/impl/WifiServiceImpl.o 

CPP_DEPS += \
./Services/Service/impl/AtsStorageServiceImpl.d \
./Services/Service/impl/BleServiceImpl.d \
./Services/Service/impl/CommandBridge.d \
./Services/Service/impl/LcdServiceImpl.d \
./Services/Service/impl/LedServiceImpl.d \
./Services/Service/impl/LightServiceImpl.d \
./Services/Service/impl/MqttServiceImpl.d \
./Services/Service/impl/SdBenchmarkServiceImpl.d \
./Services/Service/impl/SdStorageServiceImpl.d \
./Services/Service/impl/SensorServiceImpl.d \
./Services/Service/impl/TimerServiceImpl.d \
./Services/Service/impl/WifiServiceImpl.d 


# Each subdirectory must supply rules for building sources it contributes
Services/Service/impl/%.o Services/Service/impl/%.su Services/Service/impl/%.cyclo: ../Services/Service/impl/%.cpp Services/Service/impl/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m3 -std=gnu++14 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xE -c -I../Core/Inc -I../Services/Controller -I../Services/Service -I../Services/Service/impl -I../Services/ViewModel -I../Services/View -I../Services/Driver -I../Services/Model -I../Services/Common -I../Services/Command -I../../../Shared/Inc -I../../../Shared/Inc/ats -I../Middlewares/Third_Party/FatFs/src -I../Middlewares/Third_Party/FlashDB/inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3 -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O2 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Services-2f-Service-2f-impl

clean-Services-2f-Service-2f-impl:
	-$(RM) ./Services/Service/impl/AtsStorageServiceImpl.cyclo ./Services/Service/impl/AtsStorageServiceImpl.d ./Services/Service/impl/AtsStorageServiceImpl.o ./Services/Service/impl/AtsStorageServiceImpl.su ./Services/Service/impl/BleServiceImpl.cyclo ./Services/Service/impl/BleServiceImpl.d ./Services/Service/impl/BleServiceImpl.o ./Services/Service/impl/BleServiceImpl.su ./Services/Service/impl/CommandBridge.cyclo ./Services/Service/impl/CommandBridge.d ./Services/Service/impl/CommandBridge.o ./Services/Service/impl/CommandBridge.su ./Services/Service/impl/LcdServiceImpl.cyclo ./Services/Service/impl/LcdServiceImpl.d ./Services/Service/impl/LcdServiceImpl.o ./Services/Service/impl/LcdServiceImpl.su ./Services/Service/impl/LedServiceImpl.cyclo ./Services/Service/impl/LedServiceImpl.d ./Services/Service/impl/LedServiceImpl.o ./Services/Service/impl/LedServiceImpl.su ./Services/Service/impl/LightServiceImpl.cyclo ./Services/Service/impl/LightServiceImpl.d ./Services/Service/impl/LightServiceImpl.o ./Services/Service/impl/LightServiceImpl.su ./Services/Service/impl/MqttServiceImpl.cyclo ./Services/Service/impl/MqttServiceImpl.d ./Services/Service/impl/MqttServiceImpl.o ./Services/Service/impl/MqttServiceImpl.su ./Services/Service/impl/SdBenchmarkServiceImpl.cyclo ./Services/Service/impl/SdBenchmarkServiceImpl.d ./Services/Service/impl/SdBenchmarkServiceImpl.o ./Services/Service/impl/SdBenchmarkServiceImpl.su ./Services/Service/impl/SdStorageServiceImpl.cyclo ./Services/Service/impl/SdStorageServiceImpl.d ./Services/Service/impl/SdStorageServiceImpl.o ./Services/Service/impl/SdStorageServiceImpl.su ./Services/Service/impl/SensorServiceImpl.cyclo ./Services/Service/impl/SensorServiceImpl.d ./Services/Service/impl/SensorServiceImpl.o ./Services/Service/impl/SensorServiceImpl.su ./Services/Service/impl/TimerServiceImpl.cyclo ./Services/Service/impl/TimerServiceImpl.d ./Services/Service/impl/TimerServiceImpl.o ./Services/Service/impl/TimerServiceImpl.su ./Services/Service/impl/WifiServiceImpl.cyclo ./Services/Service/impl/WifiServiceImpl.d ./Services/Service/impl/WifiServiceImpl.o ./Services/Service/impl/WifiServiceImpl.su

.PHONY: clean-Services-2f-Service-2f-impl

