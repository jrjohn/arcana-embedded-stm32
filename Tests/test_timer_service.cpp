#include <gtest/gtest.h>
#include "TimerService.hpp"
#include "test_helpers.h"

using namespace arcana;

TEST(TimerServiceTest, InitCreatesTimer) {
    TimerService svc;
    svc.init(100);
    EXPECT_EQ(svc.getTickCount(), 0u);
}

TEST(TimerServiceTest, StartAndStopDoNotCrash) {
    TimerService svc;
    svc.init(100);
    svc.start();
    svc.stop();
}

TEST(TimerServiceTest, StartWithNullHandleIsSafe) {
    // Fresh service — timerHandle_ is null before init
    TimerService svc;
    svc.start();  // Should not crash (handle is nullptr)
    svc.stop();
}

TEST(TimerServiceTest, CallbackUpdatesTickCount) {
    TimerService svc;
    svc.init(50);

    // Fire callback via test helper (mock stores it in xTimerCreateStatic)
    test_fire_timer_callback();
    EXPECT_EQ(svc.getTickCount(), 1u);

    test_fire_timer_callback();
    EXPECT_EQ(svc.getTickCount(), 2u);
}

TEST(TimerServiceTest, CallbackPublishesToObservable) {
    // Start dispatcher so hasQueueSpace() returns true (queue_ != nullptr)
    ObservableDispatcher::start();

    TimerService svc;
    svc.init(100);

    // Fire callback → model_.update() + observable.publish() branch taken
    test_fire_timer_callback();
    EXPECT_EQ(svc.getTickCount(), 1u);
}
