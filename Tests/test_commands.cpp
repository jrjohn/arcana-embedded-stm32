#include <gtest/gtest.h>
#include "PingCommand.hpp"
#include "GetCounterCommand.hpp"
#include "CounterService.hpp"

using namespace arcana;

// ── PingCommand ───────────────────────────────────────────────────────────────

TEST(PingCommandTest, KeyIsPing) {
    PingCommand cmd;
    EXPECT_EQ(cmd.getKey().cluster,   Cluster::System);
    EXPECT_EQ(cmd.getKey().commandId, SystemCommand::Ping);
}

TEST(PingCommandTest, ExecuteReturnsTickCountAndSuccess) {
    PingCommand cmd;
    CommandRequest req{};
    CommandResponseModel rsp{};
    cmd.execute(req, rsp);

    EXPECT_EQ(rsp.status, CommandStatus::Success);
    EXPECT_EQ(rsp.dataLength, 4u);  // uint32_t
    /* xTaskGetTickCount() is monotonic in mock — value > 0 */
    uint32_t ticks = rsp.data[0] | (rsp.data[1] << 8) | (rsp.data[2] << 16) | (rsp.data[3] << 24);
    EXPECT_GT(ticks, 0u);
}

// ── GetCounterCommand ─────────────────────────────────────────────────────────

TEST(GetCounterCommandTest, KeyIsGetCounter) {
    GetCounterCommand cmd;
    EXPECT_EQ(cmd.getKey().cluster,   Cluster::Sensor);
    EXPECT_EQ(cmd.getKey().commandId, SensorCommand::GetCounter);
}

TEST(GetCounterCommandTest, ExecuteReturnsCounterValueAndSuccess) {
    counterService.reset();  // ensure known state

    GetCounterCommand cmd;
    CommandRequest req{};
    CommandResponseModel rsp{};
    cmd.execute(req, rsp);

    EXPECT_EQ(rsp.status, CommandStatus::Success);
    EXPECT_EQ(rsp.dataLength, 4u);
    uint32_t val = rsp.data[0] | (rsp.data[1] << 8) | (rsp.data[2] << 16) | (rsp.data[3] << 24);
    EXPECT_EQ(val, counterService.getCount());
}

TEST(GetCounterCommandTest, ReflectsUpdatedCounterValue) {
    Observable<TimerModel> obs{"Test"};
    counterService.init(&obs);
    counterService.reset();

    TimerModel m; m.update(100);
    obs.notify(&m);
    obs.notify(&m);  // count = 2

    GetCounterCommand cmd;
    CommandRequest req{};
    CommandResponseModel rsp{};
    cmd.execute(req, rsp);

    uint32_t val = rsp.data[0] | (rsp.data[1] << 8) | (rsp.data[2] << 16) | (rsp.data[3] << 24);
    EXPECT_EQ(val, 2u);
}
