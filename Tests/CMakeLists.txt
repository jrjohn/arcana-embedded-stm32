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
set(F051_CMD      ${CMAKE_SOURCE_DIR}/../Targets/stm32f051c8/Services/command)
set(MOCKS_DIR     ${CMAKE_SOURCE_DIR}/mocks)

# FreeRTOS stubs (provides xTaskGetTickCount + queue/task stubs)
set(FREERTOS_STUBS ${MOCKS_DIR}/freertos_stubs.cpp)

# ── test_crc16 ────────────────────────────────────────────────────────────────
add_executable(test_crc16 test_crc16.cpp)
target_include_directories(test_crc16 PRIVATE ${SHARED_INC})
target_link_libraries(test_crc16 PRIVATE GTest::gtest_main)

# ── test_frame_codec ──────────────────────────────────────────────────────────
add_executable(test_frame_codec test_frame_codec.cpp)
target_include_directories(test_frame_codec PRIVATE ${SHARED_INC})
target_link_libraries(test_frame_codec PRIVATE GTest::gtest_main)

# ── test_command_codec ────────────────────────────────────────────────────────
add_executable(test_command_codec
    test_command_codec.cpp
    ${F051_CMD}/CommandCodec.cpp
    ${FREERTOS_STUBS}
)
# mocks dir FIRST so our FreeRTOS.h is found before any system header
target_include_directories(test_command_codec PRIVATE
    ${MOCKS_DIR}
    ${SHARED_INC}
    ${F051_CMD}
)
target_link_libraries(test_command_codec PRIVATE GTest::gtest_main)

# ── test_registry ─────────────────────────────────────────────────────────────
add_executable(test_registry
    test_registry.cpp
    ${F051_CMD}/CommandRegistry.cpp
    ${FREERTOS_STUBS}
)
target_include_directories(test_registry PRIVATE
    ${MOCKS_DIR}
    ${SHARED_INC}
    ${F051_CMD}
)
target_link_libraries(test_registry PRIVATE GTest::gtest_main)

# ── CTest registration ─────────────────────────────────────────────────────────
# Use add_test (not gtest_discover_tests) — discovery mode requires running
# the binary at configure time which fails inside Docker build layers.
enable_testing()
add_test(NAME test_crc16          COMMAND test_crc16)
add_test(NAME test_frame_codec    COMMAND test_frame_codec)
add_test(NAME test_command_codec  COMMAND test_command_codec)
add_test(NAME test_registry       COMMAND test_registry)
