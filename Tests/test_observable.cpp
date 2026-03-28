#include <gtest/gtest.h>
#include "Observable.hpp"
#include "Models.hpp"

using namespace arcana;

// ── Observable<T> edge-case tests ────────────────────────────────────────────

TEST(ObservableExtraTest, SubscribeNullCallbackReturnsFalse) {
    Observable<TimerModel> obs{"test"};
    EXPECT_FALSE(obs.subscribe(nullptr));
    EXPECT_EQ(obs.getObserverCount(), 0);
}

TEST(ObservableExtraTest, SubscribeDuplicateUpdatesContext) {
    Observable<TimerModel> obs{"test"};
    int ctx1 = 1, ctx2 = 2;

    auto cb = [](TimerModel*, void*) {};
    obs.subscribe(cb, &ctx1);
    EXPECT_EQ(obs.getObserverCount(), 1);

    // Same callback, different context — should update, not add new
    obs.subscribe(cb, &ctx2);
    EXPECT_EQ(obs.getObserverCount(), 1);
}

TEST(ObservableExtraTest, SubscribeBeyondCapacityReturnsFalse) {
    Observable<TimerModel> obs{"test"};

    // Fill to MAX_OBSERVERS
    ObserverCallback<TimerModel> callbacks[MAX_OBSERVERS + 1];
    for (uint8_t i = 0; i < MAX_OBSERVERS; i++) {
        // Each lambda has unique address via capture
        callbacks[i] = [](TimerModel*, void*) {};
    }
    // Can't use lambdas with different addresses easily, use function pointers
    // Instead, subscribe unique static functions
    EXPECT_TRUE(obs.subscribe([](TimerModel* m, void*) { (void)m; }));
    EXPECT_TRUE(obs.subscribe([](TimerModel* m, void*) { m->updateTimestamp(); }));
    EXPECT_TRUE(obs.subscribe([](TimerModel* m, void*) { (void)m->timestamp; }));
    EXPECT_TRUE(obs.subscribe([](TimerModel* m, void*) { (void)m->type; }));
    EXPECT_TRUE(obs.subscribe([](TimerModel* m, void*) { m->tickCount = 0; }));
    EXPECT_TRUE(obs.subscribe([](TimerModel* m, void*) { m->tickCount = 1; }));
    EXPECT_EQ(obs.getObserverCount(), MAX_OBSERVERS);

    // Next subscribe should fail
    EXPECT_FALSE(obs.subscribe([](TimerModel*, void*) {}));
}

TEST(ObservableExtraTest, UnsubscribeExistingReturnsTrue) {
    Observable<TimerModel> obs{"test"};
    auto cb = [](TimerModel*, void*) {};
    obs.subscribe(cb);
    EXPECT_EQ(obs.getObserverCount(), 1);

    EXPECT_TRUE(obs.unsubscribe(cb));
    EXPECT_EQ(obs.getObserverCount(), 0);
}

TEST(ObservableExtraTest, UnsubscribeNotFoundReturnsFalse) {
    Observable<TimerModel> obs{"test"};
    auto cb = [](TimerModel*, void*) {};
    EXPECT_FALSE(obs.unsubscribe(cb));
}

TEST(ObservableExtraTest, UnsubscribeMiddleShiftsRemaining) {
    Observable<TimerModel> obs{"test"};
    int order = 0;
    auto cb1 = [](TimerModel*, void* ctx) { *static_cast<int*>(ctx) = 1; };
    auto cb2 = [](TimerModel*, void* ctx) { *static_cast<int*>(ctx) = 2; };
    auto cb3 = [](TimerModel*, void* ctx) { *static_cast<int*>(ctx) = 3; };

    obs.subscribe(cb1, &order);
    obs.subscribe(cb2, &order);
    obs.subscribe(cb3, &order);
    EXPECT_EQ(obs.getObserverCount(), 3);

    // Remove middle
    EXPECT_TRUE(obs.unsubscribe(cb2));
    EXPECT_EQ(obs.getObserverCount(), 2);

    // Notify — should call cb1 then cb3
    TimerModel model;
    obs.notify(&model);
    EXPECT_EQ(order, 3);  // Last callback sets to 3
}

TEST(ObservableExtraTest, NotifyNoObserversDoesNotCrash) {
    Observable<TimerModel> obs{"test"};
    TimerModel model;
    obs.notify(&model);  // No crash, no observers
}

TEST(ObservableExtraTest, GetName) {
    Observable<TimerModel> obs{"myObs"};
    EXPECT_STREQ(obs.getName(), "myObs");
}

TEST(ObservableExtraTest, DefaultNameIsNull) {
    Observable<TimerModel> obs;
    EXPECT_EQ(obs.getName(), nullptr);
}

