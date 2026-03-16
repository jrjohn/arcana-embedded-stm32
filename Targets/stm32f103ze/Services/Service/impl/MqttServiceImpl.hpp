#pragma once

#include "MqttService.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace mqtt {

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

    // MQTT connection
    bool connectMqtt();
    bool subscribeTopic();
    bool quickReconnect();

    // Publish sensor data as JSON to MQTT
    bool publishSensorData(SensorDataModel* model);

    // Process incoming MQTT subscription messages
    void processIncomingMsg();

    // Raw MQTT over TCP helpers
    bool tcpSend(const uint8_t* data, uint16_t len);
    bool verifyAtReady();
    static uint16_t mqttBuildConnect(uint8_t* buf, const char* clientId);
    static uint16_t mqttBuildSubscribe(uint8_t* buf, const char* topic, uint16_t packetId);
    static uint16_t mqttBuildPublish(uint8_t* buf, const char* topic, const char* payload);

    // Observer callbacks
    static void onSensorData(SensorDataModel* model, void* ctx);
    static void onLightData(LightDataModel* model, void* ctx);

    // Configuration
    static const char* MQTT_BROKER;
    static const uint16_t MQTT_PORT = 1883;
    static const char* MQTT_CLIENT_ID;
    static const char* TOPIC_SENSOR;
    static const char* TOPIC_CMD;

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
