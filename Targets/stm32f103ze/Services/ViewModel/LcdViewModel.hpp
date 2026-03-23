#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "SystemClock.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include <cstdint>
#include <cstring>

namespace arcana {
namespace lcd {

// ---------------------------------------------------------------------------
// Output: single UI state (all fields the LCD needs to display)
// ---------------------------------------------------------------------------

struct LcdOutput {
    // Temperature
    float temperature;
    bool tempValid;

    // SD card info (one-time after mount)
    uint32_t sdFreeMB;
    uint32_t sdTotalMB;

    // Storage stats
    uint32_t recordCount;
    uint16_t writesPerSec;
    uint32_t totalKB;
    uint16_t kbPerSec;

    // Time
    uint32_t epoch;
    bool timeSynced;
    uint32_t uptimeSec;

    // MQTT status
    bool mqttConnected;
    bool mqttKnown;       // false = no status yet (show "---")

    // ECG waveform (ring buffer + cursor)
    static const uint16_t ECG_WIDTH = 240;
    uint8_t ecgY[ECG_WIDTH];
    uint8_t ecgCursor;
    uint8_t ecgPrevY;    // for line connection

    // Dirty flags (set when field changes, cleared after render)
    uint8_t dirty;
    static const uint8_t DIRTY_TEMP    = 0x01;
    static const uint8_t DIRTY_STORAGE = 0x02;
    static const uint8_t DIRTY_TIME    = 0x04;
    static const uint8_t DIRTY_ECG     = 0x08;
    static const uint8_t DIRTY_SDINFO  = 0x10;
    static const uint8_t DIRTY_MQTT   = 0x20;

    LcdOutput() { memset(this, 0, sizeof(*this)); ecgPrevY = 70; }
};

// ---------------------------------------------------------------------------
// Input: all events that can affect the LCD
// ---------------------------------------------------------------------------

struct LcdInput {
    enum Type : uint8_t {
        SensorData,
        StorageStats,
        TimerTick,
        EcgSample,
        SdInfo,
        MqttStatus,
    };

    Type type;

    union {
        struct { float temperature; }                            sensor;
        struct { uint32_t records; uint16_t rate;
                 uint32_t totalKB; uint16_t kbps; }             storage;
        struct { uint32_t epoch; bool synced; uint32_t uptime; } timer;
        struct { uint8_t y; }                                    ecg;
        struct { uint32_t freeMB; uint32_t totalMB; }            sdinfo;
        struct { bool connected; }                               mqtt;
    };
};

// ---------------------------------------------------------------------------
// Effect: one-time events (future use: error dialogs, alerts)
// ---------------------------------------------------------------------------

enum class LcdEffect : uint8_t {
    None = 0,
    ShowError,
    FlashScreen,
};

// ---------------------------------------------------------------------------
// ViewModel: transforms Input → Output
// ---------------------------------------------------------------------------

class LcdViewModel {
public:
    // Input: data sources wired by Controller
    struct Input {
        Observable<SensorDataModel>*    SensorData;
        Observable<LightDataModel>*     LightData;
        Observable<StorageStatsModel>*  StorageStats;
        Observable<SdBenchmarkModel>*   SdBenchmark;
        Observable<TimerModel>*         BaseTimer;
        Observable<MqttConnectionModel>* MqttConn;
    };
    Input input;

    LcdViewModel() : input(), mOutput(), mNotifyTask(0) {
        input.SensorData = 0;
        input.LightData = 0;
        input.StorageStats = 0;
        input.SdBenchmark = 0;
        input.BaseTimer = 0;
        input.MqttConn = 0;
    }

    /** Subscribe to all wired Observables. renderTask receives xTaskNotifyGive on change. */
    void init(TaskHandle_t renderTask) {
        mNotifyTask = renderTask;
        if (input.SensorData)   input.SensorData->subscribe(onSensorData, this);
        if (input.LightData)    input.LightData->subscribe(onLightData, this);
        if (input.StorageStats) input.StorageStats->subscribe(onStorageStats, this);
        if (input.SdBenchmark)  input.SdBenchmark->subscribe(onSdBenchmark, this);
        if (input.BaseTimer)    input.BaseTimer->subscribe(onBaseTimer, this);
        if (input.MqttConn)     input.MqttConn->subscribe(onMqttConn, this);
    }

