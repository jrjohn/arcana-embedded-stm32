#pragma once

/**
 * @file Commands.hpp
 * @brief All command implementations (header-only)
 *
 * Each command is a lightweight ICommand with static allocation.
 * Sensor commands read from a shared SensorDataCache pointer.
 */

#include "ICommand.hpp"
#include "SensorDataCache.hpp"
#include <cstring>
#include <cstdio>

// UID_BASE comes from CMSIS device header (included before this file)

namespace arcana {

// ---------------------------------------------------------------------------
// System commands
// ---------------------------------------------------------------------------

class GetFwVersionCommand : public ICommand {
public:
    CommandKey getKey() const override {
        return { Cluster::System, SystemCommand::GetFwVersion };
    }
    void execute(const CommandRequest&, CommandResponseModel& rsp) override {
        const char* ver = __DATE__;  // "Mar 17 2026"
        uint8_t len = strlen(ver);
        if (len > CommandResponseModel::MAX_DATA_LENGTH)
            len = CommandResponseModel::MAX_DATA_LENGTH;
        memcpy(rsp.data, ver, len);
        rsp.dataLength = len;
        rsp.status = CommandStatus::Success;
    }
};

class GetCompileTimeCommand : public ICommand {
public:
    CommandKey getKey() const override {
        return { Cluster::System, SystemCommand::GetCompileTime };
    }
    void execute(const CommandRequest&, CommandResponseModel& rsp) override {
        // "Mar 17 2026 14:30:00"
        char buf[24];
        int n = snprintf(buf, sizeof(buf), "%s %s", __DATE__, __TIME__);
        if (n < 0) n = 0;
        uint8_t len = (uint8_t)n;
        if (len > CommandResponseModel::MAX_DATA_LENGTH)
            len = CommandResponseModel::MAX_DATA_LENGTH;
        memcpy(rsp.data, buf, len);
        rsp.dataLength = len;
        rsp.status = CommandStatus::Success;
    }
};

// ---------------------------------------------------------------------------
// Device commands
// ---------------------------------------------------------------------------

class GetDeviceModelCommand : public ICommand {
public:
    CommandKey getKey() const override {
        return { Cluster::Device, DeviceCommand::GetModel };
    }
    void execute(const CommandRequest&, CommandResponseModel& rsp) override {
        const char* model = "STM32F103ZE";
        uint8_t len = strlen(model);
        memcpy(rsp.data, model, len);
        rsp.dataLength = len;
        rsp.status = CommandStatus::Success;
    }
};

class GetSerialNumberCommand : public ICommand {
public:
    CommandKey getKey() const override {
        return { Cluster::Device, DeviceCommand::GetSerialNumber };
    }
    void execute(const CommandRequest&, CommandResponseModel& rsp) override {
        // Read 12-byte UID, output as 24-char hex string
        const uint8_t* uid = reinterpret_cast<const uint8_t*>(UID_BASE);
        static const char hex[] = "0123456789ABCDEF";
        uint8_t len = 0;
        for (uint8_t i = 0; i < 12 && len + 1 < CommandResponseModel::MAX_DATA_LENGTH; i++) {
            rsp.data[len++] = hex[uid[i] >> 4];
            rsp.data[len++] = hex[uid[i] & 0x0F];
        }
        rsp.dataLength = len;
        rsp.status = CommandStatus::Success;
    }
};

// ---------------------------------------------------------------------------
// Sensor commands — read from shared SensorDataCache
// ---------------------------------------------------------------------------

class GetTemperatureCommand : public ICommand {
public:
    const SensorDataCache* cache = nullptr;

    CommandKey getKey() const override {
        return { Cluster::Sensor, SensorCommand::GetTemperature };
    }
    void execute(const CommandRequest&, CommandResponseModel& rsp) override {
        if (!cache) { rsp.status = CommandStatus::Error; return; }
        int16_t t10 = static_cast<int16_t>(cache->temp * 10.0f);
        rsp.data[0] = static_cast<uint8_t>(t10 & 0xFF);
        rsp.data[1] = static_cast<uint8_t>((t10 >> 8) & 0xFF);
        rsp.dataLength = 2;
        rsp.status = CommandStatus::Success;
    }
};

class GetAccelCommand : public ICommand {
public:
    const SensorDataCache* cache = nullptr;

    CommandKey getKey() const override {
        return { Cluster::Sensor, SensorCommand::GetAccel };
    }
    void execute(const CommandRequest&, CommandResponseModel& rsp) override {
        if (!cache) { rsp.status = CommandStatus::Error; return; }
        rsp.data[0] = static_cast<uint8_t>(cache->ax & 0xFF);
        rsp.data[1] = static_cast<uint8_t>((cache->ax >> 8) & 0xFF);
        rsp.data[2] = static_cast<uint8_t>(cache->ay & 0xFF);
        rsp.data[3] = static_cast<uint8_t>((cache->ay >> 8) & 0xFF);
        rsp.data[4] = static_cast<uint8_t>(cache->az & 0xFF);
        rsp.data[5] = static_cast<uint8_t>((cache->az >> 8) & 0xFF);
        rsp.dataLength = 6;
        rsp.status = CommandStatus::Success;
    }
};

class GetLightCommand : public ICommand {
public:
    const SensorDataCache* cache = nullptr;

    CommandKey getKey() const override {
        return { Cluster::Sensor, SensorCommand::GetLight };
    }
    void execute(const CommandRequest&, CommandResponseModel& rsp) override {
        if (!cache) { rsp.status = CommandStatus::Error; return; }
        rsp.data[0] = static_cast<uint8_t>(cache->als & 0xFF);
        rsp.data[1] = static_cast<uint8_t>((cache->als >> 8) & 0xFF);
        rsp.data[2] = static_cast<uint8_t>(cache->ps & 0xFF);
        rsp.data[3] = static_cast<uint8_t>((cache->ps >> 8) & 0xFF);
        rsp.dataLength = 4;
        rsp.status = CommandStatus::Success;
    }
};

} // namespace arcana
