cmake_minimum_required(VERSION 3.14)
project(arcana_stm32_tests CXX)

set(CMAKE_CXX_STANDARD 17)
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
set(F051_CMD      ${CMAKE_SOURCE_DIR}/../Targets/stm32f051c8/Services/command)
set(F051_SVC      ${CMAKE_SOURCE_DIR}/../Targets/stm32f051c8/Services/service)
set(MOCKS_DIR     ${CMAKE_SOURCE_DIR}/mocks)

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
    ${COMMON_INCS} ${F051_CMD} ${F051_SVC})
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

# ── CTest registration ────────────────────────────────────────────────────────
enable_testing()
add_test(NAME test_crc16          COMMAND test_crc16)
add_test(NAME test_frame_codec    COMMAND test_frame_codec)
add_test(NAME test_command_codec  COMMAND test_command_codec)
add_test(NAME test_registry       COMMAND test_registry)
add_test(NAME test_models         COMMAND test_models)
add_test(NAME test_services       COMMAND test_services)
add_test(NAME test_dispatcher     COMMAND test_dispatcher)
add_test(NAME test_commands       COMMAND test_commands)
add_test(NAME test_timer_service  COMMAND test_timer_service)
