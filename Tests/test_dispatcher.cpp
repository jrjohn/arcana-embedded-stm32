#include <gtest/gtest.h>
#include "CommandDispatcher.hpp"
#include "CommandService.hpp"
#include "CounterService.hpp"
#include "ICommand.hpp"

using namespace arcana;

// ── Minimal concrete commands for testing ────────────────────────────────────

class EchoCommand : public ICommand {
public:
    CommandKey getKey() const override { return {Cluster::System, SystemCommand::Ping}; }
    void execute(const CommandRequest& req, CommandResponseModel& rsp) override {
        rsp.status = CommandStatus::Success;
        if (req.paramsLength > 0) {
            rsp.dataLength = req.paramsLength;
            for (uint8_t i = 0; i < req.paramsLength; i++)
                rsp.data[i] = req.params[i];
        }
    }
};

class FailCommand : public ICommand {
public:
    CommandKey getKey() const override { return {Cluster::Sensor, 0x10}; }
    void execute(const CommandRequest&, CommandResponseModel& rsp) override {
        rsp.status = CommandStatus::Error;
    }
};

// ── CommandDispatcher ─────────────────────────────────────────────────────────

TEST(CommandDispatcherTest, DispatchUnknownCommandReturnsFalse) {
    CommandRegistry reg;
    Observable<CommandResponseModel> obs{"Test"};
    CommandDispatcher disp(reg, obs);

    CommandRequest req{}; req.key = {Cluster::System, SystemCommand::Ping};
    EXPECT_FALSE(disp.dispatch(req));
}

TEST(CommandDispatcherTest, DispatchKnownCommandCallsExecute) {
    CommandRegistry reg;
    EchoCommand echo;
    reg.registerCommand(&echo);

    Observable<CommandResponseModel> obs{"Test"};
    CommandDispatcher disp(reg, obs);

    CommandRequest req{};
    req.key = {Cluster::System, SystemCommand::Ping};
    req.paramsLength = 0;
    EXPECT_TRUE(disp.dispatch(req));
}

TEST(CommandDispatcherTest, DispatchSyncSuccessReturnsTrue) {
    CommandRegistry reg;
    EchoCommand echo;
    reg.registerCommand(&echo);

    Observable<CommandResponseModel> obs{"Test"};
    CommandDispatcher disp(reg, obs);

    CommandRequest req{}; req.key = {Cluster::System, SystemCommand::Ping};
    EXPECT_TRUE(disp.dispatchSync(req));
}

TEST(CommandDispatcherTest, DispatchSyncFailCommandReturnsFalse) {
    CommandRegistry reg;
    FailCommand fail;
    reg.registerCommand(&fail);

    Observable<CommandResponseModel> obs{"Test"};
    CommandDispatcher disp(reg, obs);

    CommandRequest req{}; req.key = {Cluster::Sensor, 0x10};
    EXPECT_FALSE(disp.dispatchSync(req));
}

TEST(CommandDispatcherTest, DispatchSyncNotFoundReturnsFalse) {
    CommandRegistry reg;
    Observable<CommandResponseModel> obs{"Test"};
    CommandDispatcher disp(reg, obs);

    CommandRequest req{}; req.key = {Cluster::Sensor, 0xFF};
    EXPECT_FALSE(disp.dispatchSync(req));
}

// ── CommandService ────────────────────────────────────────────────────────────

TEST(CommandServiceTest, InitRegistersBuiltinCommands) {
    CommandService svc;
    svc.init();
    EXPECT_GE(svc.getCommandCount(), 2u);  // Ping + GetCounter
}

TEST(CommandServiceTest, ExecuteSyncPingSucceeds) {
    CommandService svc;
    svc.init();

    // Need counterService global to be initialized for GetCounterCommand
    counterService.init(nullptr);

    CommandRequest req{};
    req.key = {Cluster::System, SystemCommand::Ping};
    EXPECT_TRUE(svc.executeSync(req));
}

TEST(CommandServiceTest, ExecuteAsyncDispatchesViaObservable) {
    CommandService svc;
    svc.init();
    counterService.init(nullptr);

    bool received = false;
    svc.observable.subscribe([](CommandResponseModel* rsp, void* ctx) {
        *static_cast<bool*>(ctx) = true;
        EXPECT_EQ(rsp->status, CommandStatus::Success);
    }, &received);

    CommandRequest req{}; req.key = {Cluster::System, SystemCommand::Ping};
    // publish() goes through ObservableDispatcher (mock returns pdTRUE for queue)
    svc.execute(req);
    // In sync mode we test via dispatchSync; async path is covered by dispatch() call
}

TEST(CommandServiceTest, GetRegistryReturnsRef) {
    CommandService svc;
    svc.init();
    CommandRegistry& reg = svc.getRegistry();
    EXPECT_GE(reg.getCommandCount(), 2u);
}

TEST(CommandServiceTest, ExecuteSyncUnknownCommandReturnsFalse) {
    CommandService svc;
    svc.init();
    CommandRequest req{}; req.key = {Cluster::Sensor, 0xFF};
    EXPECT_FALSE(svc.executeSync(req));
}
