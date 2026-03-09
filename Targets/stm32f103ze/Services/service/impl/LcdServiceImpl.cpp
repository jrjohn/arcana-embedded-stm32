#include "LcdServiceImpl.hpp"

namespace arcana {
namespace lcd {

LcdServiceImpl::LcdServiceImpl()
    : mLcd()
{
}

LcdServiceImpl::~LcdServiceImpl() {}

LcdService& LcdServiceImpl::getInstance() {
    static LcdServiceImpl sInstance;
    return sInstance;
}

ServiceStatus LcdServiceImpl::initHAL() {
    mLcd.initHAL();
    return ServiceStatus::OK;
}

ServiceStatus LcdServiceImpl::init() {
    drawInitialScreen();

    if (input.SensorData) {
        input.SensorData->subscribe(onSensorData, this);
    }
    if (input.LightData) {
        input.LightData->subscribe(onLightData, this);
    }
    if (input.StorageStats) {
        input.StorageStats->subscribe(onStorageStats, this);
    }
    if (input.SdBenchmark) {
        input.SdBenchmark->subscribe(onSdBenchmark, this);
    }

    return ServiceStatus::OK;
}

ServiceStatus LcdServiceImpl::start() {
    return ServiceStatus::OK;
}

void LcdServiceImpl::stop() {}

void LcdServiceImpl::drawInitialScreen() {
    mLcd.fillScreen(Ili9341Lcd::BLACK);

    // Title
    mLcd.drawString(30, 4, "Arcana F103", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 2);

    // === Temperature section ===
    mLcd.drawHLine(10, 24, 220, Ili9341Lcd::DARKGRAY);
    mLcd.drawString(VALUE_X, 30, "Temperature", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 1);
    mLcd.drawString(VALUE_X, TEMP_VALUE_Y, "-- C", Ili9341Lcd::YELLOW, Ili9341Lcd::BLACK, 2);

    // === SD Benchmark section ===
    mLcd.drawHLine(10, 62, 220, Ili9341Lcd::DARKGRAY);
    mLcd.drawString(VALUE_X, 68, "SD Bench (ChaCha20)", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 1);
    mLcd.drawString(VALUE_X, SD_SPEED_Y, "-- KB/s", Ili9341Lcd::CYAN, Ili9341Lcd::BLACK, 2);

    mLcd.drawString(VALUE_X, SD_TOTAL_Y, "Written:", Ili9341Lcd::GRAY, Ili9341Lcd::BLACK, 1);
    mLcd.drawString(VALUE_X + 60, SD_TOTAL_Y, "0 KB", Ili9341Lcd::GREEN, Ili9341Lcd::BLACK, 1);

    mLcd.drawString(VALUE_X, SD_RECORDS_Y, "Records:", Ili9341Lcd::GRAY, Ili9341Lcd::BLACK, 1);
    mLcd.drawString(VALUE_X + 60, SD_RECORDS_Y, "0", Ili9341Lcd::GREEN, Ili9341Lcd::BLACK, 1);

    mLcd.drawString(VALUE_X, SD_RATE_Y, "Rate:", Ili9341Lcd::GRAY, Ili9341Lcd::BLACK, 1);
    mLcd.drawString(VALUE_X + 40, SD_RATE_Y, "-- /s", Ili9341Lcd::GREEN, Ili9341Lcd::BLACK, 1);

    // === MQTT section ===
    mLcd.drawHLine(10, 136, 220, Ili9341Lcd::DARKGRAY);
    mLcd.drawString(VALUE_X, MQTT_LABEL_Y, "WiFi / MQTT", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 1);
    mLcd.drawString(VALUE_X, MQTT_STATUS_Y, "Idle", Ili9341Lcd::GRAY, Ili9341Lcd::BLACK, 1);
}

void LcdServiceImpl::onSensorData(SensorDataModel* model, void* ctx) {
    LcdServiceImpl* self = static_cast<LcdServiceImpl*>(ctx);
    self->updateSensorDisplay(model);
}

void LcdServiceImpl::onLightData(LightDataModel* model, void* ctx) {
    LcdServiceImpl* self = static_cast<LcdServiceImpl*>(ctx);
    self->updateLightDisplay(model);
}

void LcdServiceImpl::onStorageStats(StorageStatsModel* model, void* ctx) {
    LcdServiceImpl* self = static_cast<LcdServiceImpl*>(ctx);
    self->updateStorageDisplay(model);
}

void LcdServiceImpl::onSdBenchmark(SdBenchmarkModel* model, void* ctx) {
    LcdServiceImpl* self = static_cast<LcdServiceImpl*>(ctx);
    self->updateSdBenchmarkDisplay(model);
}

void LcdServiceImpl::updateSensorDisplay(const SensorDataModel* data) {
    char buf[12];
    intToStr(buf, static_cast<int>(data->temperature));
    char* p = buf;
    while (*p) p++;
    *p++ = ' '; *p++ = 'C'; *p = '\0';

    mLcd.fillRect(VALUE_X, TEMP_VALUE_Y, 180, 16, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X, TEMP_VALUE_Y, buf, Ili9341Lcd::YELLOW, Ili9341Lcd::BLACK, 2);
}

void LcdServiceImpl::updateLightDisplay(const LightDataModel* data) {
    (void)data;  // Light section removed in benchmark mode
}

void LcdServiceImpl::updateSdBenchmarkDisplay(const SdBenchmarkModel* data) {
    char buf[24];

    if (data->error) {
        // Format: "E3:14" = step 3, FRESULT 14
        char errBuf[12];
        errBuf[0] = 'E';
        errBuf[1] = '0' + data->errorStep;
        errBuf[2] = ':';
        uint32ToStr(errBuf + 3, data->errorCode);
        mLcd.fillRect(VALUE_X, SD_SPEED_Y, 200, 16, Ili9341Lcd::BLACK);
        mLcd.drawString(VALUE_X, SD_SPEED_Y, errBuf, Ili9341Lcd::RED, Ili9341Lcd::BLACK, 2);
        return;
    }

    // Speed: format as "xxxx.x KB/s"
    uint32_t intPart = data->speedKBps10 / 10;
    uint32_t fracPart = data->speedKBps10 % 10;
    uint32ToStr(buf, intPart);
    char* p = buf;
    while (*p) p++;
    *p++ = '.';
    *p++ = '0' + fracPart;
    *p++ = ' ';
    *p++ = 'K';
    *p++ = 'B';
    *p++ = '/';
    *p++ = 's';
    *p = '\0';

    mLcd.fillRect(VALUE_X, SD_SPEED_Y, 200, 16, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X, SD_SPEED_Y, buf, Ili9341Lcd::CYAN, Ili9341Lcd::BLACK, 2);

    // Total written
    uint32ToStr(buf, data->totalKB);
    p = buf;
    while (*p) p++;
    *p++ = ' '; *p++ = 'K'; *p++ = 'B'; *p = '\0';

    mLcd.fillRect(VALUE_X + 60, SD_TOTAL_Y, 160, 8, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X + 60, SD_TOTAL_Y, buf, Ili9341Lcd::GREEN, Ili9341Lcd::BLACK, 1);

    // Total records
    uint32ToStr(buf, data->totalRecords);
    mLcd.fillRect(VALUE_X + 60, SD_RECORDS_Y, 160, 8, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X + 60, SD_RECORDS_Y, buf, Ili9341Lcd::GREEN, Ili9341Lcd::BLACK, 1);

    // Records/sec
    uint32ToStr(buf, data->recordsPerSec);
    p = buf;
    while (*p) p++;
    *p++ = ' '; *p++ = '/'; *p++ = 's'; *p = '\0';
    mLcd.fillRect(VALUE_X + 40, SD_RATE_Y, 160, 8, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X + 40, SD_RATE_Y, buf, Ili9341Lcd::GREEN, Ili9341Lcd::BLACK, 1);
}

void LcdServiceImpl::updateStorageDisplay(const StorageStatsModel* data) {
    char buf[20];

    uint32ToStr(buf, data->recordCount);
    mLcd.fillRect(VALUE_X + 60, SD_RECORDS_Y, 160, 8, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X + 60, SD_RECORDS_Y, buf, Ili9341Lcd::GREEN, Ili9341Lcd::BLACK, 1);

    char rateBuf[16];
    uint32ToStr(rateBuf, data->writesPerSec);
    char* p = rateBuf;
    while (*p) p++;
    *p++ = ' '; *p++ = '/'; *p++ = 's'; *p = '\0';

    mLcd.fillRect(VALUE_X + 40, SD_RATE_Y, 160, 8, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X + 40, SD_RATE_Y, rateBuf, Ili9341Lcd::GREEN, Ili9341Lcd::BLACK, 1);
}

void LcdServiceImpl::intToStr(char* buf, int value) {
    if (value < 0) {
        *buf++ = '-';
        value = -value;
    }
    int temp = value;
    int digits = 1;
    while (temp >= 10) { temp /= 10; digits++; }
    buf[digits] = '\0';
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = '0' + (value % 10);
        value /= 10;
    }
}

void LcdServiceImpl::uint32ToStr(char* buf, uint32_t value) {
    uint32_t temp = value;
    int digits = 1;
    while (temp >= 10) { temp /= 10; digits++; }
    buf[digits] = '\0';
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = '0' + (value % 10);
        value /= 10;
    }
}

} // namespace lcd
} // namespace arcana
