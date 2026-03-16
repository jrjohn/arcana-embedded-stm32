#pragma once

#include "LcdService.hpp"

namespace arcana {
namespace lcd {

class LcdServiceImpl : public LcdService {
public:
    static LcdService& getInstance();

    ServiceStatus initHAL() override;
    Ili9341Lcd& getLcd() override { return mLcd; }

private:
    LcdServiceImpl();
    ~LcdServiceImpl();
    LcdServiceImpl(const LcdServiceImpl&);
    LcdServiceImpl& operator=(const LcdServiceImpl&);

    Ili9341Lcd mLcd;
};

} // namespace lcd
} // namespace arcana
