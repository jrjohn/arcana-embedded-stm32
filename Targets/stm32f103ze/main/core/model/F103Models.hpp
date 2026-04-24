#pragma once

#include "Models.hpp"

namespace arcana {

enum class F103ModelType : uint8_t {
    Sensor         = 100,
    MqttCommand    = 101,
    MqttConnection = 102,
    LedFrame       = 103,
    Light          = 104,
    StorageStats   = 105,
    SdBenchmark    = 106
};

class SensorDataModel : public Model {
public:
    float temperature;
    int16_t accelX;
    int16_t accelY;
    int16_t accelZ;

    SensorDataModel()
        : Model(static_cast<uint8_t>(F103ModelType::Sensor))
        , temperature(0.0f)
        , accelX(0), accelY(0), accelZ(0) {}
};

class MqttCommandModel : public Model {
public:
    static const uint8_t MAX_DATA = 64;
    uint8_t data[MAX_DATA];
    uint8_t length;

    MqttCommandModel()
        : Model(static_cast<uint8_t>(F103ModelType::MqttCommand))
        , data{}
        , length(0) {}
};

class MqttConnectionModel : public Model {
public:
    bool connected;

    MqttConnectionModel()
        : Model(static_cast<uint8_t>(F103ModelType::MqttConnection))
        , connected(false) {}
};

class LightDataModel : public Model {
public:
    uint16_t ambientLight;
    uint16_t proximity;

    LightDataModel()
        : Model(static_cast<uint8_t>(F103ModelType::Light))
        , ambientLight(0)
        , proximity(0) {}
};

class LedFrameModel : public Model {
public:
    uint8_t r, g, b;

    LedFrameModel()
        : Model(static_cast<uint8_t>(F103ModelType::LedFrame))
        , r(0), g(0), b(0) {}
};

class StorageStatsModel : public Model {
public:
    uint32_t recordCount;
    uint16_t writesPerSec;  // Writes completed in last 1-second window
    uint32_t totalKB;       // Total file size on disk in KB
    uint16_t kbPerSec;      // Data throughput in KB/s

    StorageStatsModel()
        : Model(static_cast<uint8_t>(F103ModelType::StorageStats))
        , recordCount(0)
        , writesPerSec(0)
        , totalKB(0)
        , kbPerSec(0) {}
};

class SdBenchmarkModel : public Model {
public:
    uint32_t speedKBps10;   // KB/s * 10 (e.g. 25600 = 2560.0 KB/s)
    uint32_t totalKB;       // Total KB written
    uint32_t totalRecords;  // Total records written
    uint32_t recordsPerBuf; // Records per write buffer
    uint32_t recordsPerSec; // Records written in last 1-second window
    bool error;
    uint8_t errorStep;      // 0=none, 1=HAL, 2=mount, 3=format, 4=remount, 5=file_open
    uint8_t errorCode;      // FRESULT value from FatFS

    SdBenchmarkModel()
        : Model(static_cast<uint8_t>(F103ModelType::SdBenchmark))
        , speedKBps10(0)
        , totalKB(0)
        , totalRecords(0)
        , recordsPerBuf(0)
        , recordsPerSec(0)
        , error(false)
        , errorStep(0)
        , errorCode(0) {}
};

} // namespace arcana
