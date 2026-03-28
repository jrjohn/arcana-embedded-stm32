#include <gtest/gtest.h>
#include "Log.hpp"

using namespace arcana::log;

// Test appender that captures events
class TestAppender : public IAppender {
public:
    std::vector<LogEvent> events;
    Level minLvl;

    explicit TestAppender(Level min = Level::Trace) : minLvl(min) {}
    void append(const LogEvent& ev) override { events.push_back(ev); }
    Level minLevel() const override { return minLvl; }
};

// Platform stubs
static uint32_t sTime = 1000;
static uint32_t sTick = 500;

static uint32_t stubGetTime()  { return sTime; }
static uint32_t stubGetTick()  { return sTick; }
static void stubEnterCritical()   {}
static void stubExitCritical()    {}
static uint32_t stubEnterCriticalISR() { return 0x42; }
static void stubExitCriticalISR(uint32_t) {}

// Logger is a Meyers singleton — state persists across tests.
// All tests share ONE singleton, so we use a single test to avoid
// appender accumulation (max 4 appenders, can't remove).

TEST(LoggerTest, SingletonReturnsSameInstance) {
    Logger& a = Logger::getInstance();
    Logger& b = Logger::getInstance();
    EXPECT_EQ(&a, &b);
}

// Main comprehensive test — exercises all Logger paths in one go
TEST(LoggerTest, FullLifecycle) {
    Logger& log = Logger::getInstance();

    // 1. Init with full config
    LogConfig cfg{};
    cfg.enterCritical = stubEnterCritical;
    cfg.exitCritical = stubExitCritical;
    cfg.enterCriticalISR = stubEnterCriticalISR;
    cfg.exitCriticalISR = stubExitCriticalISR;
    cfg.getTime = stubGetTime;
    cfg.getTick = stubGetTick;
    log.init(cfg);
    log.setLevel(Level::Trace);
    sTime = 1000;
    sTick = 500;

    // Drain any leftover events from prior tests
    while (log.pending() > 0) log.drain(32);

    // 2. setLevel / getLevel
    log.setLevel(Level::Warn);
    EXPECT_EQ(log.getLevel(), Level::Warn);
    log.setLevel(Level::Trace);

    // 3. log() enqueues event
    log.log(Level::Info, arcana::ats::ErrorSource::System, 0x0001, 42);
    EXPECT_EQ(log.pending(), 1);

    // 4. logFromISR() enqueues event
    log.logFromISR(Level::Error, arcana::ats::ErrorSource::Sensor, 0x0002, 99);
    EXPECT_EQ(log.pending(), 2);

    // Drain without appender
    log.drain(2);
    EXPECT_EQ(log.pending(), 0);

    // 5. Add appender and test drain (static to survive singleton lifetime)
    static TestAppender appender;
    appender.events.clear();
    log.addAppender(&appender);

    log.log(Level::Info, arcana::ats::ErrorSource::System, 0x0010, 7);
    uint8_t drained = log.drain();
    EXPECT_EQ(drained, 1);
    ASSERT_GE(appender.events.size(), 1u);
    EXPECT_EQ(appender.events.back().code, 0x0010);
    EXPECT_EQ(appender.events.back().param, 7u);
    EXPECT_EQ(appender.events.back().timestamp, 1000u);
    EXPECT_EQ(appender.events.back().tickMs, 500u);

    // 6. Drain empty returns zero
    EXPECT_EQ(log.drain(), 0);

    // 7. Level filtering via appender
    static TestAppender warnAppender(Level::Warn);
    warnAppender.events.clear();
    log.addAppender(&warnAppender);

    log.log(Level::Debug, arcana::ats::ErrorSource::System, 0x20);  // below Warn
    log.log(Level::Warn, arcana::ats::ErrorSource::System, 0x21);   // at Warn
    log.log(Level::Fatal, arcana::ats::ErrorSource::System, 0x22);  // above Warn
    log.drain(3);

    // warnAppender only gets Warn + Fatal
    EXPECT_EQ(warnAppender.events.size(), 2u);
    EXPECT_EQ(warnAppender.events[0].code, 0x21);
    EXPECT_EQ(warnAppender.events[1].code, 0x22);

    // 8. Ring buffer overflow (ring size=32, so 31 max)
    while (log.pending() > 0) log.drain(32);
    for (int i = 0; i < 32; i++) {
        log.log(Level::Info, arcana::ats::ErrorSource::System, static_cast<uint16_t>(i));
    }
    EXPECT_EQ(log.pending(), 31);
    while (log.pending() > 0) log.drain(32);

    // 9. Null appender ignored
    log.addAppender(nullptr);

    // 10. Max appenders (already added 2, add 2 more = 4 total, then 5th should fail)
    static TestAppender extra1, extra2, extra3;
    extra1.events.clear();
    extra2.events.clear();
    extra3.events.clear();
    log.addAppender(&extra1);
    log.addAppender(&extra2);
    // At this point we have 4 appenders (appender, warnAppender, extra1, extra2)
    // 5th should be silently ignored
    log.addAppender(&extra3);

    log.log(Level::Info, arcana::ats::ErrorSource::System, 0x50);
    log.drain();
    EXPECT_GE(extra2.events.size(), 1u);
    EXPECT_EQ(extra3.events.size(), 0u);  // 5th appender not registered

    // 11. Drain max parameter
    while (log.pending() > 0) log.drain(32);
    for (int i = 0; i < 5; i++) {
        log.log(Level::Info, arcana::ats::ErrorSource::System, static_cast<uint16_t>(i));
    }
    drained = log.drain(2);
    EXPECT_EQ(drained, 2);
    EXPECT_EQ(log.pending(), 3);
    while (log.pending() > 0) log.drain(32);
}

TEST(LoggerTest, NullConfigFunctions) {
    Logger& log = Logger::getInstance();
    // Drain leftovers
    while (log.pending() > 0) log.drain(32);

    LogConfig cfg{};  // All null
    log.init(cfg);
    log.setLevel(Level::Trace);

    log.log(Level::Info, arcana::ats::ErrorSource::System, 0x01);
    log.logFromISR(Level::Error, arcana::ats::ErrorSource::Sensor, 0x02);
    EXPECT_EQ(log.pending(), 2);

    // Drain and check timestamps are 0 with null getTime/getTick
    log.drain(2);
    // We can't easily check the event content since appenders from prior test
    // are still registered, but at least it doesn't crash
    EXPECT_EQ(log.pending(), 0);
}
