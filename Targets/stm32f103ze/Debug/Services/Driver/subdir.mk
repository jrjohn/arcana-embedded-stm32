################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Services/Driver/Ap3216cSensor.cpp \
../Services/Driver/DhtSensor.cpp \
../Services/Driver/EcgDisplay.cpp \
../Services/Driver/Esp8266.cpp \
../Services/Driver/EspFlasher.cpp \
../Services/Driver/FatFsFilePort.cpp \
../Services/Driver/Hc08Ble.cpp \
../Services/Driver/I2cBus.cpp \
../Services/Driver/Ili9341Lcd.cpp \
../Services/Driver/Mpu6050Sensor.cpp \
../Services/Driver/SdCard.cpp \
../Services/Driver/SdFalAdapter.cpp 

OBJS += \
./Services/Driver/Ap3216cSensor.o \
./Services/Driver/DhtSensor.o \
./Services/Driver/EcgDisplay.o \
./Services/Driver/Esp8266.o \
./Services/Driver/EspFlasher.o \
./Services/Driver/FatFsFilePort.o \
./Services/Driver/Hc08Ble.o \
./Services/Driver/I2cBus.o \
./Services/Driver/Ili9341Lcd.o \
./Services/Driver/Mpu6050Sensor.o \
./Services/Driver/SdCard.o \
./Services/Driver/SdFalAdapter.o 

CPP_DEPS += \
./Services/Driver/Ap3216cSensor.d \
./Services/Driver/DhtSensor.d \
./Services/Driver/EcgDisplay.d \
./Services/Driver/Esp8266.d \
./Services/Driver/EspFlasher.d \
./Services/Driver/FatFsFilePort.d \
./Services/Driver/Hc08Ble.d \
./Services/Driver/I2cBus.d \
./Services/Driver/Ili9341Lcd.d \
./Services/Driver/Mpu6050Sensor.d \
./Services/Driver/SdCard.d \
./Services/Driver/SdFalAdapter.d 


# Each subdirectory must supply rules for building sources it contributes
Services/Driver/%.o Services/Driver/%.su Services/Driver/%.cyclo: ../Services/Driver/%.cpp Services/Driver/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m3 -std=gnu++14 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xE -c -I../Core/Inc -I../Services/Controller -I../Services/Service -I../Services/Service/impl -I../Services/ViewModel -I../Services/View -I../Services/Driver -I../Services/Model -I../Services/Common -I../Services/Command -I../../../Shared/Inc -I../../../Shared/Inc/ats -I../Middlewares/Third_Party/FatFs/src -I../Middlewares/Third_Party/FlashDB/inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3 -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O2 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Services-2f-Driver

clean-Services-2f-Driver:
	-$(RM) ./Services/Driver/Ap3216cSensor.cyclo ./Services/Driver/Ap3216cSensor.d ./Services/Driver/Ap3216cSensor.o ./Services/Driver/Ap3216cSensor.su ./Services/Driver/DhtSensor.cyclo ./Services/Driver/DhtSensor.d ./Services/Driver/DhtSensor.o ./Services/Driver/DhtSensor.su ./Services/Driver/EcgDisplay.cyclo ./Services/Driver/EcgDisplay.d ./Services/Driver/EcgDisplay.o ./Services/Driver/EcgDisplay.su ./Services/Driver/Esp8266.cyclo ./Services/Driver/Esp8266.d ./Services/Driver/Esp8266.o ./Services/Driver/Esp8266.su ./Services/Driver/FatFsFilePort.cyclo ./Services/Driver/FatFsFilePort.d ./Services/Driver/FatFsFilePort.o ./Services/Driver/FatFsFilePort.su ./Services/Driver/Hc08Ble.cyclo ./Services/Driver/Hc08Ble.d ./Services/Driver/Hc08Ble.o ./Services/Driver/Hc08Ble.su ./Services/Driver/I2cBus.cyclo ./Services/Driver/I2cBus.d ./Services/Driver/I2cBus.o ./Services/Driver/I2cBus.su ./Services/Driver/Ili9341Lcd.cyclo ./Services/Driver/Ili9341Lcd.d ./Services/Driver/Ili9341Lcd.o ./Services/Driver/Ili9341Lcd.su ./Services/Driver/Mpu6050Sensor.cyclo ./Services/Driver/Mpu6050Sensor.d ./Services/Driver/Mpu6050Sensor.o ./Services/Driver/Mpu6050Sensor.su ./Services/Driver/SdCard.cyclo ./Services/Driver/SdCard.d ./Services/Driver/SdCard.o ./Services/Driver/SdCard.su ./Services/Driver/SdFalAdapter.cyclo ./Services/Driver/SdFalAdapter.d ./Services/Driver/SdFalAdapter.o ./Services/Driver/SdFalAdapter.su

.PHONY: clean-Services-2f-Driver

