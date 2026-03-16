#include "LedServiceImpl.hpp"

namespace arcana {
namespace led {

// Color table (R, G, B) - matches ESP32 color cycle
struct Color { uint8_t r, g, b; };
static const Color kColors[] = {
    {1, 0, 0},  // Red
    {0, 1, 0},  // Green
    {0, 0, 1},  // Blue
    {1, 1, 0},  // Yellow
    {0, 1, 1},  // Cyan
    {1, 0, 1},  // Magenta
    {1, 1, 1},  // White
    {0, 0, 0},  // Off
};
static const uint8_t kColorCount = sizeof(kColors) / sizeof(kColors[0]);

LedServiceImpl::LedServiceImpl()
    : mColorIndex(0)
    , mRunning(false)
{
}

LedServiceImpl::~LedServiceImpl() {
    stop();
}

LedService& LedServiceImpl::getInstance() {
    static LedServiceImpl sInstance;
    return sInstance;
}

ServiceStatus LedServiceImpl::initHAL() {
    // GPIO already configured by CubeMX MX_GPIO_Init()
    allOff();
    return ServiceStatus::OK;
}

ServiceStatus LedServiceImpl::init() {
    if (!input.TimerEvents) {
        return ServiceStatus::InvalidState;
    }
    input.TimerEvents->subscribe(onTimerTick, this);
    return ServiceStatus::OK;
}

ServiceStatus LedServiceImpl::start() {
    mColorIndex = 0;
    mRunning = true;
    return ServiceStatus::OK;
}

void LedServiceImpl::stop() {
    mRunning = false;
    allOff();
}

void LedServiceImpl::onTimerTick(TimerModel* /*model*/, void* context) {
    LedServiceImpl* self = static_cast<LedServiceImpl*>(context);
    if (!self->mRunning) return;

    const Color& c = kColors[self->mColorIndex];
    self->setColor(c.r, c.g, c.b);

    self->mColorIndex++;
    if (self->mColorIndex >= kColorCount) {
        self->mColorIndex = 0;
    }
}

void LedServiceImpl::setColor(uint8_t r, uint8_t g, uint8_t b) {
    // Active low: RESET = ON, SET = OFF
    HAL_GPIO_WritePin(GPIOB, PIN_R, r ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, PIN_G, g ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, PIN_B, b ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void LedServiceImpl::allOff() {
    HAL_GPIO_WritePin(GPIOB, PIN_R | PIN_G | PIN_B, GPIO_PIN_SET);
}

} // namespace led
} // namespace arcana
