/**
 * @file test_commands_f103.cpp
 * @brief Host coverage for the F103 command handlers in Commands.hpp.
 *
 * Each F103 command class is header-only with no FreeRTOS / HAL surface
 * other than UID_BASE — we wire the same `stm32f1xx_hal.h` host stub used by
 * test_command_bridge so the GetSerialNumber test resolves the fake UID.
 */
#include <gtest/gtest.h>

#include <cstring>

#include "stm32f1xx_hal.h"
#include "CommandTypes.hpp"
#include "ICommand.hpp"
#include "SensorDataCache.hpp"
#include "Commands.hpp"

using arcana::CommandKey;
using arcana::CommandRequest;
using arcana::CommandResponseModel;
using arcana::CommandStatus;
using arcana::Cluster;
using arcana::SensorDataCache;

namespace SC = arcana::SystemCommand;
namespace DC = arcana::DeviceCommand;
namespace SnC = arcana::SensorCommand;

// ── System commands ─────────────────────────────────────────────────────────

TEST(F103Commands, GetFwVersionReturnsBuildDate) {
    arcana::GetFwVersionCommand cmd;
    EXPECT_EQ(cmd.getKey().cluster,   Cluster::System);
    EXPECT_EQ(cmd.getKey().commandId, SC::GetFwVersion);

    CommandRequest req{};
    CommandResponseModel rsp;
    cmd.execute(req, rsp);

    EXPECT_EQ(rsp.status, CommandStatus::Success);
    EXPECT_GT(rsp.dataLength, 0);
    EXPECT_LE(rsp.dataLength, CommandResponseModel::MAX_DATA_LENGTH);
    /* The build date string starts with a recognisable month abbreviation */
    EXPECT_EQ(rsp.dataLength, std::strlen(__DATE__));
}

TEST(F103Commands, GetCompileTimeReturnsDateAndTime) {
    arcana::GetCompileTimeCommand cmd;
    EXPECT_EQ(cmd.getKey().cluster,   Cluster::System);
    EXPECT_EQ(cmd.getKey().commandId, SC::GetCompileTime);

    CommandRequest req{};
    CommandResponseModel rsp;
    cmd.execute(req, rsp);

    EXPECT_EQ(rsp.status, CommandStatus::Success);
    /* "Mar 17 2026 14:30:00" is 20 chars; truncated to MAX_DATA_LENGTH=24 */
    EXPECT_GT(rsp.dataLength, 12);
    EXPECT_LE(rsp.dataLength, CommandResponseModel::MAX_DATA_LENGTH);
}

// ── Device commands ─────────────────────────────────────────────────────────

TEST(F103Commands, GetDeviceModelReturnsStm32) {
    arcana::GetDeviceModelCommand cmd;
    EXPECT_EQ(cmd.getKey().cluster,   Cluster::Device);
    EXPECT_EQ(cmd.getKey().commandId, DC::GetModel);

    CommandRequest req{};
    CommandResponseModel rsp;
    cmd.execute(req, rsp);

    EXPECT_EQ(rsp.status, CommandStatus::Success);
    EXPECT_EQ(rsp.dataLength, std::strlen("STM32F103ZE"));
    EXPECT_EQ(0, std::memcmp(rsp.data, "STM32F103ZE", rsp.dataLength));
}

TEST(F103Commands, GetSerialNumberHexEncodesUid) {
    arcana::GetSerialNumberCommand cmd;
    EXPECT_EQ(cmd.getKey().cluster,   Cluster::Device);
    EXPECT_EQ(cmd.getKey().commandId, DC::GetSerialNumber);

    CommandRequest req{};
    CommandResponseModel rsp;
    cmd.execute(req, rsp);

    EXPECT_EQ(rsp.status, CommandStatus::Success);
    /* 12 UID bytes → 24 hex chars (clamped to MAX_DATA_LENGTH-1 = 23) */
    EXPECT_GT(rsp.dataLength, 0);
    EXPECT_LE(rsp.dataLength, 24);
    /* Each byte must be a hex digit */
    for (uint8_t i = 0; i < rsp.dataLength; ++i) {
        char c = static_cast<char>(rsp.data[i]);
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))
            << "non-hex char at index " << (int)i << ": " << c;
    }
}

