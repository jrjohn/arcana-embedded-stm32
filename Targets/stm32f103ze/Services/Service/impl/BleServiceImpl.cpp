#include "BleServiceImpl.hpp"
#include "CommandBridge.hpp"
#include "ChaCha20.hpp"
#include "RegistrationServiceImpl.hpp"
#include "Log.hpp"
#include "EventCodes.hpp"
#ifdef ARCANA_CMD_CRYPTO
#include "CryptoEngine.hpp"
#include "KeyExchangeManager.hpp"
#endif
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
    LOG_I(ats::ErrorSource::Ble, evt::BLE_TRANSPORT_READY);

    // Start CommandBridge tasks (RX + TX processing)
    CommandBridge::getInstance().startTasks();
    LOG_I(ats::ErrorSource::Ble, evt::BLE_CMD_COUNT,
          (uint32_t)CommandBridge::getInstance().getCommandCount());

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

        // Sensor push at ~1Hz — ChaCha20 encrypted protobuf (same format as MQTT)
        if (mSensorDirty) {
            mSensorDirty = false;
            pushSensorEncrypted();
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

// ---------------------------------------------------------------------------
// Encrypted sensor push — ChaCha20 + FrameCodec (same format as MQTT)
// No ARCANA_CMD_CRYPTO dependency — uses ChaCha20 (software, zero extra RAM)
// ---------------------------------------------------------------------------

static uint16_t bleVarInt(uint8_t* buf, uint32_t val) {
    uint16_t n = 0;
    while (val > 0x7F) { buf[n++] = (uint8_t)(val | 0x80); val >>= 7; }
    buf[n++] = (uint8_t)val;
    return n;
}

static uint16_t blePbSint32(uint8_t* buf, uint8_t fieldNum, int32_t val) {
    buf[0] = (fieldNum << 3);
    uint32_t zz = (uint32_t)((val << 1) ^ (val >> 31));
    return 1 + bleVarInt(buf + 1, zz);
}

static uint16_t blePbUint32(uint8_t* buf, uint8_t fieldNum, uint32_t val) {
    buf[0] = (fieldNum << 3);
    return 1 + bleVarInt(buf + 1, val);
}

void BleServiceImpl::pushSensorEncrypted() {
    // Same key as MQTT: comm_key (ECDH-derived) or device_key (fallback)
    auto& regSvc = reg::RegistrationServiceImpl::getInstance();
    const uint8_t* key = regSvc.getCommKey();

    // 1. Encode protobuf (~20-30 bytes) — identical to MQTT
    uint8_t pb[40];
    uint16_t pos = 0;
    pos += blePbSint32(pb + pos, 1, (int32_t)(mTemp * 10));
    pos += blePbSint32(pb + pos, 2, (int32_t)mAx);
    pos += blePbSint32(pb + pos, 3, (int32_t)mAy);
    pos += blePbSint32(pb + pos, 4, (int32_t)mAz);
    pos += blePbUint32(pb + pos, 5, (uint32_t)mAls);
    pos += blePbUint32(pb + pos, 6, (uint32_t)mPs);

    // 2. Nonce: [tick:4][0:8] — same as MQTT
    uint8_t nonce[12] = {};
    uint32_t tick = xTaskGetTickCount();
    memcpy(nonce, &tick, 4);

    // 3. ChaCha20 encrypt in-place
    crypto::ChaCha20::crypt(key, nonce, 0, pb, pos);

    // 4. Build payload: [nonce:12][encrypted_pb:N]
    uint8_t enc[52];
    memcpy(enc, nonce, 12);
    memcpy(enc + 12, pb, pos);

    // 5. FrameCodec wrap (streamId 0x20 = sensor stream)
    uint8_t frame[72];
    size_t frameLen = 0;
    if (!FrameCodec::frame(enc, 12 + pos, FrameCodec::kFlagFin, 0x20,
                            frame, sizeof(frame), frameLen)) {
        return;
    }

    mBle.send(frame, (uint16_t)frameLen);
}

#ifdef ARCANA_CMD_CRYPTO
void BleServiceImpl::pushSensorEncryptedCcm() {
    // AES-256-CCM version — only when ARCANA_CMD_CRYPTO enabled + session up
    CommandBridge& bridge = CommandBridge::getInstance();
    if (!bridge.mEncryptionEnabled ||
        !bridge.mKeyExchange.hasSession(CmdFrameItem::BLE, 0)) return;

    uint8_t pb[40];
    uint16_t pos = 0;
    pos += blePbSint32(pb + pos, 1, (int32_t)(mTemp * 10));
    pos += blePbSint32(pb + pos, 2, (int32_t)mAx);
    pos += blePbSint32(pb + pos, 3, (int32_t)mAy);
    pos += blePbSint32(pb + pos, 4, (int32_t)mAz);
    pos += blePbUint32(pb + pos, 5, (uint32_t)mAls);
    pos += blePbUint32(pb + pos, 6, (uint32_t)mPs);

    uint8_t enc[40 + CryptoEngine::kOverhead];
    size_t encLen = 0;
    if (!bridge.mKeyExchange.encryptWithSession(
            CmdFrameItem::BLE, 0, pb, pos, enc, sizeof(enc), encLen)) return;

    uint8_t frame[80];
    size_t frameLen = 0;
    if (!FrameCodec::frame(enc, encLen, FrameCodec::kFlagFin, 0x20,
                            frame, sizeof(frame), frameLen)) return;

    mBle.send(frame, (uint16_t)frameLen);
}
#endif

} // namespace ble
} // namespace arcana