    void onEvent(const LcdInput& input) {
        switch (input.type) {
        case LcdInput::SensorData:
            mOutput.temperature = input.sensor.temperature;
            mOutput.tempValid = true;
            mOutput.dirty |= LcdOutput::DIRTY_TEMP;
            break;

        case LcdInput::StorageStats:
            mOutput.recordCount = input.storage.records;
            mOutput.writesPerSec = input.storage.rate;
            mOutput.totalKB = input.storage.totalKB;
            mOutput.kbPerSec = input.storage.kbps;
            mOutput.dirty |= LcdOutput::DIRTY_STORAGE;
            break;

        case LcdInput::TimerTick:
            mOutput.epoch = input.timer.epoch;
            mOutput.timeSynced = input.timer.synced;
            mOutput.uptimeSec = input.timer.uptime;
            mOutput.dirty |= LcdOutput::DIRTY_TIME;
            break;

        case LcdInput::SdInfo:
            mOutput.sdFreeMB = input.sdinfo.freeMB;
            mOutput.sdTotalMB = input.sdinfo.totalMB;
            mOutput.dirty |= LcdOutput::DIRTY_SDINFO;
            break;

        case LcdInput::MqttStatus:
            mOutput.mqttConnected = input.mqtt.connected;
            mOutput.mqttKnown = true;
            mOutput.dirty |= LcdOutput::DIRTY_MQTT;
            break;

        case LcdInput::EcgSample: {
            uint8_t y = input.ecg.y;
            if (y >= LcdOutput::ECG_WIDTH) y = 99;
            mOutput.ecgY[mOutput.ecgCursor] = y;
            mOutput.ecgCursor = (mOutput.ecgCursor + 1) % LcdOutput::ECG_WIDTH;
            mOutput.dirty |= LcdOutput::DIRTY_ECG;
            break;
        }
        }
    }

    const LcdOutput& output() const { return mOutput; }
    void clearDirty() { mOutput.dirty = 0; }

    // For ECG: get cursor and clear only ECG dirty
    uint8_t ecgCursor() const { return mOutput.ecgCursor; }

private:
    void notifyView() { if (mNotifyTask) xTaskNotifyGive(mNotifyTask); }

    static void onSensorData(SensorDataModel* model, void* ctx) {
        LcdViewModel* self = static_cast<LcdViewModel*>(ctx);
        LcdInput in; in.type = LcdInput::SensorData;
        in.sensor.temperature = model->temperature;
        self->onEvent(in); self->notifyView();
    }
    static void onLightData(LightDataModel*, void*) {}
    static void onStorageStats(StorageStatsModel* model, void* ctx) {
        LcdViewModel* self = static_cast<LcdViewModel*>(ctx);
        LcdInput in; in.type = LcdInput::StorageStats;
        in.storage.records = model->recordCount;
        in.storage.rate = model->writesPerSec;
        in.storage.totalKB = model->totalKB;
        in.storage.kbps = model->kbPerSec;
        self->onEvent(in); self->notifyView();
    }
    static void onSdBenchmark(SdBenchmarkModel* model, void* ctx) {
        LcdViewModel* self = static_cast<LcdViewModel*>(ctx);
        LcdInput in; in.type = LcdInput::SdInfo;
        in.sdinfo.freeMB = model->totalKB;
        in.sdinfo.totalMB = model->totalRecords;
        self->onEvent(in); self->notifyView();
    }
    static void onBaseTimer(TimerModel*, void* ctx) {
        LcdViewModel* self = static_cast<LcdViewModel*>(ctx);
        LcdInput in; in.type = LcdInput::TimerTick;
        in.timer.epoch = SystemClock::getInstance().localNow();
        in.timer.synced = SystemClock::getInstance().isSynced();
        in.timer.uptime = xTaskGetTickCount() / configTICK_RATE_HZ;
        self->onEvent(in); self->notifyView();
    }

    static void onMqttConn(MqttConnectionModel* model, void* ctx) {
        LcdViewModel* self = static_cast<LcdViewModel*>(ctx);
        LcdInput in; in.type = LcdInput::MqttStatus;
        in.mqtt.connected = model->connected;
        self->onEvent(in); self->notifyView();
    }

    LcdOutput mOutput;
    TaskHandle_t mNotifyTask;
};

} // namespace lcd
} // namespace arcana