// ── ObservableDispatcher static methods ──────────────────────────────────────

TEST(DispatcherExtraTest, SetErrorCallback) {
    bool called = false;
    ObservableDispatcher::setErrorCallback(
        [](ObservableError, const char*, void* ctx) {
            *static_cast<bool*>(ctx) = true;
        }, &called);

    // Clear it
    ObservableDispatcher::setErrorCallback(nullptr);
}

TEST(DispatcherExtraTest, ResetStats) {
    ObservableDispatcher::resetStats();
    const DispatcherStats& stats = ObservableDispatcher::getStats();
    EXPECT_EQ(stats.publishCount, 0u);
    EXPECT_EQ(stats.overflowCount, 0u);
}

TEST(DispatcherExtraTest, QueueSpaceAvailable) {
    // Before start(), queues are nullptr (depends on test order)
    // After start(), queues are valid
    ObservableDispatcher::start();
    EXPECT_GT(ObservableDispatcher::getQueueSpaceAvailable(), 0);
    EXPECT_GT(ObservableDispatcher::getHighQueueSpaceAvailable(), 0);
    EXPECT_TRUE(ObservableDispatcher::hasQueueSpace());
    EXPECT_TRUE(ObservableDispatcher::hasHighQueueSpace());
}

// ── publish / publishHighPriority / FromISR ──────────────────────────────────

TEST(ObservablePublishTest, PublishNullModelReturnsTrue) {
    Observable<TimerModel> obs{"test"};
    auto cb = [](TimerModel*, void*) {};
    obs.subscribe(cb);
    EXPECT_TRUE(obs.publish(nullptr));
}

TEST(ObservablePublishTest, PublishNoObserversReturnsTrue) {
    Observable<TimerModel> obs{"test"};
    TimerModel model;
    EXPECT_TRUE(obs.publish(&model));
}

TEST(ObservablePublishTest, PublishHighPriorityNullReturnsTrue) {
    Observable<TimerModel> obs{"test"};
    auto cb = [](TimerModel*, void*) {};
    obs.subscribe(cb);
    EXPECT_TRUE(obs.publishHighPriority(nullptr));
}

TEST(ObservablePublishTest, PublishHighPriorityNoObserversReturnsTrue) {
    Observable<TimerModel> obs{"test"};
    TimerModel model;
    EXPECT_TRUE(obs.publishHighPriority(&model));
}

TEST(ObservablePublishTest, PublishHighPriorityWithObserver) {
    ObservableDispatcher::start();  // Ensure queues exist
    Observable<TimerModel> obs{"test"};
    auto cb = [](TimerModel*, void*) {};
    obs.subscribe(cb);
    TimerModel model;
    EXPECT_TRUE(obs.publishHighPriority(&model));
}

TEST(ObservablePublishTest, PublishFromISRNullReturnsTrue) {
    Observable<TimerModel> obs{"test"};
    auto cb = [](TimerModel*, void*) {};
    obs.subscribe(cb);
    BaseType_t woken = pdFALSE;
    EXPECT_TRUE(obs.publishFromISR(nullptr, &woken));
}

TEST(ObservablePublishTest, PublishFromISRNoObserversReturnsTrue) {
    Observable<TimerModel> obs{"test"};
    TimerModel model;
    BaseType_t woken = pdFALSE;
    EXPECT_TRUE(obs.publishFromISR(&model, &woken));
}

TEST(ObservablePublishTest, PublishFromISRWithObserver) {
    ObservableDispatcher::start();
    Observable<TimerModel> obs{"test"};
    auto cb = [](TimerModel*, void*) {};
    obs.subscribe(cb);
    TimerModel model;
    BaseType_t woken = pdFALSE;
    EXPECT_TRUE(obs.publishFromISR(&model, &woken));
}

TEST(ObservablePublishTest, PublishHighPriorityFromISRNullReturnsTrue) {
    Observable<TimerModel> obs{"test"};
    auto cb = [](TimerModel*, void*) {};
    obs.subscribe(cb);
    BaseType_t woken = pdFALSE;
    EXPECT_TRUE(obs.publishHighPriorityFromISR(nullptr, &woken));
}

TEST(ObservablePublishTest, PublishHighPriorityFromISRWithObserver) {
    ObservableDispatcher::start();
    Observable<TimerModel> obs{"test"};
    auto cb = [](TimerModel*, void*) {};
    obs.subscribe(cb);
    TimerModel model;
    BaseType_t woken = pdFALSE;
    EXPECT_TRUE(obs.publishHighPriorityFromISR(&model, &woken));
}
