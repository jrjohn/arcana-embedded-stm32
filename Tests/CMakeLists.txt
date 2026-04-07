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
set(ATS_INC       ${SHARED_INC}/ats)
set(ATS_SRC       ${SHARED_SRC}/ats)
set(F103_COMMON   ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Services/Common)
set(F051_CMD      ${CMAKE_SOURCE_DIR}/../Targets/stm32f051c8/Services/command)
set(F051_SVC      ${CMAKE_SOURCE_DIR}/../Targets/stm32f051c8/Services/service)
set(F103_SVC_IMPL ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Services/Service/impl)
set(F103_SVC_INC  ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Services/Service)
set(F103_CMD_INC  ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Services/Command)
set(F103_DRV      ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Services/Driver)
set(F103_MODEL    ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Services/Model)
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
set(COMMON_INCS ${MOCKS_DIR} ${SHARED_INC})

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
    ${F051_CMD}/CommandCodec.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_command_codec PRIVATE ${COMMON_INCS} ${F051_CMD})
target_link_libraries(test_command_codec PRIVATE GTest::gtest_main)

# ── test_registry ─────────────────────────────────────────────────────────────
add_executable(test_registry
    test_registry.cpp
    ${F051_CMD}/CommandRegistry.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_registry PRIVATE ${COMMON_INCS} ${F051_CMD})
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
    ${F051_CMD}/CommandCodec.cpp
    ${F051_CMD}/CommandRegistry.cpp
    ${F051_CMD}/CommandDispatcher.cpp
    ${F051_CMD}/CommandService.cpp
    ${F051_SVC}/CounterService.cpp
    ${SHARED_SRC}/Observable.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_dispatcher PRIVATE
    ${COMMON_INCS} ${F051_CMD} ${F051_CMD}/impl ${F051_SVC})
target_link_libraries(test_dispatcher PRIVATE GTest::gtest_main)

# ── test_commands (PingCommand + GetCounterCommand) ───────────────────────────
add_executable(test_commands
    test_commands.cpp
    ${F051_SVC}/CounterService.cpp
    ${SHARED_SRC}/Observable.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_commands PRIVATE
    ${COMMON_INCS} ${F051_CMD} ${F051_SVC}
    ${F051_CMD}/impl)
target_link_libraries(test_commands PRIVATE GTest::gtest_main)

# ── test_timer_service ────────────────────────────────────────────────────────
add_executable(test_timer_service
    test_timer_service.cpp
    ${F051_SVC}/TimerService.cpp
    ${SHARED_SRC}/Observable.cpp
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
    ${SHARED_SRC}/Observable.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_observable PRIVATE ${COMMON_INCS})
target_link_libraries(test_observable PRIVATE GTest::gtest_main)

# ── test_observable_errors (separate exe: tests queue-null + queue-full) ──────
# Uses its own FreeRTOS stubs (inline in .cpp) with controllable xQueueSend
add_executable(test_observable_errors
    test_observable_errors.cpp
    ${SHARED_SRC}/Observable.cpp
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
    ${SHARED_SRC}/CryptoEngine.cpp
    ${MBEDTLS_SOURCES}
)
target_include_directories(test_crypto_engine PRIVATE ${COMMON_INCS} ${MBEDTLS_INC} ${MBEDTLS_INC2})
target_link_libraries(test_crypto_engine PRIVATE GTest::gtest_main)

# ── test_key_exchange (KeyExchangeManager + ECDH P-256 over real mbedtls) ─────
add_executable(test_key_exchange
    test_key_exchange.cpp
    ${SHARED_SRC}/KeyExchangeManager.cpp
    ${SHARED_SRC}/CryptoEngine.cpp
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
    ${SHARED_SRC}/Observable.cpp
    ${SHARED_SRC}/uECC.c
    ${SHARED_SRC}/registration.pb.c
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
    ${SHARED_SRC}/Observable.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${MOCKS_DIR}/ff_host_stub.cpp
    ${MOCKS_DIR}/atsstorage_host_stubs.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_atsstorage PRIVATE
    ${COMMON_INCS} ${ATS_INC} ${SHARED_INC}/display
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
    ${SHARED_SRC}/Observable.cpp
    ${SHARED_SRC}/uECC.c
    ${SHARED_SRC}/registration.pb.c
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
    ${COMMON_INCS} ${ATS_INC} ${SHARED_INC}/display
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

# ── test_lcd_viewmodel (LcdViewModel observable wiring + dirty flags) ────────
add_executable(test_lcd_viewmodel
    test_lcd_viewmodel.cpp
    ${SHARED_SRC}/Observable.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_lcd_viewmodel PRIVATE
    ${COMMON_INCS} ${F103_COMMON} ${F103_MODEL}
    ${CMAKE_SOURCE_DIR}/../Targets/stm32f103ze/Services/ViewModel)
target_link_libraries(test_lcd_viewmodel PRIVATE GTest::gtest_main)

# ── test_widgets (FormWidgets + DialogWidgets + Widget + WidgetGroup) ────────
add_executable(test_widgets test_widgets.cpp)
target_include_directories(test_widgets PRIVATE
    ${COMMON_INCS} ${SHARED_INC}/display)
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
    ${SHARED_SRC}/Observable.cpp
    ${MOCKS_DIR}/test_hal_stub.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_command_bridge PRIVATE
    ${COMMON_INCS} ${F103_COMMON} ${F103_SVC_INC} ${F103_CMD_INC})
target_link_libraries(test_command_bridge PRIVATE GTest::gtest_main)

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
