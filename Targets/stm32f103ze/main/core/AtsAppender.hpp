/**
 * @file AtsAppender.hpp
 * @brief Log appender — writes ERROR_LOG records to sensor.ats channel
 *
 * Only WARN+ events are written (minLevel = Warn).
 * Records match ERROR_LOG schema: [ts:4][sev:1][src:1][errCod:2][param:4] = 12 bytes.
 * Flush is deferred to the normal 1-second flush cycle.
 */

#pragma once

#include "Log.hpp"
#include "ats/ArcanaTsDb.hpp"
#include <cstring>

namespace arcana {
namespace log {

class AtsAppender : public IAppender {
public:
    AtsAppender() : mDb(nullptr), mChannel(0) {}

    /** Attach to a live ArcanaTsDb instance + ERROR_LOG channel ID */
    void attach(ats::ArcanaTsDb* db, uint8_t channel) {
        mDb = db;
        mChannel = channel;
    }

    void detach() { mDb = nullptr; }

    void append(const LogEvent& event) override {
        if (!mDb) return;

        // ERROR_LOG: [ts:4][sev:1][src:1][errCod:2][param:4] = 12 bytes
        uint8_t rec[12];
        memcpy(rec, &event.timestamp, 4);
        rec[4] = toSeverity(event.level);
        rec[5] = event.source;
        memcpy(rec + 6, &event.code, 2);
        memcpy(rec + 8, &event.param, 4);

        mDb->append(mChannel, rec);
        // ERROR+ flush immediately — must survive power loss
        if (static_cast<Level>(event.level) >= Level::Error) {
            mDb->flush();
        }
    }

    Level minLevel() const override { return Level::Warn; }

private:
    ats::ArcanaTsDb* mDb;
    uint8_t mChannel;

    /** Map log::Level → ats::ErrorSeverity for on-disk record */
    static uint8_t toSeverity(uint8_t level) {
        if (level <= static_cast<uint8_t>(Level::Info))
            return static_cast<uint8_t>(ats::ErrorSeverity::Info);
        if (level == static_cast<uint8_t>(Level::Warn))
            return static_cast<uint8_t>(ats::ErrorSeverity::Warn);
        if (level == static_cast<uint8_t>(Level::Error))
            return static_cast<uint8_t>(ats::ErrorSeverity::Error);
        return static_cast<uint8_t>(ats::ErrorSeverity::Fatal);
    }
};

} // namespace log
} // namespace arcana
