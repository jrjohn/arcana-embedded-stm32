#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace sensor {

class SensorService {
public:
    struct Output {
        Observable<SensorDataModel>* DataEvents;
    };

    Output output;

    virtual ~SensorService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

    /**
     * Enable ADC simulation mode for high-frequency testing.
     * In this mode, fake high-frequency data is generated instead of reading from real sensor.
     * @param enable true to enable simulation, false for real sensor (default)
     * @param sampleRateHz Sample rate in Hz (e.g., 10 for 10 samples/sec)
     */
    virtual void enableAdcSimulation(bool enable, uint16_t sampleRateHz = 10) {
        (void)enable;
        (void)sampleRateHz;
        // Default implementation does nothing (override in implementation)
    }

protected:
    SensorService() : output() {
        output.DataEvents = 0;
    }
};

} // namespace sensor
} // namespace arcana
