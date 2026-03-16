#include "BleServiceImpl.hpp"
#include "FrameCodec.hpp"
#include "Crc16.hpp"
#include <cstring>
#include <cstdio>

namespace arcana {
namespace ble {

// ---------------------------------------------------------------------------
// Built-in PingCommand — returns FreeRTOS tick count
// ---------------------------------------------------------------------------

class PingCommand : public ICommand {
public:
    CommandKey getKey() const override {
        return { Cluster::System, 0x01 };
    }
    void execute(const CommandRequest& req, CommandResponseModel& rsp) override {
        (void)req;
        uint32_t tick = xTaskGetTickCount();
        rsp.setUint32(tick);
        rsp.status = CommandStatus::Success;
    }
};

static PingCommand sPingCmd;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

BleServiceImpl::BleServiceImpl()
    : mCommands{}
    , mCommandCount(0)
    , mTaskBuf()
    , mTaskStack{}
    , mTaskHandle(0)
    , mRunning(false)
    , mTemp(0), mAx(0), mAy(0), mAz(0)
    , mAls(0), mPs(0), mSensorDirty(false)
    , mBle(Hc08Ble::getInstance())
{
}

BleServiceImpl::~BleServiceImpl() {}

BleService& BleServiceImpl::getInstance() {
    static BleServiceImpl sInstance;
    return sInstance;
}

ServiceStatus BleServiceImpl::initHAL() {
    return ServiceStatus::OK;  // HC-08 already init'd by Controller
}

ServiceStatus BleServiceImpl::init() {
    registerCommand(&sPingCmd);

    // Subscribe to sensor data for JSON streaming
    if (input.SensorData) input.SensorData->subscribe(onSensorData, this);
    if (input.LightData)  input.LightData->subscribe(onLightData, this);

    return ServiceStatus::OK;
}

ServiceStatus BleServiceImpl::start() {
    mRunning = true;
    mTaskHandle = xTaskCreateStatic(
        bleTask, "BLE", TASK_STACK_SIZE,
        this, tskIDLE_PRIORITY + 1, mTaskStack, &mTaskBuf);
    if (!mTaskHandle) return ServiceStatus::Error;
    return ServiceStatus::OK;
}

void BleServiceImpl::stop() {
    mRunning = false;
}

// ---------------------------------------------------------------------------
// Command registry
// ---------------------------------------------------------------------------

bool BleServiceImpl::registerCommand(ICommand* cmd) {
    if (mCommandCount >= MAX_COMMANDS) return false;
    mCommands[mCommandCount++] = cmd;
    return true;
}

ICommand* BleServiceImpl::findCommand(CommandKey key) {
    for (uint8_t i = 0; i < mCommandCount; i++) {
        if (mCommands[i]->getKey() == key) return mCommands[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// BLE task — monitors HC-08 for incoming framed commands
// ---------------------------------------------------------------------------

void BleServiceImpl::bleTask(void* param) {
    BleServiceImpl* self = static_cast<BleServiceImpl*>(param);
    vTaskDelay(pdMS_TO_TICKS(2000));  // Wait for BLE to stabilize
    printf("[BLE] Command service ready (%u cmds)\r\n", self->mCommandCount);
    self->taskLoop();
    vTaskDelete(0);
}

void BleServiceImpl::taskLoop() {
    while (mRunning) {
        mBle.clearRx();
        uint16_t len = mBle.waitForData(pdMS_TO_TICKS(1000));

        // Push sensor JSON when dirty (1Hz from Observable callback)
        if (mSensorDirty) {
            mSensorDirty = false;
            pushSensorJson();
        }

        if (len == 0) continue;
        if (len > 0) {
            const uint8_t* raw = (const uint8_t*)mBle.getResponse();
            // Log raw bytes received from BLE
            printf("[BLE] RX %u bytes:", len);
            for (uint16_t i = 0; i < len && i < 24; i++) {
                printf(" %02X", raw[i]);
            }
            printf("\r\n");
            processFrame(raw, len);
        }
    }
}

// ---------------------------------------------------------------------------
// Frame processing — deframe → execute → encode response → send back
// ---------------------------------------------------------------------------

void BleServiceImpl::processFrame(const uint8_t* data, uint16_t len) {
    // 1. Deframe (validate magic, CRC-16)
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    uint8_t flags = 0, streamId = 0;

    if (!FrameCodec::deframe(data, len, payload, payloadLen, flags, streamId)) {
        printf("[BLE] Bad frame (%u bytes)\r\n", len);
        return;
    }

    // 2. Decode command request from payload
    // Payload format: [cluster:1][commandId:1][paramsLen:1][params:0-8]
    if (payloadLen < 3) return;
    CommandRequest req;
    req.key.cluster = static_cast<Cluster>(payload[0]);
    req.key.commandId = payload[1];
    req.paramsLength = payload[2];
    if (req.paramsLength > 8) req.paramsLength = 8;
    if (payloadLen >= 3u + req.paramsLength) {
        memcpy(req.params, payload + 3, req.paramsLength);
    }

    printf("[BLE] CMD %02X:%02X\r\n",
           (unsigned)req.key.cluster, (unsigned)req.key.commandId);

    // 3. Execute command
    CommandResponseModel rsp;
    rsp.key = req.key;

    ICommand* cmd = findCommand(req.key);
    if (cmd) {
        cmd->execute(req, rsp);
    } else {
        rsp.status = CommandStatus::NotFound;
    }

    // 4. Encode response payload: [cluster:1][commandId:1][status:1][dataLen:1][data:0-16]
    uint8_t rspPayload[20];
    rspPayload[0] = static_cast<uint8_t>(rsp.key.cluster);
    rspPayload[1] = rsp.key.commandId;
    rspPayload[2] = static_cast<uint8_t>(rsp.status);
    rspPayload[3] = rsp.dataLength;
    if (rsp.dataLength > 0) {
        memcpy(rspPayload + 4, rsp.data, rsp.dataLength);
    }
    size_t rspPayloadLen = 4 + rsp.dataLength;

    // 5. Frame the response
    uint8_t frameBuf[32];
    size_t frameLen = 0;
    if (!FrameCodec::frame(rspPayload, rspPayloadLen,
                           FrameCodec::kFlagFin, FrameCodec::kSidNone,
                           frameBuf, sizeof(frameBuf), frameLen)) {
        return;
    }

    // 6. Send back via BLE
    mBle.send(frameBuf, (uint16_t)frameLen);
    printf("[BLE] RSP status=%u data=%u\r\n",
           (unsigned)rsp.status, (unsigned)rsp.dataLength);
}

// ---------------------------------------------------------------------------
// Sensor data → JSON streaming to BLE
// ---------------------------------------------------------------------------

void BleServiceImpl::onSensorData(SensorDataModel* model, void* ctx) {
    BleServiceImpl* self = static_cast<BleServiceImpl*>(ctx);
    self->mTemp = model->temperature;
    self->mAx = model->accelX;
    self->mAy = model->accelY;
    self->mAz = model->accelZ;
    self->mSensorDirty = true;
}

void BleServiceImpl::onLightData(LightDataModel* model, void* ctx) {
    BleServiceImpl* self = static_cast<BleServiceImpl*>(ctx);
    self->mAls = model->ambientLight;
    self->mPs  = model->proximity;
    self->mSensorDirty = true;
}

void BleServiceImpl::pushSensorJson() {
    char json[96];
    int whole = (int)mTemp;
    int frac = (int)((mTemp - whole) * 10);
    if (frac < 0) frac = -frac;

    int n = snprintf(json, sizeof(json),
        "{\"t\":%d.%d,\"ax\":%d,\"ay\":%d,\"az\":%d,\"als\":%u,\"ps\":%u}\n",
        whole, frac,
        (int)mAx, (int)mAy, (int)mAz,
        (unsigned)mAls, (unsigned)mPs);

    if (n > 0) {
        mBle.send((const uint8_t*)json, (uint16_t)n);
    }
}

} // namespace ble
} // namespace arcana
