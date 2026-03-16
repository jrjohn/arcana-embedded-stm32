#include "BleServiceImpl.hpp"
#include "CommandBridge.hpp"
#include <cstring>
#include <cstdio>

namespace arcana {
namespace ble {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

BleServiceImpl::BleServiceImpl()
    : mTaskBuf()
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
    return ServiceStatus::OK;
}

ServiceStatus BleServiceImpl::init() {
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
// BLE transport callback — CommandBridge sends response here
// ---------------------------------------------------------------------------

void BleServiceImpl::onBleResponse(const uint8_t* frameBuf, uint16_t frameLen, void* ctx) {
    BleServiceImpl* self = static_cast<BleServiceImpl*>(ctx);
    self->mBle.send(frameBuf, frameLen);
}

// ---------------------------------------------------------------------------
// BLE task
// ---------------------------------------------------------------------------

void BleServiceImpl::bleTask(void* param) {
    BleServiceImpl* self = static_cast<BleServiceImpl*>(param);
    vTaskDelay(pdMS_TO_TICKS(2000));
    printf("[BLE] Transport ready\r\n");
    self->taskLoop();
    vTaskDelete(0);
}

void BleServiceImpl::taskLoop() {
    while (mRunning) {
        mBle.clearRx();
        uint16_t len = mBle.waitForData(pdMS_TO_TICKS(1000));

        if (mSensorDirty) {
            mSensorDirty = false;
            pushSensorJson();
        }

        if (len == 0) continue;

        const uint8_t* raw = (const uint8_t*)mBle.getResponse();
        printf("[BLE] RX %u bytes\r\n", len);

        // Delegate to shared CommandBridge
        CommandBridge::getInstance().processFrame(raw, len, onBleResponse, this);
    }
}

// ---------------------------------------------------------------------------
// Sensor data → JSON streaming
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
