#include "Esp8266.hpp"

namespace arcana {

Esp8266& Esp8266::getInstance() {
    static Esp8266 sInstance;
    return sInstance;
}

} // namespace arcana
