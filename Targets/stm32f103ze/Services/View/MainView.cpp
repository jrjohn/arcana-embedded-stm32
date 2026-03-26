#include "MainView.hpp"
#include "LcdViewModel.hpp"
#include "SystemClock.hpp"
#include "DisplayStatus.hpp"
#include <cstdio>
#include <cstring>

namespace arcana {
namespace lcd {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

MainView::MainView()
    : input()
    , mRenderTaskBuf()
    , mRenderTaskStack{}
    , mRenderTaskHandle(0)
    , mEcgQueue(0)
    , mLcdMutex(0)
    , mRendered()
{
    input.viewModel = 0;
    input.lcd = 0;
}

void MainView::init() {
    mLcdMutex = xSemaphoreCreateMutexStatic(&mLcdMutexBuf);
    mEcgQueue = xQueueCreateStatic(ECG_QUEUE_LEN, 1,
                                    mEcgQueueStorage, &mEcgQueueBuf);
}

void MainView::start() {
    if (input.lcd) onEnter(*input.lcd);

    mRenderTaskHandle = xTaskCreateStatic(
        renderTaskEntry, "LcdRndr", RENDER_TASK_STACK,
        this, tskIDLE_PRIORITY + 1, mRenderTaskStack, &mRenderTaskBuf);
}

void MainView::pushEcgSample(uint8_t y) {
    if (mEcgQueue) {
        xQueueSend(mEcgQueue, &y, 0);
        if (mRenderTaskHandle) xTaskNotifyGive(mRenderTaskHandle);
    }
}

// ---------------------------------------------------------------------------
// Render task — blocks on xTaskNotify, wakes on ViewModel change / ECG
// ---------------------------------------------------------------------------

void MainView::renderTaskEntry(void* param) {
    MainView* self = static_cast<MainView*>(param);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        self->processRender();
    }
}

void MainView::processRender() {
    if (!input.viewModel || !input.lcd) return;
    if (xSemaphoreTake(mLcdMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    LcdViewModel& vm = *input.viewModel;
    display::IDisplay& lcd = *input.lcd;

    // 1. Drain all pending ECG samples → ViewModel → render
    uint8_t y;
    while (xQueueReceive(mEcgQueue, &y, 0) == pdTRUE) {
        const LcdOutput& out = vm.output();
        uint8_t cursor = out.ecgCursor;
        uint8_t prevY = out.ecgPrevY;

        LcdInput in;
        in.type = LcdInput::EcgSample;
        in.ecg.y = y;
        vm.onEvent(in);

        renderEcgColumn(lcd, cursor, y, prevY);
    }

    // 2. Render dirty fields
    if (vm.output().dirty) {
        render(lcd, vm.output(), mRendered);
        vm.clearDirty();
    }

    // 3. Toast overlay — single-writer: only render task touches LCD for toast
    {
        bool expired = display::toastUpdate((uint32_t)xTaskGetTickCount());
        if (expired) {
            // Toast dismissed — redraw full layout
            onEnter(lcd);
            mRendered = LcdOutput();
            LcdOutput fullRedraw = vm.output();
            fullRedraw.dirty = 0xFF;
            render(lcd, fullRedraw, mRendered);
            mRendered = vm.output();
            mRendered.dirty = 0;
        }
    }

    xSemaphoreGive(mLcdMutex);
}

// ---------------------------------------------------------------------------
// Layout: onEnter draws static labels
// ---------------------------------------------------------------------------

void MainView::onEnter(display::IDisplay& lcd) {
    lcd.fillScreen(display::colors::BLACK);

    lcd.drawString(30, 4, "Arcana F103", display::colors::WHITE, display::colors::BLACK, 2);

    lcd.drawHLine(10, 24, 220, display::colors::DARKGRAY);
    lcd.drawString(VALUE_X, 30, "Temperature", display::colors::WHITE, display::colors::BLACK, 1);
    lcd.drawString(VALUE_X, TEMP_VALUE_Y, "-- C", display::colors::YELLOW, display::colors::BLACK, 2);

    lcd.drawHLine(10, 62, 220, display::colors::DARKGRAY);
    lcd.drawString(VALUE_X, 68, "SD Storage (ArcanaTS)", display::colors::WHITE, display::colors::BLACK, 1);
    lcd.drawString(VALUE_X, SD_RECORDS_Y, "Records:", display::colors::GRAY, display::colors::BLACK, 1);
    lcd.drawString(VALUE_X + 54, SD_RECORDS_Y, "---", display::colors::GRAY, display::colors::BLACK, 1);
    lcd.drawString(VALUE_X, SD_RATE_Y, "Rate:", display::colors::GRAY, display::colors::BLACK, 1);
    lcd.drawString(VALUE_X + 36, SD_RATE_Y, "---", display::colors::GRAY, display::colors::BLACK, 1);

    lcd.drawHLine(10, 136, 220, display::colors::DARKGRAY);
    lcd.drawString(VALUE_X, MQTT_LABEL_Y, "WiFi / MQTT", display::colors::WHITE, display::colors::BLACK, 1);
    lcd.drawString(VALUE_X, MQTT_STATUS_Y, "---", display::colors::GRAY, display::colors::BLACK, 1);

    lcd.drawString(78, CLOCK_DATE_Y, "UPTIME", display::colors::YELLOW, display::colors::BLACK, 2);
    lcd.drawString(72, CLOCK_TIME_Y, "00:00:00", display::colors::YELLOW, display::colors::BLACK, 2);

    lcd.drawHLine(0, ECG_TOP_Y - 2, 240, display::colors::DARKGRAY);
    lcd.drawString(2, ECG_TOP_Y - 12, "ECG", display::colors::GREEN, display::colors::BLACK, 1);
    lcd.drawString(40, ECG_TOP_Y - 12, "II  25mm/s", display::colors::GRAY, display::colors::BLACK, 1);
}

void MainView::render(display::IDisplay& lcd, const LcdOutput& out, LcdOutput& rendered) {
    if (out.dirty & LcdOutput::DIRTY_TEMP)    renderTemp(lcd, out, rendered);
    if (out.dirty & LcdOutput::DIRTY_SDINFO)  renderSdInfo(lcd, out, rendered);
    if (out.dirty & LcdOutput::DIRTY_STORAGE) renderStorage(lcd, out, rendered);
    if (out.dirty & LcdOutput::DIRTY_TIME)    renderTime(lcd, out, rendered);
    if (out.dirty & LcdOutput::DIRTY_MQTT)   renderMqtt(lcd, out, rendered);
}

void MainView::renderTemp(display::IDisplay& lcd, const LcdOutput& out, LcdOutput& rendered) {
    char buf[20];
    int whole = (int)out.temperature;
    int frac = (int)((out.temperature - whole) * 10);
    if (frac < 0) frac = -frac;
    snprintf(buf, sizeof(buf), "%d.%d C  ", whole, frac);

    lcd.fillRect(VALUE_X, TEMP_VALUE_Y, 180, 16, display::colors::BLACK);
    lcd.drawString(VALUE_X, TEMP_VALUE_Y, buf, display::colors::YELLOW, display::colors::BLACK, 2);
    rendered.temperature = out.temperature;
    rendered.tempValid = true;
}

void MainView::renderSdInfo(display::IDisplay& lcd, const LcdOutput& out, LcdOutput& rendered) {
    char buf[28];
    snprintf(buf, sizeof(buf), "%lu / %luMB",
             (unsigned long)out.sdFreeMB, (unsigned long)out.sdTotalMB);
    lcd.fillRect(VALUE_X, SD_INFO_Y, 200, 8, display::colors::BLACK);
    lcd.drawString(VALUE_X, SD_INFO_Y, buf, display::colors::CYAN, display::colors::BLACK, 1);

    lcd.drawString(VALUE_X, SD_STATUS_Y, "exFAT Ready", display::colors::GREEN, display::colors::BLACK, 1);

    rendered.sdFreeMB = out.sdFreeMB;
    rendered.sdTotalMB = out.sdTotalMB;
}

void MainView::renderStorage(display::IDisplay& lcd, const LcdOutput& out, LcdOutput& rendered) {
    static const uint16_t VAL_X = VALUE_X + 54;
    char buf[28];
    uint32ToStr(buf, out.recordCount);
    char* p = buf; while (*p) p++;
    *p++ = ' '; *p++ = '(';
    uint32ToStr(p, out.totalKB);
    while (*p) p++;
    *p++ = 'K'; *p++ = 'B'; *p++ = ')'; *p = '\0';
    lcd.fillRect(VAL_X, SD_RECORDS_Y, 160, 8, display::colors::BLACK);
    lcd.drawString(VAL_X, SD_RECORDS_Y, buf, display::colors::GREEN, display::colors::BLACK, 1);

    static const uint16_t RATE_VAL_X = VALUE_X + 36;
    char rateBuf[28];
    uint32ToStr(rateBuf, out.writesPerSec);
    p = rateBuf; while (*p) p++;
    *p++ = ' '; *p++ = '(';
    uint32ToStr(p, out.kbPerSec);
    while (*p) p++;
    *p++ = 'K'; *p++ = 'B'; *p++ = ')';
    *p++ = ' '; *p++ = '/'; *p++ = 's'; *p = '\0';
    lcd.fillRect(RATE_VAL_X, SD_RATE_Y, 160, 8, display::colors::BLACK);
    lcd.drawString(RATE_VAL_X, SD_RATE_Y, rateBuf, display::colors::GREEN, display::colors::BLACK, 1);

    rendered.recordCount = out.recordCount;
    rendered.writesPerSec = out.writesPerSec;
    rendered.totalKB = out.totalKB;
    rendered.kbPerSec = out.kbPerSec;
}

void MainView::renderTime(display::IDisplay& lcd, const LcdOutput& out, LcdOutput& rendered) {
    if (out.epoch > 1577836800) {
        uint16_t color = out.timeSynced ? display::colors::CYAN : display::colors::WHITE;

        uint32_t date = SystemClock::dateYYYYMMDD(out.epoch);
        char dateBuf[12];
        snprintf(dateBuf, sizeof(dateBuf), "%04lu-%02lu-%02lu",
            (unsigned long)(date / 10000),
            (unsigned long)((date / 100) % 100),
            (unsigned long)(date % 100));
        lcd.fillRect(60, CLOCK_DATE_Y, 120, 16, display::colors::BLACK);
        lcd.drawString(60, CLOCK_DATE_Y, dateBuf, display::colors::WHITE, display::colors::BLACK, 2);

        uint8_t h, m, s;
        SystemClock::toHMS(out.epoch, h, m, s);
        char timeBuf[12];
        snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u", h, m, s);
        lcd.fillRect(72, CLOCK_TIME_Y, 96, 16, display::colors::BLACK);
        lcd.drawString(72, CLOCK_TIME_Y, timeBuf, color, display::colors::BLACK, 2);
    } else {
        uint32_t h = out.uptimeSec / 3600;
        uint32_t m = (out.uptimeSec / 60) % 60;
        uint32_t s = out.uptimeSec % 60;

        lcd.fillRect(60, CLOCK_DATE_Y, 120, 16, display::colors::BLACK);
        lcd.drawString(78, CLOCK_DATE_Y, "UPTIME", display::colors::YELLOW, display::colors::BLACK, 2);

        char timeBuf[14];
        snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu:%02lu",
            (unsigned long)h, (unsigned long)m, (unsigned long)s);
        lcd.fillRect(72, CLOCK_TIME_Y, 96, 16, display::colors::BLACK);
        lcd.drawString(72, CLOCK_TIME_Y, timeBuf, display::colors::YELLOW, display::colors::BLACK, 2);
    }

    rendered.epoch = out.epoch;
    rendered.timeSynced = out.timeSynced;
    rendered.uptimeSec = out.uptimeSec;
}

void MainView::renderMqtt(display::IDisplay& lcd, const LcdOutput& out, LcdOutput& rendered) {
    lcd.fillRect(VALUE_X, MQTT_STATUS_Y, 200, 8, display::colors::BLACK);
    if (!out.mqttKnown) {
        lcd.drawString(VALUE_X, MQTT_STATUS_Y, "---", display::colors::GRAY, display::colors::BLACK, 1);
    } else if (out.mqttConnected) {
        lcd.drawString(VALUE_X, MQTT_STATUS_Y, "Connected", display::colors::GREEN, display::colors::BLACK, 1);
    } else {
        lcd.drawString(VALUE_X, MQTT_STATUS_Y, "Disconnected", display::colors::RED, display::colors::BLACK, 1);
    }
    rendered.mqttConnected = out.mqttConnected;
    rendered.mqttKnown = out.mqttKnown;
}

void MainView::renderEcgColumn(display::IDisplay& lcd, uint8_t x, uint8_t y, uint8_t prevY) {
    // Half-resolution: cursor 0-119, each maps to 2px on 240px wide LCD
    uint16_t px = (uint16_t)x * 2;

    // Scale 0-99 → 8-92 to leave top/bottom margin
    y    = (uint8_t)(y    * 84 / 100 + 8);
    prevY = (uint8_t)(prevY * 84 / 100 + 8);
    if (y >= ECG_HEIGHT) y = ECG_HEIGHT - 1;
    if (prevY >= ECG_HEIGHT) prevY = ECG_HEIGHT - 1;

    // Erase ahead (6px = 3 half-res columns)
    uint16_t eraseX = ((x + 1) % LcdOutput::ECG_WIDTH) * 2;
    for (int i = 0; i < 6; i++) {
        lcd.fillRect(eraseX, ECG_TOP_Y, 1, ECG_HEIGHT, display::colors::BLACK);
        eraseX = (eraseX + 1) % 240;
    }

    // Draw 2px wide column
    uint8_t minY = (prevY < y) ? prevY : y;
    uint8_t maxY = (prevY > y) ? prevY : y;
    uint16_t lineH = maxY - minY + 1;
    if (lineH < 2) lineH = 2;
    lcd.fillRect(px, ECG_TOP_Y + minY, 2, lineH, display::colors::GREEN);
}

void MainView::uint32ToStr(char* buf, uint32_t value) {
    uint32_t temp = value;
    int digits = 0;
    do { digits++; temp /= 10; } while (temp > 0);
    buf[digits] = '\0';
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = '0' + (value % 10);
        value /= 10;
    }
}

} // namespace lcd
} // namespace arcana
