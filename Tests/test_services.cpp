#include <gtest/gtest.h>
#include "CounterService.hpp"
#include "TimeDisplayService.hpp"
#include "Models.hpp"
#include <cstring>

using namespace arcana;

// ── CounterService ────────────────────────────────────────────────────────────

TEST(CounterServiceTest, InitWithNullObservable) {
    CounterService svc;
    svc.init(nullptr);  // Should not crash
    EXPECT_EQ(svc.getCount(), 0u);
}

TEST(CounterServiceTest, InitSubscribesAndCountsOnEvent) {
    CounterService svc;
    Observable<TimerModel> obs{"TestTimer"};

    svc.init(&obs);
    EXPECT_EQ(svc.getCount(), 0u);

    TimerModel m;
    m.update(100);
    obs.notify(&m);
    EXPECT_EQ(svc.getCount(), 1u);

    obs.notify(&m);
    obs.notify(&m);
    EXPECT_EQ(svc.getCount(), 3u);
}

TEST(CounterServiceTest, ResetClearsCount) {
    CounterService svc;
    Observable<TimerModel> obs{"TestTimer"};
    svc.init(&obs);

    TimerModel m; m.update(100);
    obs.notify(&m);
    obs.notify(&m);
    EXPECT_EQ(svc.getCount(), 2u);

    svc.reset();
    EXPECT_EQ(svc.getCount(), 0u);
}

TEST(CounterServiceTest, GetCountReturnsCurrentValue) {
    CounterService svc;
    Observable<TimerModel> obs{"TestTimer"};
    svc.init(&obs);

    TimerModel m; m.update(50);
    for (int i = 0; i < 10; i++) obs.notify(&m);
    EXPECT_EQ(svc.getCount(), 10u);
}

// ── TimeDisplayService ────────────────────────────────────────────────────────

TEST(TimeDisplayServiceTest, InitialTimeIsZero) {
    TimeDisplayService svc;
    svc.init(nullptr);
    EXPECT_EQ(svc.getTotalSeconds(), 0u);
    EXPECT_EQ(svc.getMilliseconds(), 0u);
    EXPECT_STREQ(svc.getTimeString(), "00:00:00.000");
}

TEST(TimeDisplayServiceTest, MillisecondsAccumulate) {
    TimeDisplayService svc;
    Observable<TimerModel> obs{"TestTimer"};
    svc.init(&obs);

    TimerModel m; m.update(100);  // 100ms per tick
    obs.notify(&m);
    EXPECT_EQ(svc.getMilliseconds(), 100u);
    EXPECT_EQ(svc.getTotalSeconds(), 0u);
}

TEST(TimeDisplayServiceTest, RolloverToSeconds) {
    TimeDisplayService svc;
    Observable<TimerModel> obs{"TestTimer"};
    svc.init(&obs);

    TimerModel m; m.update(100);
    for (int i = 0; i < 10; i++) obs.notify(&m);  // 10 × 100ms = 1 second

    EXPECT_EQ(svc.getTotalSeconds(), 1u);
    EXPECT_EQ(svc.getMilliseconds(), 0u);
}

TEST(TimeDisplayServiceTest, TimeComponents) {
    TimeDisplayService svc;
    Observable<TimerModel> obs{"TestTimer"};
    svc.init(&obs);

    // Tick 3661 seconds worth (1h 1m 1s) via 100ms ticks
    TimerModel m; m.update(100);
    for (int i = 0; i < 36610; i++) obs.notify(&m);

    EXPECT_EQ(svc.getHours(),   1u);
    EXPECT_EQ(svc.getMinutes(), 1u);
    EXPECT_EQ(svc.getSeconds(), 1u);
}

TEST(TimeDisplayServiceTest, TimeStringFormat) {
    TimeDisplayService svc;
    Observable<TimerModel> obs{"TestTimer"};
    svc.init(&obs);

    TimerModel m; m.update(100);
    obs.notify(&m);  // 100ms
    // Should be "00:00:00.100"
    EXPECT_STREQ(svc.getTimeString(), "00:00:00.100");
}

TEST(TimeDisplayServiceTest, ResetClearsAll) {
    TimeDisplayService svc;
    Observable<TimerModel> obs{"TestTimer"};
    svc.init(&obs);

    TimerModel m; m.update(100);
    for (int i = 0; i < 10; i++) obs.notify(&m);
    EXPECT_EQ(svc.getTotalSeconds(), 1u);

    svc.reset();
    EXPECT_EQ(svc.getTotalSeconds(), 0u);
    EXPECT_EQ(svc.getMilliseconds(), 0u);
    EXPECT_STREQ(svc.getTimeString(), "00:00:00.000");
}

TEST(TimeDisplayServiceTest, NullModelDoesNotCrash) {
    TimeDisplayService svc;
    Observable<TimerModel> obs{"TestTimer"};
    svc.init(&obs);
    // Manually call notify with nullptr (should be guarded in callback)
    obs.notify(nullptr);
    EXPECT_EQ(svc.getTotalSeconds(), 0u);
}
