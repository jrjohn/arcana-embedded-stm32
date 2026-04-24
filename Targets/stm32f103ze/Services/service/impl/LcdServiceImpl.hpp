#pragma once

#include "LcdService.hpp"
#include "Ili9341Display.hpp"

namespace arcana {
namespace lcd {

class LcdServiceImpl : public LcdService {
public:
    static LcdService& getInstance();

    ServiceStatus initHAL() override;
    display::IDisplay& getDisplay() override { return mAdapter; }

private:
    LcdServiceImpl();
    ~LcdServiceImpl();
    LcdServiceImpl(const LcdServiceImpl&);
    LcdServiceImpl& operator=(const LcdServiceImpl&);

    Ili9341Lcd mLcd;
    display::Ili9341Display mAdapter;  // must be after mLcd
};

} // namespace lcd
} // namespace arcana
