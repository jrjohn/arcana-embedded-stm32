#pragma once

#include "MqttService.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace mqtt {

/**
 * @brief MQTT 3.1.1 over TCP SSL (Option B)
 *
 * ESP8266 1MB flash has no built-in MQTT+TLS, but AT+CIPSTART="SSL" works.
 * STM32 builds raw MQTT 3.1.1 packets and sends via AT+CIPSEND over TLS TCP.
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

    // TCP SSL connection
    bool sslConnect();
    void sslClose();

    // Raw MQTT 3.1.1 over TCP
    bool mqttHandshake();
    bool mqttSubscribeRaw(const char* topic, uint8_t qos = 0);
    bool mqttPublishRaw(const char* topic, const char* payload);
    bool mqttDisconnectRaw();

    // Low-level: send MQTT packet via AT+CIPSEND
    bool sendMqttPacket(const uint8_t* pkt, uint16_t len);
    // Wait for incoming MQTT packet via +IPD → mMqttBuf
    bool waitMqttPacket(uint8_t* buf, uint16_t& len, uint32_t timeoutMs);

    // Publish sensor data as JSON
    bool publishSensorData(SensorDataModel* model);

    // Process incoming +IPD (raw MQTT packets from server)
    void processIncomingMqtt();

    // MQTT send — used as TransportSendFn by CommandBridge TX task
    static bool mqttSendFn(const uint8_t* data, uint16_t len, void* ctx);

    // Observer callbacks
    static void onSensorData(SensorDataModel* model, void* ctx);
    static void onLightData(LightDataModel* model, void* ctx);

    // Configuration
    static const char* MQTT_BROKER;
    static const uint16_t MQTT_PORT = 8883;
    static const char* MQTT_CLIENT_ID;
    static const char* TOPIC_SENSOR;
    static const char* TOPIC_CMD;
    static const char* TOPIC_RSP;
    static const uint16_t KEEPALIVE_SEC = 60;

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
    uint16_t mNextPacketId;
};

} // namespace mqtt
} // namespace arcana
