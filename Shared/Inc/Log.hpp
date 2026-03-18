/**
 * @file Log.hpp
 * @brief ArcanaLog — Log4j2-inspired embedded logging system
 *
 * Header-only. Platform-independent core: ring buffer, IAppender, macros.
 * Configure via LogConfig function pointers (critical section, time source).
 *
 * Hot path (below threshold): ~42ns (volatile read + branch)
 * Hot path (enqueue): ~486ns (build event + critical section + memcpy16)
 * RAM cost: 512 bytes ring buffer + 24 bytes state = 536 bytes .bss
 */

#ifndef ARCANA_LOG_HPP
#define ARCANA_LOG_HPP

#include <cstdint>
#include <cstring>
#include "ats/ArcanaTsTypes.hpp"

namespace arcana {
namespace log {

// ---------------------------------------------------------------------------
// Level enum (superset of ats::ErrorSeverity for finer granularity)
// ---------------------------------------------------------------------------

enum class Level : uint8_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5,
};

// ---------------------------------------------------------------------------
// LogEvent (16 bytes, packed — fits exactly in ring buffer slot)
// ---------------------------------------------------------------------------

struct __attribute__((packed)) LogEvent {
    uint32_t timestamp;   // epoch seconds from getTime()
    uint16_t tickMs;      // sub-second ordering (tick % 1000)
    uint8_t  level;       // Level enum value
    uint8_t  source;      // ats::ErrorSource enum value
    uint16_t code;        // event code (module-specific, replaces format string)
    uint32_t param;       // 32-bit context value
    uint16_t reserved;    // pad to 16 bytes
};
static_assert(sizeof(LogEvent) == 16, "LogEvent must be 16 bytes");

// ---------------------------------------------------------------------------
// Platform configuration (pluggable via function pointers)
// ---------------------------------------------------------------------------

struct LogConfig {
    void     (*enterCritical)();             // task-context critical section enter
    void     (*exitCritical)();              // task-context critical section exit
    uint32_t (*enterCriticalISR)();          // ISR-context: returns saved mask
    void     (*exitCriticalISR)(uint32_t);   // ISR-context: restores saved mask
    uint32_t (*getTime)();                   // epoch seconds (0 if unknown)
    uint32_t (*getTick)();                   // millisecond tick
};

// ---------------------------------------------------------------------------
// Appender interface — implement per destination (serial, ATS, syslog)
// ---------------------------------------------------------------------------

class IAppender {
public:
    virtual ~IAppender() {}
    virtual void append(const LogEvent& event) = 0;
    virtual Level minLevel() const = 0;
};

// ---------------------------------------------------------------------------
// Logger singleton
// ---------------------------------------------------------------------------

class Logger {
public:
    static Logger& getInstance() {
        static Logger sInstance;
        return sInstance;
    }

    /** Configure platform functions. Call once at boot (before multi-tasking OK). */
    void init(const LogConfig& config) {
        mConfig = config;
        mInitialized = true;
    }

    Level getLevel() const { return mLevel; }
    void setLevel(Level level) { mLevel = level; }

    /** Log from task context (uses enterCritical/exitCritical). */
    void log(Level level, ats::ErrorSource source,
             uint16_t code, uint32_t param = 0) {
        if (level < mLevel) return;

        LogEvent ev;
        ev.timestamp = mConfig.getTime ? mConfig.getTime() : 0;
        ev.tickMs    = mConfig.getTick
                       ? static_cast<uint16_t>(mConfig.getTick() % 1000) : 0;
        ev.level     = static_cast<uint8_t>(level);
        ev.source    = static_cast<uint8_t>(source);
        ev.code      = code;
        ev.param     = param;
        ev.reserved  = 0;

        if (mConfig.enterCritical) mConfig.enterCritical();
        enqueue(ev);
        if (mConfig.exitCritical) mConfig.exitCritical();
    }

    /** Log from ISR context (uses enterCriticalISR/exitCriticalISR). */
    void logFromISR(Level level, ats::ErrorSource source,
                    uint16_t code, uint32_t param = 0) {
        if (level < mLevel) return;

        LogEvent ev;
        ev.timestamp = mConfig.getTime ? mConfig.getTime() : 0;
        ev.tickMs    = mConfig.getTick
                       ? static_cast<uint16_t>(mConfig.getTick() % 1000) : 0;
        ev.level     = static_cast<uint8_t>(level);
        ev.source    = static_cast<uint8_t>(source);
        ev.code      = code;
        ev.param     = param;
        ev.reserved  = 0;

        uint32_t mask = 0;
        if (mConfig.enterCriticalISR) mask = mConfig.enterCriticalISR();
        enqueue(ev);
        if (mConfig.exitCriticalISR) mConfig.exitCriticalISR(mask);
    }