// ── Sensor commands (read from cache pointer) ───────────────────────────────

TEST(F103Commands, GetTemperatureWithoutCacheReturnsError) {
    arcana::GetTemperatureCommand cmd;
    /* cache is nullptr by default */
    CommandRequest req{};
    CommandResponseModel rsp;
    cmd.execute(req, rsp);
    EXPECT_EQ(rsp.status, CommandStatus::Error);
}

TEST(F103Commands, GetTemperaturePacksTenths) {
    SensorDataCache cache;
    cache.temp = 23.7f;

    arcana::GetTemperatureCommand cmd;
    cmd.cache = &cache;
    EXPECT_EQ(cmd.getKey().cluster,   Cluster::Sensor);
    EXPECT_EQ(cmd.getKey().commandId, SnC::GetTemperature);

    CommandRequest req{};
    CommandResponseModel rsp;
    cmd.execute(req, rsp);

    EXPECT_EQ(rsp.status, CommandStatus::Success);
    EXPECT_EQ(rsp.dataLength, 2);
    int16_t t10 = static_cast<int16_t>(rsp.data[0] | (rsp.data[1] << 8));
    EXPECT_EQ(t10, 237);
}

TEST(F103Commands, GetAccelWithoutCacheReturnsError) {
    arcana::GetAccelCommand cmd;
    CommandRequest req{};
    CommandResponseModel rsp;
    cmd.execute(req, rsp);
    EXPECT_EQ(rsp.status, CommandStatus::Error);
}

TEST(F103Commands, GetAccelPacksThreeAxes) {
    SensorDataCache cache;
    cache.ax = 100; cache.ay = -200; cache.az = 300;

    arcana::GetAccelCommand cmd;
    cmd.cache = &cache;
    EXPECT_EQ(cmd.getKey().cluster,   Cluster::Sensor);
    EXPECT_EQ(cmd.getKey().commandId, SnC::GetAccel);

    CommandRequest req{};
    CommandResponseModel rsp;
    cmd.execute(req, rsp);

    EXPECT_EQ(rsp.status, CommandStatus::Success);
    EXPECT_EQ(rsp.dataLength, 6);
    int16_t ax = static_cast<int16_t>(rsp.data[0] | (rsp.data[1] << 8));
    int16_t ay = static_cast<int16_t>(rsp.data[2] | (rsp.data[3] << 8));
    int16_t az = static_cast<int16_t>(rsp.data[4] | (rsp.data[5] << 8));
    EXPECT_EQ(ax, 100);
    EXPECT_EQ(ay, -200);
    EXPECT_EQ(az, 300);
}

TEST(F103Commands, GetLightWithoutCacheReturnsError) {
    arcana::GetLightCommand cmd;
    CommandRequest req{};
    CommandResponseModel rsp;
    cmd.execute(req, rsp);
    EXPECT_EQ(rsp.status, CommandStatus::Error);
}

TEST(F103Commands, GetLightPacksAmbientAndProximity) {
    SensorDataCache cache;
    cache.als = 1234; cache.ps = 567;

    arcana::GetLightCommand cmd;
    cmd.cache = &cache;
    EXPECT_EQ(cmd.getKey().cluster,   Cluster::Sensor);
    EXPECT_EQ(cmd.getKey().commandId, SnC::GetLight);

    CommandRequest req{};
    CommandResponseModel rsp;
    cmd.execute(req, rsp);

    EXPECT_EQ(rsp.status, CommandStatus::Success);
    EXPECT_EQ(rsp.dataLength, 4);
    uint16_t als = static_cast<uint16_t>(rsp.data[0] | (rsp.data[1] << 8));
    uint16_t ps  = static_cast<uint16_t>(rsp.data[2] | (rsp.data[3] << 8));
    EXPECT_EQ(als, 1234);
    EXPECT_EQ(ps,  567);
}
