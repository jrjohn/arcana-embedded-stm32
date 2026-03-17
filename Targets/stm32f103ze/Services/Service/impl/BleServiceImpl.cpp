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

    // Register BLE send function with CommandBridge
    CommandBridge::getInstance().setBleSend(bleSendFn, this);

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
// Transport send callback — CommandBridge TX task calls this
// ---------------------------------------------------------------------------

bool BleServiceImpl::bleSendFn(const uint8_t* data, uint16_t len, void* ctx) {
    BleServiceImpl* self = static_cast<BleServiceImpl*>(ctx);
    return self->mBle.send(data, len);
}

// ---------------------------------------------------------------------------
// BLE task — ring buffer drain + sensor JSON push
// ---------------------------------------------------------------------------

void BleServiceImpl::bleTask(void* param) {
    BleServiceImpl* self = static_cast<BleServiceImpl*>(param);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Switch HC-08 driver to data (frame reassembly) mode
    self->mBle.setDataMode(true);
    printf("[BLE] Transport ready\r\n");

    // Start CommandBridge tasks (RX + TX processing)
    CommandBridge::getInstance().startTasks();
    printf("[CMD] %u cmds\r\n",
           (unsigned)CommandBridge::getInstance().getCommandCount());

    self->taskLoop();
    vTaskDelete(0);
}

void BleServiceImpl::taskLoop() {
    while (mRunning) {
        // Wait for IDLE interrupt (new data available) or 1s timeout
        uint16_t pending = mBle.waitForData(pdMS_TO_TICKS(1000));

        // Drain ring buffer → FrameAssembler → submit complete frames
        if (pending > 0) {
            while (mBle.processRxRing()) {
                CommandBridge::getInstance().submitFrame(
                    mBle.getFrame(), mBle.getFrameLen(),
                    CmdFrameItem::BLE);
                mBle.resetFrame();
            }
        }

        // Sensor JSON push at ~1Hz
        if (mSensorDirty) {
            mSensorDirty = false;
            pushSensorJson();
        }
    }
}

// ---------------------------------------------------------------------------
// Sensor data → JSON streaming + cache update
// ---------------------------------------------------------------------------

void BleServiceImpl::onSensorData(SensorDataModel* model, void* ctx) {
    BleServiceImpl* self = static_cast<BleServiceImpl*>(ctx);
    self->mTemp = model->temperature;
    self->mAx = model->accelX;
    self->mAy = model->accelY;
    self->mAz = model->accelZ;
    self->mSensorDirty = true;

    // Update shared sensor cache for commands
    SensorDataCache& cache = CommandBridge::getInstance().getSensorCache();
    cache.temp = model->temperature;
    cache.ax = model->accelX;
    cache.ay = model->accelY;
    cache.az = model->accelZ;
}

void BleServiceImpl::onLightData(LightDataModel* model, void* ctx) {
    BleServiceImpl* self = static_cast<BleServiceImpl*>(ctx);
    self->mAls = model->ambientLight;
    self->mPs  = model->proximity;
    self->mSensorDirty = true;

    // Update shared sensor cache for commands
    SensorDataCache& cache = CommandBridge::getInstance().getSensorCache();
    cache.als = model->ambientLight;
    cache.ps  = model->proximity;
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