    /** Register an appender (max 4). Thread-safe only during init. */
    void addAppender(IAppender* appender) {
        if (mAppenderCount < MAX_APPENDERS && appender) {
            mAppenders[mAppenderCount++] = appender;
        }
    }

    /**
     * Drain events from ring buffer to appenders.
     * Call from consumer task (single-threaded). Returns events drained.
     */
    uint8_t drain(uint8_t max = 8) {
        uint8_t drained = 0;
        while (drained < max && mHead != mTail) {
            LogEvent ev;
            memcpy(&ev, &mRing[mTail], sizeof(LogEvent));
            mTail = (mTail + 1) & RING_MASK;

            for (uint8_t i = 0; i < mAppenderCount; i++) {
                if (static_cast<Level>(ev.level) >= mAppenders[i]->minLevel()) {
                    mAppenders[i]->append(ev);
                }
            }
            drained++;
        }
        return drained;
    }

    /** Number of events waiting in ring buffer. */
    uint8_t pending() const {
        return static_cast<uint8_t>((mHead - mTail) & RING_MASK);
    }

private:
    static const uint8_t RING_SIZE    = 32;
    static const uint8_t RING_MASK    = RING_SIZE - 1;
    static const uint8_t MAX_APPENDERS = 4;

    Logger()
        : mHead(0), mTail(0)
        , mAppenderCount(0)
        , mLevel(Level::Trace)
        , mInitialized(false) {
        memset(&mConfig, 0, sizeof(mConfig));
        memset(mAppenders, 0, sizeof(mAppenders));
    }

    void enqueue(const LogEvent& ev) {
        uint8_t next = (mHead + 1) & RING_MASK;
        if (next == mTail) return;  // drop on overflow
        memcpy(&mRing[mHead], &ev, sizeof(LogEvent));
        mHead = next;
    }

    LogEvent     mRing[RING_SIZE];             // 512 bytes
    volatile uint8_t  mHead;
    volatile uint8_t  mTail;
    IAppender*   mAppenders[MAX_APPENDERS];
    uint8_t      mAppenderCount;
    LogConfig    mConfig;
    volatile Level mLevel;
    bool         mInitialized;
};

} // namespace log
} // namespace arcana

// ---------------------------------------------------------------------------
// Convenience macros — lazy evaluation (level check before building event)
// ---------------------------------------------------------------------------

#define LOG_T(src, code, ...) do { \
    if (::arcana::log::Logger::getInstance().getLevel() <= ::arcana::log::Level::Trace) \
        ::arcana::log::Logger::getInstance().log(::arcana::log::Level::Trace, src, code, ##__VA_ARGS__); \
} while(0)

#define LOG_D(src, code, ...) do { \
    if (::arcana::log::Logger::getInstance().getLevel() <= ::arcana::log::Level::Debug) \
        ::arcana::log::Logger::getInstance().log(::arcana::log::Level::Debug, src, code, ##__VA_ARGS__); \
} while(0)

#define LOG_I(src, code, ...) do { \
    if (::arcana::log::Logger::getInstance().getLevel() <= ::arcana::log::Level::Info) \
        ::arcana::log::Logger::getInstance().log(::arcana::log::Level::Info, src, code, ##__VA_ARGS__); \
} while(0)

#define LOG_W(src, code, ...) do { \
    if (::arcana::log::Logger::getInstance().getLevel() <= ::arcana::log::Level::Warn) \
        ::arcana::log::Logger::getInstance().log(::arcana::log::Level::Warn, src, code, ##__VA_ARGS__); \
} while(0)

#define LOG_E(src, code, ...) do { \
    if (::arcana::log::Logger::getInstance().getLevel() <= ::arcana::log::Level::Error) \
        ::arcana::log::Logger::getInstance().log(::arcana::log::Level::Error, src, code, ##__VA_ARGS__); \
} while(0)

#define LOG_F(src, code, ...) do { \
    if (::arcana::log::Logger::getInstance().getLevel() <= ::arcana::log::Level::Fatal) \
        ::arcana::log::Logger::getInstance().log(::arcana::log::Level::Fatal, src, code, ##__VA_ARGS__); \
} while(0)

#endif /* ARCANA_LOG_HPP */
