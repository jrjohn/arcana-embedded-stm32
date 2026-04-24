/**
 * @file DeviceAppender.hpp
 * @brief Log appender — writes LIFECYCLE Error records to device.ats
 *
 * Only FATAL events are written (minLevel = Fatal).
 * Records match LIFECYCLE schema: [ts:4][evtTyp:1][evtCod:2][rsv:1][param:4] = 12 bytes.
 * Flushes immediately — FATAL events must persist before potential power loss.
 */

#pragma once

#include "Log.hpp"
#include "ats/ArcanaTsDb.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include <cstring>

namespace arcana {
namespace log {

class DeviceAppender : public IAppender {
public:
    DeviceAppender() : mDb(nullptr) {}

    /** Attach to a live device.ats ArcanaTsDb instance (channel 0 = LIFECYCLE) */
    void attach(ats::ArcanaTsDb* db) { mDb = db; }

    void detach() { mDb = nullptr; }

    void append(const LogEvent& event) override {
        if (!mDb) return;

        // LIFECYCLE: [ts:4][evtTyp:1][evtCod:2][rsv:1][param:4] = 12 bytes
        uint8_t rec[12];
        memcpy(rec, &event.timestamp, 4);
        rec[4] = static_cast<uint8_t>(ats::LifecycleEventType::Error);
        memcpy(rec + 5, &event.code, 2);
        rec[7] = event.source;  // stash ErrorSource in reserved byte
        memcpy(rec + 8, &event.param, 4);

        mDb->append(0, rec);
        mDb->flush();  // FATAL must persist immediately
    }

    Level minLevel() const override { return Level::Fatal; }

private:
    ats::ArcanaTsDb* mDb;
};

} // namespace log
} // namespace arcana
