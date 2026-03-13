#include <gtest/gtest.h>
#include "CommandRegistry.hpp"
#include "ICommand.hpp"

using namespace arcana;

// Concrete test command
class PingCmd : public ICommand {
public:
    CommandKey getKey() const override { return {Cluster::System, SystemCommand::Ping}; }
    void execute(const CommandRequest&, CommandResponseModel& rsp) override {
        rsp.status = CommandStatus::Success;
    }
};

class GetCounterCmd : public ICommand {
public:
    CommandKey getKey() const override { return {Cluster::Sensor, SensorCommand::GetCounter}; }
    void execute(const CommandRequest&, CommandResponseModel& rsp) override {
        rsp.setUint32(42);
    }
};

// ── CommandRegistry ───────────────────────────────────────────────────────────

TEST(CommandRegistryTest, EmptyRegistryFindsNothing) {
    CommandRegistry reg;
    EXPECT_EQ(reg.findCommand({Cluster::System, SystemCommand::Ping}), nullptr);
    EXPECT_EQ(reg.getCommandCount(), 0);
}

TEST(CommandRegistryTest, RegisterAndFindCommand) {
    CommandRegistry reg;
    PingCmd ping;
    ASSERT_TRUE(reg.registerCommand(&ping));
    EXPECT_EQ(reg.getCommandCount(), 1);

    ICommand* found = reg.findCommand({Cluster::System, SystemCommand::Ping});
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found, &ping);
}

TEST(CommandRegistryTest, FindReturnsNullForUnknownCommand) {
    CommandRegistry reg;
    PingCmd ping;
    ASSERT_TRUE(reg.registerCommand(&ping));
    EXPECT_EQ(reg.findCommand({Cluster::Sensor, 0xFF}), nullptr);
}

TEST(CommandRegistryTest, RegisterMultipleCommands) {
    CommandRegistry reg;
    PingCmd ping;
    GetCounterCmd getCounter;
    ASSERT_TRUE(reg.registerCommand(&ping));
    ASSERT_TRUE(reg.registerCommand(&getCounter));
    EXPECT_EQ(reg.getCommandCount(), 2);

    EXPECT_NE(reg.findCommand({Cluster::System, SystemCommand::Ping}), nullptr);
    EXPECT_NE(reg.findCommand({Cluster::Sensor, SensorCommand::GetCounter}), nullptr);
}

TEST(CommandRegistryTest, DuplicateKeyRejected) {
    CommandRegistry reg;
    PingCmd ping1, ping2;
    ASSERT_TRUE(reg.registerCommand(&ping1));
    EXPECT_FALSE(reg.registerCommand(&ping2));  // Same key
    EXPECT_EQ(reg.getCommandCount(), 1);
}

TEST(CommandRegistryTest, NullCommandRejected) {
    CommandRegistry reg;
    EXPECT_FALSE(reg.registerCommand(nullptr));
    EXPECT_EQ(reg.getCommandCount(), 0);
}

TEST(CommandRegistryTest, RegistryFillsToCapacity) {
    CommandRegistry reg;
    // We can only add 8 commands; use anonymous lambdas via concrete classes
    struct TestCmd : public ICommand {
        uint8_t id;
        TestCmd(uint8_t i) : id(i) {}
        CommandKey getKey() const override { return {Cluster::System, id}; }
        void execute(const CommandRequest&, CommandResponseModel&) override {}
    };

    TestCmd cmds[] = {0,1,2,3,4,5,6,7};
    for (auto& c : cmds) ASSERT_TRUE(reg.registerCommand(&c));
    EXPECT_EQ(reg.getCommandCount(), CommandRegistry::MAX_COMMANDS);

    // One more should fail
    TestCmd extra(99);
    EXPECT_FALSE(reg.registerCommand(&extra));
}

TEST(CommandRegistryTest, ExecuteCommandViaFound) {
    CommandRegistry reg;
    GetCounterCmd getCounter;
    ASSERT_TRUE(reg.registerCommand(&getCounter));

    ICommand* cmd = reg.findCommand({Cluster::Sensor, SensorCommand::GetCounter});
    ASSERT_NE(cmd, nullptr);

    CommandRequest req{};
    req.key = {Cluster::Sensor, SensorCommand::GetCounter};
    CommandResponseModel rsp{};
    cmd->execute(req, rsp);

    // GetCounterCmd sets uint32 = 42
    EXPECT_EQ(rsp.dataLength, 4);
    uint32_t val = rsp.data[0] | (rsp.data[1] << 8) | (rsp.data[2] << 16) | (rsp.data[3] << 24);
    EXPECT_EQ(val, 42u);
}
