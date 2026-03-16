################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables
CPP_SRCS += \
../Services/Ap3216cSensor.cpp \
../Services/AtsStorageServiceImpl.cpp \
../Services/Controller.cpp \
../Services/DhtSensor.cpp \
../Services/EcgDisplay.cpp \
../Services/Esp8266.cpp \
../Services/F103App.cpp \
../Services/FatFsFilePort.cpp \
../Services/I2cBus.cpp \
../Services/Ili9341Lcd.cpp \
../Services/LcdServiceImpl.cpp \
../Services/LedServiceImpl.cpp \
../Services/LightServiceImpl.cpp \
../Services/MainView.cpp \
../Services/Mpu6050Sensor.cpp \
../Services/MqttServiceImpl.cpp \
../Services/SdBenchmarkServiceImpl.cpp \
../Services/SdCard.cpp \
../Services/SdFalAdapter.cpp \
../Services/SdStorageServiceImpl.cpp \
../Services/SensorServiceImpl.cpp \
../Services/TimerServiceImpl.cpp \
../Services/WifiServiceImpl.cpp

OBJS += \
./Services/Ap3216cSensor.o \
./Services/AtsStorageServiceImpl.o \
./Services/Controller.o \
./Services/DhtSensor.o \
./Services/EcgDisplay.o \
./Services/Esp8266.o \
./Services/F103App.o \
./Services/FatFsFilePort.o \
./Services/I2cBus.o \
./Services/Ili9341Lcd.o \
./Services/LcdServiceImpl.o \
./Services/LedServiceImpl.o \
./Services/LightServiceImpl.o \
./Services/MainView.o \
./Services/Mpu6050Sensor.o \
./Services/MqttServiceImpl.o \
./Services/SdBenchmarkServiceImpl.o \
./Services/SdCard.o \
./Services/SdFalAdapter.o \
./Services/SdStorageServiceImpl.o \
./Services/SensorServiceImpl.o \
./Services/TimerServiceImpl.o \
./Services/WifiServiceImpl.o

CPP_DEPS += \
./Services/Ap3216cSensor.d \
./Services/AtsStorageServiceImpl.d \
./Services/Controller.d \
./Services/DhtSensor.d \
./Services/EcgDisplay.d \
./Services/Esp8266.d \
./Services/F103App.d \
./Services/FatFsFilePort.d \
./Services/I2cBus.d \
./Services/Ili9341Lcd.d \
./Services/LcdServiceImpl.d \
./Services/LedServiceImpl.d \
./Services/LightServiceImpl.d \
./Services/MainView.d \
./Services/Mpu6050Sensor.d \
./Services/MqttServiceImpl.d \
./Services/SdBenchmarkServiceImpl.d \
./Services/SdCard.d \
./Services/SdFalAdapter.d \
./Services/SdStorageServiceImpl.d \
./Services/SensorServiceImpl.d \
./Services/TimerServiceImpl.d \
./Services/WifiServiceImpl.d


# Each subdirectory must supply rules for building sources it contributes
Services/%.o Services/%.su Services/%.cyclo: ../Services/%.cpp Services/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m3 -std=gnu++14 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xE -c -I../Core/Inc -I../Services -I../../../Shared/Inc -I../../../Shared/Inc/ats -I../Middlewares/Third_Party/FatFs/src -I../Middlewares/Third_Party/FlashDB/inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3 -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O2 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Services

clean-Services:
	-$(RM) ./Services/Ap3216cSensor.cyclo ./Services/Ap3216cSensor.d ./Services/Ap3216cSensor.o ./Services/Ap3216cSensor.su ./Services/AtsStorageServiceImpl.cyclo ./Services/AtsStorageServiceImpl.d ./Services/AtsStorageServiceImpl.o ./Services/AtsStorageServiceImpl.su ./Services/Controller.cyclo ./Services/Controller.d ./Services/Controller.o ./Services/Controller.su ./Services/DhtSensor.cyclo ./Services/DhtSensor.d ./Services/DhtSensor.o ./Services/DhtSensor.su ./Services/EcgDisplay.cyclo ./Services/EcgDisplay.d ./Services/EcgDisplay.o ./Services/EcgDisplay.su ./Services/Esp8266.cyclo ./Services/Esp8266.d ./Services/Esp8266.o ./Services/Esp8266.su ./Services/F103App.cyclo ./Services/F103App.d ./Services/F103App.o ./Services/F103App.su ./Services/FatFsFilePort.cyclo ./Services/FatFsFilePort.d ./Services/FatFsFilePort.o ./Services/FatFsFilePort.su ./Services/I2cBus.cyclo ./Services/I2cBus.d ./Services/I2cBus.o ./Services/I2cBus.su ./Services/Ili9341Lcd.cyclo ./Services/Ili9341Lcd.d ./Services/Ili9341Lcd.o ./Services/Ili9341Lcd.su ./Services/LcdServiceImpl.cyclo ./Services/LcdServiceImpl.d ./Services/LcdServiceImpl.o ./Services/LcdServiceImpl.su ./Services/LedServiceImpl.cyclo ./Services/LedServiceImpl.d ./Services/LedServiceImpl.o ./Services/LedServiceImpl.su ./Services/LightServiceImpl.cyclo ./Services/LightServiceImpl.d ./Services/LightServiceImpl.o ./Services/LightServiceImpl.su ./Services/MainView.cyclo ./Services/MainView.d ./Services/MainView.o ./Services/MainView.su ./Services/Mpu6050Sensor.cyclo ./Services/Mpu6050Sensor.d ./Services/Mpu6050Sensor.o ./Services/Mpu6050Sensor.su ./Services/MqttServiceImpl.cyclo ./Services/MqttServiceImpl.d ./Services/MqttServiceImpl.o ./Services/MqttServiceImpl.su ./Services/SdBenchmarkServiceImpl.cyclo ./Services/SdBenchmarkServiceImpl.d ./Services/SdBenchmarkServiceImpl.o ./Services/SdBenchmarkServiceImpl.su ./Services/SdCard.cyclo ./Services/SdCard.d ./Services/SdCard.o ./Services/SdCard.su ./Services/SdFalAdapter.cyclo ./Services/SdFalAdapter.d ./Services/SdFalAdapter.o ./Services/SdFalAdapter.su ./Services/SdStorageServiceImpl.cyclo ./Services/SdStorageServiceImpl.d ./Services/SdStorageServiceImpl.o ./Services/SdStorageServiceImpl.su ./Services/SensorServiceImpl.cyclo ./Services/SensorServiceImpl.d ./Services/SensorServiceImpl.o ./Services/SensorServiceImpl.su ./Services/TimerServiceImpl.cyclo ./Services/TimerServiceImpl.d ./Services/TimerServiceImpl.o ./Services/TimerServiceImpl.su ./Services/WifiServiceImpl.cyclo ./Services/WifiServiceImpl.d ./Services/WifiServiceImpl.o ./Services/WifiServiceImpl.su

.PHONY: clean-Services
