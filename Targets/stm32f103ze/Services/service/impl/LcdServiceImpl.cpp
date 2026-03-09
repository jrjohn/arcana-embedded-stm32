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

    return ServiceStatus::OK;
}

ServiceStatus LcdServiceImpl::start() {
    return ServiceStatus::OK;
}

void LcdServiceImpl::stop() {}

void LcdServiceImpl::drawInitialScreen() {
    mLcd.fillScreen(Ili9341Lcd::BLACK);

    // Title
    mLcd.drawString(30, 10, "Arcana F103", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 2);

    // Subtitle
    mLcd.drawString(30, 35, "Sensor Monitor", Ili9341Lcd::GRAY, Ili9341Lcd::BLACK, 1);

    // === Temperature section ===
    mLcd.drawHLine(10, 55, 220, Ili9341Lcd::DARKGRAY);
    mLcd.drawString(VALUE_X, 65, "Temperature", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 2);
    mLcd.drawString(VALUE_X, TEMP_VALUE_Y, "-- C", Ili9341Lcd::YELLOW, Ili9341Lcd::BLACK, 3);

    // === Light section ===
    mLcd.drawHLine(10, 135, 220, Ili9341Lcd::DARKGRAY);
    mLcd.drawString(VALUE_X, 145, "Light", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 2);
    mLcd.drawString(VALUE_X, LIGHT_VALUE_Y, "-- lux", Ili9341Lcd::CYAN, Ili9341Lcd::BLACK, 3);

    // === Storage section ===
    mLcd.drawHLine(10, 215, 220, Ili9341Lcd::DARKGRAY);
    mLcd.drawString(VALUE_X, 225, "Storage (ChaCha20)", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 1);
    mLcd.drawString(VALUE_X, 240, "Records:", Ili9341Lcd::GRAY, Ili9341Lcd::BLACK, 1);
    mLcd.drawString(VALUE_X, STORAGE_VALUE_Y, "0", Ili9341Lcd::GREEN, Ili9341Lcd::BLACK, 2);
    mLcd.drawString(VALUE_X, 285, "Rate:", Ili9341Lcd::GRAY, Ili9341Lcd::BLACK, 1);
    mLcd.drawString(VALUE_X + 40, 285, "-- /s", Ili9341Lcd::GREEN, Ili9341Lcd::BLACK, 1);
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

void LcdServiceImpl::updateSensorDisplay(const SensorDataModel* data) {
    char buf[12];

    intToStr(buf, static_cast<int>(data->temperature));
    char* p = buf;
    while (*p) p++;
    *p++ = ' '; *p++ = 'C'; *p = '\0';

    mLcd.fillRect(VALUE_X, TEMP_VALUE_Y, 180, 24, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X, TEMP_VALUE_Y, buf, Ili9341Lcd::YELLOW, Ili9341Lcd::BLACK, 3);
}

void LcdServiceImpl::updateLightDisplay(const LightDataModel* data) {
    char buf[16];

    intToStr(buf, static_cast<int>(data->ambientLight));
    char* p = buf;
    while (*p) p++;
    *p++ = ' '; *p++ = 'l'; *p++ = 'u'; *p++ = 'x'; *p = '\0';

    mLcd.fillRect(VALUE_X, LIGHT_VALUE_Y, 200, 24, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X, LIGHT_VALUE_Y, buf, Ili9341Lcd::CYAN, Ili9341Lcd::BLACK, 3);
}

void LcdServiceImpl::updateStorageDisplay(const StorageStatsModel* data) {
    char buf[20];

    // Record count (scale 2 = 12×16 px per char)
    uint32ToStr(buf, data->recordCount);
    mLcd.fillRect(VALUE_X, STORAGE_VALUE_Y, 200, 16, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X, STORAGE_VALUE_Y, buf, Ili9341Lcd::GREEN, Ili9341Lcd::BLACK, 2);

    // Write rate (scale 1 = 6×8 px per char)
    char rateBuf[16];
    uint32ToStr(rateBuf, data->writesPerSec);
    char* p = rateBuf;
    while (*p) p++;
    *p++ = ' '; *p++ = '/'; *p++ = 's'; *p = '\0';

    mLcd.fillRect(VALUE_X + 40, 285, 100, 8, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X + 40, 285, rateBuf, Ili9341Lcd::GREEN, Ili9341Lcd::BLACK, 1);
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
