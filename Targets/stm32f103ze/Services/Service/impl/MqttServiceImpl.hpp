#pragma once

#include "MqttService.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace mqtt {

/**
 * @brief MQTT service using ESP8266 AT+MQTT commands (AT v2.2+)
 *
 * No manual MQTT packet building — ESP8266 handles the protocol:
 * AT+MQTTUSERCFG → AT+MQTTCONN → AT+MQTTSUB → AT+MQTTPUB
 * Keepalive, QoS, and reconnect handled by ESP8266 internally.
 */
class MqttServiceImpl : public MqttService {
public:
    static MqttService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

private:
    MqttServiceImpl();
    ~MqttServiceImpl();

    // FreeRTOS task
    static void mqttTask(void* param);
    void runTask();

    // AT+MQTT helpers
    bool mqttConfig();
    bool mqttConnect();
    bool mqttSubscribe(const char* topic, uint8_t qos = 0);
    bool mqttPublish(const char* topic, const char* payload, uint8_t qos = 0);
    bool mqttDisconnect();
    bool isMqttConnected();

    // Publish sensor data as JSON
    bool publishSensorData(SensorDataModel* model);

    // Process incoming +MQTTSUBRECV messages
    void processIncomingMsg();

    // MQTT send — used as TransportSendFn by CommandBridge TX task
    static bool mqttSendFn(const uint8_t* data, uint16_t len, void* ctx);

    // Observer callbacks
    static void onSensorData(SensorDataModel* model, void* ctx);
    static void onLightData(LightDataModel* model, void* ctx);

    // Configuration
    static const char* MQTT_BROKER;
    static const uint16_t MQTT_PORT = 1883;
    static const char* MQTT_CLIENT_ID;
    static const char* TOPIC_SENSOR;
    static const char* TOPIC_CMD;
    static const char* TOPIC_RSP;

    static const uint16_t TASK_STACK_SIZE = 512;

    Observable<MqttCommandModel>    mCmdObs;
    Observable<MqttConnectionModel> mConnObs;
    MqttCommandModel    mCmdModel;
    MqttConnectionModel mConnModel;

    // Pending sensor + light data for publish
    volatile bool mSensorPending;
    SensorDataModel mPendingSensor;
    LightDataModel mLatestLight;

    StaticTask_t mTaskBuffer;
    StackType_t  mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
    volatile bool mRunning;
    bool mMqttConnected;
};

} // namespace mqtt
} // namespace arcana
