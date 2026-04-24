cmake_minimum_required(VERSION 3.14)
project(arcana_stm32_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Coverage flags ────────────────────────────────────────────────────────────
add_compile_options(-O0 -g --coverage -fprofile-arcs -ftest-coverage)
add_link_options(--coverage)

# ── FetchContent: Google Test ─────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── Paths ─────────────────────────────────────────────────────────────────────
set(SHARED_INC    ${CMAKE_SOURCE_DIR}/../Shared/Inc)
set(SHARED_SRC    ${CMAKE_SOURCE_DIR}/../Shared/Src)
set(SHARED_CORE_EVENT    ${SHARED_SRC}/core/event)
set(SHARED_CMD_CODEC     ${SHARED_SRC}/command/codec)
set(SHARED_CMD_SECURITY  ${SHARED_SRC}/command/security)
set(ATS_INC       ${SHARED_INC}/db/arcanats/ats)
set(ATS_INC_PFX   ${SHARED_INC}/db/arcanats)
set(ATS_SRC       ${SHARED_SRC}/db/arcanats/ats)
set(F103_COMMON   ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Main/core)
set(F103_CORE     ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Main/core)
set(F103_SVC_ROOT ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Main)
set(F051_CMD      ${CMAKE_SOURCE_DIR}/../Targets/stm32f051c8/Main/command)
set(F051_CMD_CODEC ${F051_CMD}/codec)
set(F051_CMD_CMDS ${F051_CMD}/commands)
set(F051_SVC      ${CMAKE_SOURCE_DIR}/../Targets/stm32f051c8/Main/service)
set(F103_SVC_IMPL ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Main/service/impl)
set(F103_SVC_INC  ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Main/service)
set(F103_CMD_INC  ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Main/command)
set(F103_DRV      ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Main/driver)
set(F103_TRANSPORT_BLE   ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Main/transport/ble)
set(F103_TRANSPORT_WIFI  ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Main/transport/wifi)
set(F103_MODEL    ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Main/core/model)
set(NANOPB_INC    ${SHARED_INC}/nanopb)
set(NANOPB_SRC    ${SHARED_SRC}/nanopb)
set(MOCKS_DIR     ${CMAKE_SOURCE_DIR}/mocks)
set(MBEDTLS_INC   ${SHARED_INC})            # mbedtls headers live at Shared/Inc/mbedtls/*
set(MBEDTLS_INC2  ${SHARED_INC}/mbedtls)    # bare `#include "common.h"` etc.
set(MBEDTLS_SRC   ${SHARED_SRC}/mbedtls)

# mbedtls source subset needed for CCM/SHA256/HMAC/HKDF + ECDH P-256 + DRBG.
# Excludes the STM32-specific entropy_hardware_alt.c — host stub provided in
# Tests/mocks/mbedtls_host_entropy.c instead.
set(MBEDTLS_SOURCES
    ${MBEDTLS_SRC}/aes.c
    ${MBEDTLS_SRC}/ccm.c
    ${MBEDTLS_SRC}/sha256.c
    ${MBEDTLS_SRC}/md.c
    ${MBEDTLS_SRC}/cipher.c
    ${MBEDTLS_SRC}/cipher_wrap.c
    ${MBEDTLS_SRC}/constant_time.c
    ${MBEDTLS_SRC}/platform_util.c
    ${MBEDTLS_SRC}/bignum.c
    ${MBEDTLS_SRC}/bignum_core.c
    ${MBEDTLS_SRC}/bignum_mod.c
    ${MBEDTLS_SRC}/bignum_mod_raw.c
    ${MBEDTLS_SRC}/ecp.c
    ${MBEDTLS_SRC}/ecp_curves.c
    ${MBEDTLS_SRC}/ecdh.c
    ${MBEDTLS_SRC}/asn1parse.c
    ${MBEDTLS_SRC}/asn1write.c
    ${MBEDTLS_SRC}/ecdsa.c
    ${MBEDTLS_SRC}/entropy.c
    ${MBEDTLS_SRC}/entropy_poll.c
    ${MBEDTLS_SRC}/ctr_drbg.c
    ${MOCKS_DIR}/mbedtls_host_entropy.c
)

# Shared stubs for all targets needing FreeRTOS
set(FREERTOS_STUBS ${MOCKS_DIR}/freertos_stubs.cpp)

# Common include dirs (mocks FIRST to override any system FreeRTOS headers)
set(COMMON_INCS
    ${MOCKS_DIR}
    ${SHARED_INC}
    ${SHARED_INC}/core/event
    ${SHARED_INC}/core/log
    ${SHARED_INC}/core/model
    ${SHARED_INC}/core/validation
    ${SHARED_INC}/core/types
    ${SHARED_INC}/command
    ${SHARED_INC}/command/codec
    ${SHARED_INC}/command/security
    ${SHARED_INC}/db/arcanats
    ${SHARED_INC}/db/arcanats/ats
    ${SHARED_INC}/view
)

# ── test_crc16 ────────────────────────────────────────────────────────────────
add_executable(test_crc16 test_crc16.cpp)
target_include_directories(test_crc16 PRIVATE ${COMMON_INCS})
target_link_libraries(test_crc16 PRIVATE GTest::gtest_main)

# ── test_frame_codec ──────────────────────────────────────────────────────────
add_executable(test_frame_codec test_frame_codec.cpp)
target_include_directories(test_frame_codec PRIVATE ${COMMON_INCS})
target_link_libraries(test_frame_codec PRIVATE GTest::gtest_main)

# ── test_command_codec ────────────────────────────────────────────────────────
add_executable(test_command_codec
    test_command_codec.cpp
    ${F051_CMD_CODEC}/CommandCodec.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_command_codec PRIVATE ${COMMON_INCS} ${F051_CMD} ${F051_CMD_CODEC})
target_link_libraries(test_command_codec PRIVATE GTest::gtest_main)

# ── test_registry ─────────────────────────────────────────────────────────────
add_executable(test_registry
    test_registry.cpp
    ${F051_CMD}/CommandRegistry.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_registry PRIVATE ${COMMON_INCS} ${F051_CMD} ${F051_CMD_CODEC})
target_link_libraries(test_registry PRIVATE GTest::gtest_main)

# ── test_models ───────────────────────────────────────────────────────────────
add_executable(test_models
    test_models.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_models PRIVATE ${COMMON_INCS})
target_link_libraries(test_models PRIVATE GTest::gtest_main)

# ── test_services (CounterService + TimeDisplayService) ───────────────────────
add_executable(test_services
    test_services.cpp
    ${F051_SVC}/CounterService.cpp
    ${F051_SVC}/TimeDisplayService.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_services PRIVATE
    ${COMMON_INCS} ${F051_SVC} ${F051_CMD})
target_link_libraries(test_services PRIVATE GTest::gtest_main)

# ── test_dispatcher (CommandDispatcher + CommandService) ──────────────────────
add_executable(test_dispatcher
    test_dispatcher.cpp
    ${F051_CMD_CODEC}/CommandCodec.cpp
    ${F051_CMD}/CommandRegistry.cpp
    ${F051_CMD}/CommandDispatcher.cpp
    ${F051_CMD}/CommandService.cpp
    ${F051_SVC}/CounterService.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_dispatcher PRIVATE
    ${COMMON_INCS} ${F051_CMD} ${F051_CMD_CODEC} ${F051_CMD_CMDS} ${F051_SVC})
target_link_libraries(test_dispatcher PRIVATE GTest::gtest_main)

# ── test_commands (PingCommand + GetCounterCommand) ───────────────────────────
add_executable(test_commands
    test_commands.cpp
    ${F051_SVC}/CounterService.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_commands PRIVATE
    ${COMMON_INCS} ${F051_CMD} ${F051_SVC}
    ${F051_CMD_CMDS})
target_link_libraries(test_commands PRIVATE GTest::gtest_main)

# ── test_timer_service ────────────────────────────────────────────────────────
add_executable(test_timer_service
    test_timer_service.cpp
    ${F051_SVC}/TimerService.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_timer_service PRIVATE ${COMMON_INCS} ${F051_SVC})
target_link_libraries(test_timer_service PRIVATE GTest::gtest_main)

# ── test_crc32 ───────────────────────────────────────────────────────────────
add_executable(test_crc32 test_crc32.cpp)
target_include_directories(test_crc32 PRIVATE ${COMMON_INCS})
target_link_libraries(test_crc32 PRIVATE GTest::gtest_main)

# ── test_frame_assembler ─────────────────────────────────────────────────────
add_executable(test_frame_assembler test_frame_assembler.cpp)
target_include_directories(test_frame_assembler PRIVATE ${COMMON_INCS})
target_link_libraries(test_frame_assembler PRIVATE GTest::gtest_main)

# ── test_log ─────────────────────────────────────────────────────────────────
add_executable(test_log test_log.cpp ${FREERTOS_STUBS})
target_include_directories(test_log PRIVATE ${COMMON_INCS} ${SHARED_INC}/ats)
target_link_libraries(test_log PRIVATE GTest::gtest_main)

# ── test_ota_header ──────────────────────────────────────────────────────────
add_executable(test_ota_header test_ota_header.cpp)
target_include_directories(test_ota_header PRIVATE ${COMMON_INCS})
target_link_libraries(test_ota_header PRIVATE GTest::gtest_main)

# ── test_observable (extra coverage) ─────────────────────────────────────────
add_executable(test_observable
    test_observable.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_observable PRIVATE ${COMMON_INCS})
target_link_libraries(test_observable PRIVATE GTest::gtest_main)

# ── test_observable_errors (separate exe: tests queue-null + queue-full) ──────
# Uses its own FreeRTOS stubs (inline in .cpp) with controllable xQueueSend
add_executable(test_observable_errors
    test_observable_errors.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
)
target_include_directories(test_observable_errors PRIVATE ${COMMON_INCS})
target_link_libraries(test_observable_errors PRIVATE GTest::gtest_main)

# ── test_sha256 (header-only Sha256 + HMAC + HKDF) ────────────────────────────
add_executable(test_sha256 test_sha256.cpp)
target_include_directories(test_sha256 PRIVATE ${COMMON_INCS})
target_link_libraries(test_sha256 PRIVATE GTest::gtest_main)

# ── test_chacha20 (RFC 7539 KAT) ──────────────────────────────────────────────
add_executable(test_chacha20 test_chacha20.cpp)
target_include_directories(test_chacha20 PRIVATE ${COMMON_INCS} ${F103_COMMON})
target_link_libraries(test_chacha20 PRIVATE GTest::gtest_main)

# ── test_crypto_engine (CryptoEngine + real mbedtls CCM/SHA256) ───────────────
add_executable(test_crypto_engine
    test_crypto_engine.cpp
    ${SHARED_CMD_SECURITY}/CryptoEngine.cpp
    ${MBEDTLS_SOURCES}
)
target_include_directories(test_crypto_engine PRIVATE ${COMMON_INCS} ${MBEDTLS_INC} ${MBEDTLS_INC2})
target_link_libraries(test_crypto_engine PRIVATE GTest::gtest_main)

# ── test_key_exchange (KeyExchangeManager + ECDH P-256 over real mbedtls) ─────
add_executable(test_key_exchange
    test_key_exchange.cpp
    ${SHARED_CMD_SECURITY}/KeyExchangeManager.cpp
    ${SHARED_CMD_SECURITY}/CryptoEngine.cpp
    ${FREERTOS_STUBS}
    ${MBEDTLS_SOURCES}
)
target_include_directories(test_key_exchange PRIVATE ${COMMON_INCS} ${MBEDTLS_INC} ${MBEDTLS_INC2})
target_link_libraries(test_key_exchange PRIVATE GTest::gtest_main)

# ── test_registration (RegistrationServiceImpl crypto + storage + protobuf) ──
# Note: FatFsFilePort.cpp is linked despite no test calling its methods —
# the AtsStorageServiceImpl stub ctor instantiates mFilePort/mDeviceFilePort,
# which need the FatFsFilePort vtable, which is emitted in FatFsFilePort.cpp.
add_executable(test_registration
    test_registration.cpp
    ${F103_SVC_IMPL}/RegistrationServiceImpl.cpp
    ${F103_DRV}/FatFsFilePort.cpp
    ${ATS_SRC}/ArcanaTsDb.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${SHARED_SRC}/uECC.c
    ${SHARED_CMD_CODEC}/registration.pb.c
    ${NANOPB_SRC}/pb_encode.c
    ${NANOPB_SRC}/pb_decode.c
    ${NANOPB_SRC}/pb_common.c
    ${MBEDTLS_SOURCES}
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/ff_host_stub.cpp
    ${MOCKS_DIR}/AtsStorageServiceImpl_stub.cpp
    ${MOCKS_DIR}/Esp8266_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_registration PRIVATE
    ${COMMON_INCS} ${ATS_INC} ${MBEDTLS_INC} ${MBEDTLS_INC2}
    ${NANOPB_INC}
    ${F103_COMMON} ${F103_SVC_INC} ${F103_SVC_IMPL} ${F103_CMD_INC}
    ${F103_DRV} ${F103_MODEL})
target_link_libraries(test_registration PRIVATE GTest::gtest_main)

# ── test_atsstorage (real AtsStorageServiceImpl business logic) ──────────────
# Compiles the production AtsStorageServiceImpl.cpp against host stubs of
# IoServiceImpl, SdBenchmarkServiceImpl, SystemClock, DisplayStatus,
# DeviceKey, FreeRTOS task/queue/sem, FATFS, HAL. The mocks/ -I path
# overrides production headers where the same-directory rule allows it.
# storageTask + taskLoop are infinite HW polling loops and remain uncov;
# this target focuses on the testable business-logic functions
# (loadCredentials/saveCredentials, loadTzConfig/saveTzConfig, queryByDate,
# listPendingUploads/isDateUploaded/markUploaded, writeLifecycleEvent,
# writeRecoveryEvents, openDailyDb, openDeviceDb, restoreTimeFromDeviceDb,
# initDeviceDbChannels).
add_executable(test_atsstorage
    test_atsstorage.cpp
    ${F103_SVC_IMPL}/AtsStorageServiceImpl.cpp
    ${F103_DRV}/FatFsFilePort.cpp
    ${ATS_SRC}/ArcanaTsDb.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/ff_host_stub.cpp
    ${MOCKS_DIR}/atsstorage_host_stubs.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_atsstorage PRIVATE
    ${COMMON_INCS} ${ATS_INC} ${SHARED_INC}/view
    ${F103_COMMON} ${F103_SVC_INC} ${F103_SVC_IMPL} ${F103_CMD_INC}
    ${F103_DRV} ${F103_MODEL})
target_link_libraries(test_atsstorage PRIVATE GTest::gtest_main)

# ── test_httpupload (HttpUploadServiceImpl HTTPS + retry + streaming) ────────
# Links the REAL AtsStorageServiceImpl.cpp + RegistrationServiceImpl.cpp +
# HttpUploadServiceImpl.cpp against the same heavy stub set as test_atsstorage.
# Reuses Esp8266 mock for AT-command sequencing and the in-memory FatFs for
# file streaming.
add_executable(test_httpupload
    test_httpupload.cpp
    ${F103_SVC_IMPL}/AtsStorageServiceImpl.cpp
    ${F103_SVC_IMPL}/RegistrationServiceImpl.cpp
    ${F103_SVC_IMPL}/HttpUploadServiceImpl.cpp
    ${F103_DRV}/FatFsFilePort.cpp
    ${ATS_SRC}/ArcanaTsDb.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${SHARED_SRC}/uECC.c
    ${SHARED_CMD_CODEC}/registration.pb.c
    ${NANOPB_SRC}/pb_encode.c
    ${NANOPB_SRC}/pb_decode.c
    ${NANOPB_SRC}/pb_common.c
    ${MBEDTLS_SOURCES}
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/ff_host_stub.cpp
    ${MOCKS_DIR}/atsstorage_host_stubs.cpp
    ${MOCKS_DIR}/Esp8266_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_httpupload PRIVATE
    ${COMMON_INCS} ${ATS_INC} ${SHARED_INC}/view
    ${MBEDTLS_INC} ${MBEDTLS_INC2} ${NANOPB_INC}
    ${F103_COMMON} ${F103_SVC_INC} ${F103_SVC_IMPL} ${F103_CMD_INC}
    ${F103_DRV} ${F103_MODEL})
target_link_libraries(test_httpupload PRIVATE GTest::gtest_main)

# ── test_ota (OtaServiceImpl HTTP download + CRC + meta + flag) ──────────────
add_executable(test_ota
    test_ota.cpp
    ${F103_SVC_IMPL}/OtaServiceImpl.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/ff_host_stub.cpp
    ${MOCKS_DIR}/Esp8266_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_ota PRIVATE
    ${COMMON_INCS} ${F103_COMMON} ${F103_SVC_INC} ${F103_SVC_IMPL}
    ${F103_DRV})
target_link_libraries(test_ota PRIVATE GTest::gtest_main)

# ── test_mqtt (MqttServiceImpl raw MQTT 3.1.1 over Esp8266 SSL) ──────────────
# Links the full kitchen sink: real Mqtt + Wifi + AtsStorage + HttpUpload +
# Registration + CommandBridge + uECC + nanopb + mbedtls + FatFsFilePort.
add_executable(test_mqtt
    test_mqtt.cpp
    ${F103_SVC_IMPL}/MqttServiceImpl.cpp
    ${F103_SVC_IMPL}/WifiServiceImpl.cpp
    ${F103_SVC_IMPL}/AtsStorageServiceImpl.cpp
    ${F103_SVC_IMPL}/HttpUploadServiceImpl.cpp
    ${F103_SVC_IMPL}/RegistrationServiceImpl.cpp
    ${F103_SVC_IMPL}/CommandBridgeImpl.cpp
    ${F103_DRV}/FatFsFilePort.cpp
    ${ATS_SRC}/ArcanaTsDb.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${SHARED_SRC}/uECC.c
    ${SHARED_CMD_CODEC}/registration.pb.c
    ${NANOPB_SRC}/pb_encode.c
    ${NANOPB_SRC}/pb_decode.c
    ${NANOPB_SRC}/pb_common.c
    ${MBEDTLS_SOURCES}
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/ff_host_stub.cpp
    ${MOCKS_DIR}/atsstorage_host_stubs.cpp
    ${MOCKS_DIR}/Esp8266_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_mqtt PRIVATE
    ${COMMON_INCS} ${ATS_INC} ${SHARED_INC}/view
    ${MBEDTLS_INC} ${MBEDTLS_INC2} ${NANOPB_INC}
    ${F103_COMMON} ${F103_SVC_INC} ${F103_SVC_IMPL} ${F103_CMD_INC}
    ${F103_DRV} ${F103_MODEL})
target_link_libraries(test_mqtt PRIVATE GTest::gtest_main)

# ── test_wifi (WifiServiceImpl AT-command sequencing) ────────────────────────
add_executable(test_wifi
    test_wifi.cpp
    ${F103_SVC_IMPL}/WifiServiceImpl.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/Esp8266_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_wifi PRIVATE
    ${COMMON_INCS} ${F103_COMMON} ${F103_SVC_INC} ${F103_SVC_IMPL}
    ${F103_DRV} ${F103_MODEL})
target_link_libraries(test_wifi PRIVATE GTest::gtest_main)

# ── test_main_view (MainView render functions against StubDisplay) ───────────
add_executable(test_main_view
    test_main_view.cpp
    ${F103_SVC_ROOT}/view/main/MainView.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/display_stubs.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_main_view PRIVATE
    ${COMMON_INCS}
    ${F103_COMMON} ${F103_MODEL}
    ${F103_SVC_ROOT}/view
    ${F103_SVC_ROOT}/view/main)
target_link_libraries(test_main_view PRIVATE GTest::gtest_main)

# ── test_lcd_viewmodel (MainViewModel observable wiring + dirty flags) ───────
add_executable(test_lcd_viewmodel
    test_lcd_viewmodel.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_lcd_viewmodel PRIVATE
    ${COMMON_INCS} ${F103_COMMON} ${F103_MODEL}
    ${F103_SVC_ROOT}/view
    ${F103_SVC_ROOT}/view/main)
target_link_libraries(test_lcd_viewmodel PRIVATE GTest::gtest_main)

# ── test_widgets (FormWidgets + DialogWidgets + Widget + WidgetGroup) ────────
add_executable(test_widgets test_widgets.cpp)
target_include_directories(test_widgets PRIVATE ${COMMON_INCS})
target_link_libraries(test_widgets PRIVATE GTest::gtest_main)

# ── test_mqtt_packet (MqttPacket header-only build/parse) ────────────────────
add_executable(test_mqtt_packet
    test_mqtt_packet.cpp
)
target_include_directories(test_mqtt_packet PRIVATE
    ${COMMON_INCS} ${F103_COMMON})
target_link_libraries(test_mqtt_packet PRIVATE GTest::gtest_main)

# ── test_command_bridge (CommandBridgeImpl crypto + dispatch) ────────────────
add_executable(test_command_bridge
    test_command_bridge.cpp
    ${F103_SVC_IMPL}/CommandBridgeImpl.cpp
    ${SHARED_SRC}/uECC.c
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_command_bridge PRIVATE
    ${COMMON_INCS} ${F103_COMMON} ${F103_SVC_INC} ${F103_CMD_INC})
target_link_libraries(test_command_bridge PRIVATE GTest::gtest_main)

# ── test_ble (BleServiceImpl + Hc08Ble mock + CommandBridge) ─────────────────
# BleServiceImpl pulls in CommandBridgeImpl (real, with uECC) and a stub
# RegistrationServiceImpl (we only need getCommKey() returning a fake key —
# the real registration handshake is far too heavy for a focused BLE test).
add_executable(test_ble
    test_ble.cpp
    ${F103_SVC_IMPL}/BleServiceImpl.cpp
    ${F103_SVC_IMPL}/CommandBridgeImpl.cpp
    ${SHARED_SRC}/uECC.c
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/Hc08Ble_stub.cpp
    ${MOCKS_DIR}/RegistrationServiceImpl_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_ble PRIVATE
    ${COMMON_INCS} ${F103_COMMON} ${F103_SVC_INC} ${F103_SVC_IMPL}
    ${F103_CMD_INC} ${F103_DRV} ${F103_MODEL})
target_link_libraries(test_ble PRIVATE GTest::gtest_main)

# ── test_commands_f103 (header-only ICommand impls in Commands.hpp) ──────────
add_executable(test_commands_f103
    test_commands_f103.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_commands_f103 PRIVATE
    ${COMMON_INCS} ${F103_COMMON} ${F103_CMD_INC})
target_link_libraries(test_commands_f103 PRIVATE GTest::gtest_main)

# ── test_i2c_drivers (production I2cBus + Mpu6050 + Ap3216c + DhtSensor) ────
# Software bit-bang drivers — link real .cpp files via include order
# (F103_DRV before MOCKS_DIR). delayUs spins on DWT->CYCCNT which never
# advances on host; tests set SystemCoreClock=0 to make `cycles = us * 0 = 0`
# so the wait loop terminates immediately.
add_executable(test_i2c_drivers
    test_i2c_drivers.cpp
    ${F103_DRV}/I2cBus.cpp
    ${F103_DRV}/Mpu6050Sensor.cpp
    ${F103_DRV}/Ap3216cSensor.cpp
    ${F103_DRV}/DhtSensor.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_i2c_drivers PRIVATE
    ${F103_DRV} ${MOCKS_DIR} ${SHARED_INC} ${F103_COMMON})
target_link_libraries(test_i2c_drivers PRIVATE GTest::gtest_main)

# ── test_esp_flasher (production EspFlasher SLIP bootloader driver) ─────────
add_executable(test_esp_flasher
    test_esp_flasher.cpp
    ${F103_TRANSPORT_WIFI}/EspFlasher.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/ff_host_stub.cpp
    ${FREERTOS_STUBS}
)
target_compile_definitions(test_esp_flasher PRIVATE g_exfat_ready_storage=1)
target_include_directories(test_esp_flasher PRIVATE
    ${F103_DRV} ${F103_TRANSPORT_WIFI} ${COMMON_INCS} ${F103_COMMON})
target_link_libraries(test_esp_flasher PRIVATE GTest::gtest_main)

# ── test_esp8266_driver (production Esp8266 UART driver) ────────────────────
# Same pattern as test_hc08ble_driver: link real Esp8266.cpp via include
# order (F103_DRV before MOCKS_DIR).
add_executable(test_esp8266_driver
    test_esp8266_driver.cpp
    ${F103_TRANSPORT_WIFI}/Esp8266.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_esp8266_driver PRIVATE
    ${F103_DRV} ${F103_TRANSPORT_WIFI} ${COMMON_INCS} ${F103_COMMON})
target_link_libraries(test_esp8266_driver PRIVATE GTest::gtest_main)

# ── test_hc08ble_driver (production Hc08Ble UART driver) ────────────────────
# Pulls in the REAL Hc08Ble.cpp by putting F103_DRV BEFORE MOCKS_DIR in the
# include order so quoted include resolves to the production header instead
# of the host stub. UART HAL stubs in test_hal_stub return HAL_OK.
add_executable(test_hc08ble_driver
    test_hc08ble_driver.cpp
    ${F103_TRANSPORT_BLE}/Hc08Ble.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_hc08ble_driver PRIVATE
    ${F103_DRV} ${F103_TRANSPORT_BLE} ${COMMON_INCS} ${F103_COMMON})
target_link_libraries(test_hc08ble_driver PRIVATE GTest::gtest_main)

# ── test_app_entry (F051 App.cpp entry points) ──────────────────────────────
# F051 App.cpp's extern "C" symbols are renamed to F051_* via macro rewrite
# in test_app_f051_wrap.cpp so the test target can link them without
# colliding with any other App.cpp version. F103 App.cpp is intentionally
# not covered — its App_Init cascades into Controller::run() (entire HAL).
add_executable(test_app_entry
    test_app_entry.cpp
    test_app_f051_wrap.cpp
    ${F051_CMD}/CommandService.cpp
    ${F051_CMD}/CommandRegistry.cpp
    ${F051_CMD}/CommandDispatcher.cpp
    ${F051_CMD_CODEC}/CommandCodec.cpp
    ${F051_SVC}/CounterService.cpp
    ${F051_SVC}/TimeDisplayService.cpp
    ${F051_SVC}/TimerService.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_app_entry PRIVATE
    ${COMMON_INCS} ${F051_CMD} ${F051_CMD_CMDS} ${F051_SVC})
target_link_libraries(test_app_entry PRIVATE GTest::gtest_main)

# ── test_services_f103 (Timer + Led + Io + Sensor + Light services) ─────────
# Sensor/Light pull in the I2cBus + Mpu6050 + Ap3216c drivers; the host stub
# for those lives in mocks/sensor_drivers_stub.cpp. We include F103_DRV in
# the -I path so the driver headers resolve, but link the stub .cpp instead
# of the real .cpp files.
add_executable(test_services_f103
    test_services_f103.cpp
    ${F103_SVC_IMPL}/TimerServiceImpl.cpp
    ${F103_SVC_IMPL}/LedServiceImpl.cpp
    ${F103_SVC_IMPL}/IoServiceImpl.cpp
    ${F103_SVC_IMPL}/SensorServiceImpl.cpp
    ${F103_SVC_IMPL}/LightServiceImpl.cpp
    ${SHARED_CORE_EVENT}/Observable.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/Esp8266_stub.cpp
    ${MOCKS_DIR}/display_stubs.cpp
    ${MOCKS_DIR}/sensor_drivers_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_services_f103 PRIVATE
    ${COMMON_INCS} ${SHARED_INC}/view
    ${F103_COMMON} ${F103_SVC_INC} ${F103_SVC_IMPL} ${F103_MODEL} ${F103_DRV})
target_link_libraries(test_services_f103 PRIVATE GTest::gtest_main)

# ── test_display_clock (production SystemClock + DisplayStatus + BitmapButton)
# Drives the REAL F103 SystemClock.hpp by putting F103_COMMON BEFORE MOCKS_DIR
# in the include order so its quoted include resolves to the production header
# instead of the host stub. RTC register stubs in stm32f1xx_hal.h back the
# CNTH/CNTL accesses; CRL is initialised with RTOFF|RSF set so the spin
# loops never block.
add_executable(test_display_clock
    test_display_clock.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/display_stubs.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_display_clock PRIVATE
    ${F103_COMMON} ${MOCKS_DIR} ${SHARED_INC} ${SHARED_INC}/view)
target_link_libraries(test_display_clock PRIVATE GTest::gtest_main)

# ── test_fatfs_file_port (FatFsFilePort retry + zero-fill recovery) ─────────
add_executable(test_fatfs_file_port
    test_fatfs_file_port.cpp
    ${F103_DRV}/FatFsFilePort.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/ff_host_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_fatfs_file_port PRIVATE
    ${COMMON_INCS} ${ATS_INC} ${F103_COMMON} ${F103_DRV})
target_link_libraries(test_fatfs_file_port PRIVATE GTest::gtest_main)

# ── test_appenders (SerialAppender, AtsAppender, DeviceAppender, SyslogAppender) ──
# Header-only Logger appenders. AtsAppender + DeviceAppender are tested at
# the no-op (mDb=nullptr) level here; the production write path is covered
# by test_atsstorage which links the full ArcanaTsDb engine.
add_executable(test_appenders
    test_appenders.cpp
    ${ATS_SRC}/ArcanaTsDb.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/ff_host_stub.cpp
    ${MOCKS_DIR}/Esp8266_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_appenders PRIVATE
    ${COMMON_INCS} ${ATS_INC} ${F103_COMMON} ${F103_DRV})
target_link_libraries(test_appenders PRIVATE GTest::gtest_main)

# ── test_arcanats_db (ArcanaTS v2 core engine + schema) ──────────────────────
add_executable(test_arcanats_db
    test_arcanats_db.cpp
    ${ATS_SRC}/ArcanaTsDb.cpp
)
target_include_directories(test_arcanats_db PRIVATE
    ${COMMON_INCS} ${ATS_INC})
target_link_libraries(test_arcanats_db PRIVATE GTest::gtest_main)

# ── CTest registration ────────────────────────────────────────────────────────
enable_testing()
add_test(NAME test_crc16             COMMAND test_crc16)
add_test(NAME test_frame_codec       COMMAND test_frame_codec)
add_test(NAME test_command_codec     COMMAND test_command_codec)
add_test(NAME test_registry          COMMAND test_registry)
add_test(NAME test_models            COMMAND test_models)
add_test(NAME test_services          COMMAND test_services)
add_test(NAME test_dispatcher        COMMAND test_dispatcher)
add_test(NAME test_commands          COMMAND test_commands)
add_test(NAME test_timer_service     COMMAND test_timer_service)
add_test(NAME test_crc32             COMMAND test_crc32)
add_test(NAME test_frame_assembler   COMMAND test_frame_assembler)
add_test(NAME test_log               COMMAND test_log)
add_test(NAME test_ota_header        COMMAND test_ota_header)
add_test(NAME test_observable        COMMAND test_observable)
add_test(NAME test_observable_errors COMMAND test_observable_errors)
add_test(NAME test_sha256            COMMAND test_sha256)
add_test(NAME test_arcanats_db       COMMAND test_arcanats_db)
add_test(NAME test_chacha20          COMMAND test_chacha20)
add_test(NAME test_crypto_engine     COMMAND test_crypto_engine)
add_test(NAME test_key_exchange      COMMAND test_key_exchange)
add_test(NAME test_command_bridge    COMMAND test_command_bridge)
add_test(NAME test_registration      COMMAND test_registration)
add_test(NAME test_atsstorage        COMMAND test_atsstorage)
add_test(NAME test_httpupload        COMMAND test_httpupload)
add_test(NAME test_mqtt_packet       COMMAND test_mqtt_packet)
add_test(NAME test_ota               COMMAND test_ota)
add_test(NAME test_widgets           COMMAND test_widgets)
add_test(NAME test_lcd_viewmodel     COMMAND test_lcd_viewmodel)
add_test(NAME test_wifi              COMMAND test_wifi)
add_test(NAME test_mqtt              COMMAND test_mqtt)
add_test(NAME test_main_view         COMMAND test_main_view)
add_test(NAME test_ble               COMMAND test_ble)
add_test(NAME test_commands_f103     COMMAND test_commands_f103)
add_test(NAME test_appenders         COMMAND test_appenders)
add_test(NAME test_fatfs_file_port   COMMAND test_fatfs_file_port)
add_test(NAME test_display_clock     COMMAND test_display_clock)
add_test(NAME test_services_f103     COMMAND test_services_f103)
add_test(NAME test_app_entry         COMMAND test_app_entry)
add_test(NAME test_hc08ble_driver    COMMAND test_hc08ble_driver)
add_test(NAME test_esp8266_driver    COMMAND test_esp8266_driver)
add_test(NAME test_esp_flasher       COMMAND test_esp_flasher)
add_test(NAME test_i2c_drivers       COMMAND test_i2c_drivers)
