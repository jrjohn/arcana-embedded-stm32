#pragma once

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

    // Storage stats
    uint32_t recordCount;
    uint16_t writesPerSec;
    uint32_t totalKB;
    uint16_t kbPerSec;

    // Time
    uint32_t epoch;
    bool timeSynced;
    uint32_t uptimeSec;

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
    };

    Type type;

    union {
        struct { float temperature; }                            sensor;
        struct { uint32_t records; uint16_t rate;
                 uint32_t totalKB; uint16_t kbps; }             storage;
        struct { uint32_t epoch; bool synced; uint32_t uptime; } timer;
        struct { uint8_t y; }                                    ecg;
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
    LcdViewModel() : mOutput() {}

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
    LcdOutput mOutput;
};

} // namespace lcd
} // namespace arcana
