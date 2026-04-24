#pragma once
#include "DisplayConfig.hpp"
#if DISPLAY_FEATURE_MUTEX

#include "IDisplay.hpp"
#include "IMutex.hpp"   // arcana::ats::IMutex

namespace arcana {
namespace display {

class MutexDisplay : public IDisplay {
    IDisplay& mInner;
    ats::IMutex& mMutex;
public:
    MutexDisplay(IDisplay& inner, ats::IMutex& mutex)
        : mInner(inner), mMutex(mutex) {}

    uint16_t width()  const override { return mInner.width(); }
    uint16_t height() const override { return mInner.height(); }

    void fillScreen(Color c) override {
        if (mMutex.lock(10)) { mInner.fillScreen(c); mMutex.unlock(); }
    }
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, Color c) override {
        if (mMutex.lock(10)) { mInner.fillRect(x, y, w, h, c); mMutex.unlock(); }
    }
    void drawHLine(uint16_t x, uint16_t y, uint16_t w, Color c) override {
        if (mMutex.lock(10)) { mInner.drawHLine(x, y, w, c); mMutex.unlock(); }
    }
    void drawChar(uint16_t x, uint16_t y, char c, Color fg, Color bg, uint8_t s) override {
        if (mMutex.lock(10)) { mInner.drawChar(x, y, c, fg, bg, s); mMutex.unlock(); }
    }
    void drawString(uint16_t x, uint16_t y, const char* str, Color fg, Color bg, uint8_t s) override {
        if (mMutex.lock(10)) { mInner.drawString(x, y, str, fg, bg, s); mMutex.unlock(); }
    }
    void drawXBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     const uint8_t* bmp, Color fg, Color bg) override {
        if (mMutex.lock(10)) { mInner.drawXBitmap(x, y, w, h, bmp, fg, bg); mMutex.unlock(); }
    }
};

} // namespace display
} // namespace arcana

#endif // DISPLAY_FEATURE_MUTEX
