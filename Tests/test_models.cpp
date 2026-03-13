#include <gtest/gtest.h>
#include "Models.hpp"

using namespace arcana;

TEST(ModelsTest, TimerModelDefaultValues) {
    TimerModel m;
    EXPECT_EQ(m.tickCount, 0u);
    EXPECT_EQ(m.periodMs,  0u);
}

TEST(ModelsTest, TimerModelUpdate) {
    TimerModel m;
    m.update(100);
    EXPECT_EQ(m.tickCount, 1u);
    EXPECT_EQ(m.periodMs,  100u);
    m.update(100);
    EXPECT_EQ(m.tickCount, 2u);
}

TEST(ModelsTest, CounterModelDefaultValues) {
    CounterModel m;
    EXPECT_EQ(m.count, 0u);
}

TEST(ModelsTest, CounterModelIncrement) {
    CounterModel m;
    m.increment();
    EXPECT_EQ(m.count, 1u);
    m.increment();
    EXPECT_EQ(m.count, 2u);
}

TEST(ModelsTest, ModelTypeField) {
    TimerModel   t;
    CounterModel c;
    EXPECT_EQ(t.type, static_cast<uint8_t>(ModelType::Timer));
    EXPECT_EQ(c.type, static_cast<uint8_t>(ModelType::Counter));
}

TEST(ModelsTest, ModelUpdateTimestamp) {
    TimerModel m;
    m.updateTimestamp();   // calls xTaskGetTickCount() → 0 in mock
    EXPECT_EQ(m.timestamp, 0u);
}
