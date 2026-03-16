#pragma once

#include "LcdView.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

namespace arcana {
namespace lcd {

class LcdViewModel;  // forward

/**
 * Main dashboard view — ECG waveform + storage stats + time.
 * Owns render task, ECG queue, LCD mutex.
 * Default view on boot.
 */
class MainView : public LcdView {
public:
    struct Input {
        LcdViewModel* viewModel;
        Ili9341Lcd*    lcd;
    };
    Input input;

    MainView();

    void init();    // create mutex, ECG queue
    void start();   // create render task, call onEnter

    /** Push ECG sample from any task (thread-safe via queue) */
    void pushEcgSample(uint8_t y);

    /** Render task handle — ViewModel needs this for xTaskNotifyGive */
    TaskHandle_t renderTaskHandle() const { return mRenderTaskHandle; }

    // LcdView interface
    void onEnter(Ili9341Lcd& lcd) override;
    void render(Ili9341Lcd& lcd, const LcdOutput& output, LcdOutput& rendered) override;
    void renderEcgColumn(Ili9341Lcd& lcd, uint8_t x, uint8_t y, uint8_t prevY) override;

private:
    void renderTemp(Ili9341Lcd& lcd, const LcdOutput& out, LcdOutput& rendered);
    void renderSdInfo(Ili9341Lcd& lcd, const LcdOutput& out, LcdOutput& rendered);
    void renderStorage(Ili9341Lcd& lcd, const LcdOutput& out, LcdOutput& rendered);
    void renderTime(Ili9341Lcd& lcd, const LcdOutput& out, LcdOutput& rendered);

    static void uint32ToStr(char* buf, uint32_t value);

    // Render task
    static void renderTaskEntry(void* param);
    void processRender();
    static const uint16_t RENDER_TASK_STACK = 256;
    StaticTask_t mRenderTaskBuf;
    StackType_t mRenderTaskStack[RENDER_TASK_STACK];
    TaskHandle_t mRenderTaskHandle;

    // ECG sample queue
    static const uint8_t ECG_QUEUE_LEN = 16;
    QueueHandle_t mEcgQueue;
    StaticQueue_t mEcgQueueBuf;
    uint8_t mEcgQueueStorage[ECG_QUEUE_LEN];

    // LCD mutex + render diff
    SemaphoreHandle_t mLcdMutex;
    StaticSemaphore_t mLcdMutexBuf;
    LcdOutput mRendered;

    // Layout constants
    static const uint16_t VALUE_X      = 20;
    static const uint16_t TEMP_VALUE_Y = 42;
    static const uint16_t SD_INFO_Y    = 82;
    static const uint16_t SD_STATUS_Y  = 96;
    static const uint16_t SD_RECORDS_Y = 112;
    static const uint16_t SD_RATE_Y    = 124;
    static const uint16_t MQTT_LABEL_Y = 142;
    static const uint16_t MQTT_STATUS_Y= 154;
    static const uint16_t ECG_TOP_Y    = 174;
    static const uint16_t ECG_HEIGHT   = 100;
    static const uint16_t ECG_WIDTH    = 240;
    static const uint16_t CLOCK_DATE_Y = 286;
    static const uint16_t CLOCK_TIME_Y = 304;
};

} // namespace lcd
} // namespace arcana
