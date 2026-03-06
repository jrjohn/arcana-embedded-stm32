#pragma once

#include "Models.hpp"

namespace arcana {

enum class F103ModelType : uint8_t {
    Sensor         = 100,
    MqttCommand    = 101,
    MqttConnection = 102,
    LedFrame       = 103,
    Light          = 104
};

class SensorDataModel : public Model {
public:
    float temperature;
    float humidity;
    uint8_t quality;

    SensorDataModel()
        : Model(static_cast<uint8_t>(F103ModelType::Sensor))
        , temperature(0.0f)
        , humidity(0.0f)
        , quality(0) {}
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

} // namespace arcana
