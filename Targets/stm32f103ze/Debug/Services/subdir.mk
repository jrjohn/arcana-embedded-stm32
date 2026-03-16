################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Common compiler flags for all Services subdirectories
SERVICES_INCLUDES = \
-I../Core/Inc \
-I../Services/Controller -I../Services/Service -I../Services/Service/impl -I../Services/Driver \
-I../Services/Model -I../Services/View -I../Services/ViewModel \
-I../Services/Common \
-I../../../Shared/Inc -I../../../Shared/Inc/ats \
-I../Middlewares/Third_Party/FatFs/src -I../Middlewares/Third_Party/FlashDB/inc \
-I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy \
-I../Middlewares/Third_Party/FreeRTOS/Source/include \
-I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 \
-I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3 \
-I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include

SERVICES_CXXFLAGS = -mcpu=cortex-m3 -std=gnu++14 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xE \
-O2 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit \
-Wall -fstack-usage -fcyclomatic-complexity --specs=nano.specs -mfloat-abi=soft -mthumb

# Controller
CPP_SRCS += \
../Services/Controller/Controller.cpp \
../Services/Controller/F103App.cpp

# Service
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

# Driver
CPP_SRCS += \
../Services/Driver/Ap3216cSensor.cpp \
../Services/Driver/DhtSensor.cpp \
../Services/Driver/EcgDisplay.cpp \
../Services/Driver/Esp8266.cpp \
../Services/Driver/FatFsFilePort.cpp \
../Services/Driver/I2cBus.cpp \
../Services/Driver/Ili9341Lcd.cpp \
../Services/Driver/Mpu6050Sensor.cpp \
../Services/Driver/SdCard.cpp \
../Services/Driver/SdFalAdapter.cpp

# View
CPP_SRCS += \
../Services/View/MainView.cpp

# Object files
OBJS += \
./Services/Controller/Controller.o \
./Services/Controller/F103App.o \
./Services/Service/AtsStorageServiceImpl.o \
./Services/Service/LcdServiceImpl.o \
./Services/Service/LedServiceImpl.o \
./Services/Service/LightServiceImpl.o \
./Services/Service/MqttServiceImpl.o \
./Services/Service/SdBenchmarkServiceImpl.o \
./Services/Service/SdStorageServiceImpl.o \
./Services/Service/SensorServiceImpl.o \
./Services/Service/TimerServiceImpl.o \
./Services/Service/WifiServiceImpl.o \
./Services/Driver/Ap3216cSensor.o \
./Services/Driver/DhtSensor.o \
./Services/Driver/EcgDisplay.o \
./Services/Driver/Esp8266.o \
./Services/Driver/FatFsFilePort.o \
./Services/Driver/I2cBus.o \
./Services/Driver/Ili9341Lcd.o \
./Services/Driver/Mpu6050Sensor.o \
./Services/Driver/SdCard.o \
./Services/Driver/SdFalAdapter.o \
./Services/View/MainView.o

# Dependency files
CPP_DEPS += \
./Services/Controller/Controller.d \
./Services/Controller/F103App.d \
./Services/Service/AtsStorageServiceImpl.d \
./Services/Service/LcdServiceImpl.d \
./Services/Service/LedServiceImpl.d \
./Services/Service/LightServiceImpl.d \
./Services/Service/MqttServiceImpl.d \
./Services/Service/SdBenchmarkServiceImpl.d \
./Services/Service/SdStorageServiceImpl.d \
./Services/Service/SensorServiceImpl.d \
./Services/Service/TimerServiceImpl.d \
./Services/Service/WifiServiceImpl.d \
./Services/Driver/Ap3216cSensor.d \
./Services/Driver/DhtSensor.d \
./Services/Driver/EcgDisplay.d \
./Services/Driver/Esp8266.d \
./Services/Driver/FatFsFilePort.d \
./Services/Driver/I2cBus.d \
./Services/Driver/Ili9341Lcd.d \
./Services/Driver/Mpu6050Sensor.d \
./Services/Driver/SdCard.d \
./Services/Driver/SdFalAdapter.d \
./Services/View/MainView.d


# Build rules — one per subdirectory
Services/Controller/%.o Services/Controller/%.su Services/Controller/%.cyclo: ../Services/Controller/%.cpp Services/subdir.mk
	@mkdir -p $(dir $@)
	arm-none-eabi-g++ "$<" -c $(SERVICES_CXXFLAGS) $(SERVICES_INCLUDES) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Services/Service/%.o Services/Service/%.su Services/Service/%.cyclo: ../Services/Service/%.cpp Services/subdir.mk
	@mkdir -p $(dir $@)
	arm-none-eabi-g++ "$<" -c $(SERVICES_CXXFLAGS) $(SERVICES_INCLUDES) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Services/Driver/%.o Services/Driver/%.su Services/Driver/%.cyclo: ../Services/Driver/%.cpp Services/subdir.mk
	@mkdir -p $(dir $@)
	arm-none-eabi-g++ "$<" -c $(SERVICES_CXXFLAGS) $(SERVICES_INCLUDES) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

Services/View/%.o Services/View/%.su Services/View/%.cyclo: ../Services/View/%.cpp Services/subdir.mk
	@mkdir -p $(dir $@)
	arm-none-eabi-g++ "$<" -c $(SERVICES_CXXFLAGS) $(SERVICES_INCLUDES) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@"

clean: clean-Services

clean-Services:
	-$(RM) -r ./Services/Controller ./Services/Service ./Services/Driver ./Services/View

.PHONY: clean-Services
