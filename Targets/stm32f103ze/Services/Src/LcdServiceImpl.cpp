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

    return ServiceStatus::OK;
}

ServiceStatus LcdServiceImpl::start() {
    return ServiceStatus::OK;
}

void LcdServiceImpl::stop() {}

void LcdServiceImpl::drawInitialScreen() {
    // Fill RED to verify LCD is alive
    mLcd.fillScreen(Ili9341Lcd::RED);

    // Title
    mLcd.drawString(30, 20, "Arcana F103", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 2);

    // Subtitle
    mLcd.drawString(30, 50, "Sensor Monitor", Ili9341Lcd::GRAY, Ili9341Lcd::BLACK, 1);

    // Separator
    mLcd.drawHLine(10, 70, 220, Ili9341Lcd::DARKGRAY);

    // Labels
    mLcd.drawString(VALUE_X, 90, "Temperature", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 2);
    mLcd.drawString(VALUE_X, TEMP_VALUE_Y, "-- C", Ili9341Lcd::YELLOW, Ili9341Lcd::BLACK, 3);

    // Separator
    mLcd.drawHLine(10, 170, 220, Ili9341Lcd::DARKGRAY);

    mLcd.drawString(VALUE_X, 190, "Humidity", Ili9341Lcd::WHITE, Ili9341Lcd::BLACK, 2);
    mLcd.drawString(VALUE_X, HUMI_VALUE_Y, "-- %", Ili9341Lcd::CYAN, Ili9341Lcd::BLACK, 3);
}

void LcdServiceImpl::onSensorData(SensorDataModel* model, void* ctx) {
    LcdServiceImpl* self = static_cast<LcdServiceImpl*>(ctx);
    self->updateSensorDisplay(model);
}

void LcdServiceImpl::updateSensorDisplay(const SensorDataModel* data) {
    char buf[12];

    // Temperature: clear and redraw value area
    intToStr(buf, static_cast<int>(data->temperature));
    // Append " C"
    char* p = buf;
    while (*p) p++;
    *p++ = ' '; *p++ = 'C'; *p = '\0';

    // Clear value area (max ~7 chars at scale 3 = 126px wide, 21px tall)
    mLcd.fillRect(VALUE_X, TEMP_VALUE_Y, 180, 24, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X, TEMP_VALUE_Y, buf, Ili9341Lcd::YELLOW, Ili9341Lcd::BLACK, 3);

    // Humidity
    intToStr(buf, static_cast<int>(data->humidity));
    p = buf;
    while (*p) p++;
    *p++ = ' '; *p++ = '%'; *p = '\0';

    mLcd.fillRect(VALUE_X, HUMI_VALUE_Y, 180, 24, Ili9341Lcd::BLACK);
    mLcd.drawString(VALUE_X, HUMI_VALUE_Y, buf, Ili9341Lcd::CYAN, Ili9341Lcd::BLACK, 3);
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
