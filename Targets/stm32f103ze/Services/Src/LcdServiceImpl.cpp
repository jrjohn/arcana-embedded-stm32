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

    return ServiceStatus::OK;
}

ServiceStatus LcdServiceImpl::start() {
    return ServiceStatus::OK;
}

void LcdServiceImpl::stop() {}

void LcdServiceImpl::drawInitialScreen() {
    mLcd.fillScreen(Ili9341Lcd::BLACK);

    // Title
    mLcd.drawString(30, 20, "Arcana F103", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 2);

    // Subtitle
    mLcd.drawString(30, 50, "Sensor Monitor", Ili9341Lcd::GRAY, Ili9341Lcd::BLACK, 1);

    // Separator
    mLcd.drawHLine(10, 70, 220, Ili9341Lcd::DARKGRAY);

    // Temperature (MPU6050)
    mLcd.drawString(VALUE_X, 90, "Temperature", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 2);
    mLcd.drawString(VALUE_X, TEMP_VALUE_Y, "-- C", Ili9341Lcd::YELLOW, Ili9341Lcd::BLACK, 3);

    // Separator
    mLcd.drawHLine(10, 170, 220, Ili9341Lcd::DARKGRAY);

    // Light (AP3216C)
    mLcd.drawString(VALUE_X, 190, "Light", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 2);
    mLcd.drawString(VALUE_X, LIGHT_VALUE_Y, "-- lux", Ili9341Lcd::CYAN, Ili9341Lcd::BLACK, 3);
}

void LcdServiceImpl::onSensorData(SensorDataModel* model, void* ctx) {
    LcdServiceImpl* self = static_cast<LcdServiceImpl*>(ctx);
    self->updateSensorDisplay(model);
}

void LcdServiceImpl::onLightData(LightDataModel* model, void* ctx) {
    LcdServiceImpl* self = static_cast<LcdServiceImpl*>(ctx);
    self->updateLightDisplay(model);
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

} // namespace lcd
} // namespace arcana
