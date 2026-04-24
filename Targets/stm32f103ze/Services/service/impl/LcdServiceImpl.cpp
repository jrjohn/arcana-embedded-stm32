#include "LcdServiceImpl.hpp"

namespace arcana {
namespace lcd {

LcdServiceImpl::LcdServiceImpl() : mLcd(), mAdapter(mLcd) {}
LcdServiceImpl::~LcdServiceImpl() {}

LcdService& LcdServiceImpl::getInstance() {
    static LcdServiceImpl sInstance;
    return sInstance;
}

ServiceStatus LcdServiceImpl::initHAL() {
    mLcd.initHAL();
    return ServiceStatus::OK;
}

} // namespace lcd
} // namespace arcana
