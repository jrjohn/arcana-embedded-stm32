#pragma once

#include "BleService.hpp"
#include "Hc08Ble.hpp"
#include "CommandTypes.hpp"
#include "ICommand.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace ble {

/**
 * BLE Service implementation — HC-08 transport + command execution.
 *
 * Task monitors BLE for incoming framed commands (FrameCodec wire format).
 * Deframes → decodes → executes → encodes response → sends back via BLE.
 * Same wire protocol as MQTT — phone/cloud use identical frame format.
 */
class BleServiceImpl : public BleService {
public:
    static BleService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

    /** Register a command handler (max 8) */
    bool registerCommand(ICommand* cmd);

private:
    BleServiceImpl();
    ~BleServiceImpl();
    BleServiceImpl(const BleServiceImpl&);
    BleServiceImpl& operator=(const BleServiceImpl&);

    static void bleTask(void* param);
    void taskLoop();
    void processFrame(const uint8_t* data, uint16_t len);

    // Command registry (shared with MQTT if needed)
    static const uint8_t MAX_COMMANDS = 8;
    ICommand* mCommands[MAX_COMMANDS];
    uint8_t mCommandCount;

    ICommand* findCommand(CommandKey key);

    // Task
    static const uint16_t TASK_STACK_SIZE = 256;
    StaticTask_t mTaskBuf;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
    bool mRunning;

    // Reference to BLE driver
    Hc08Ble& mBle;
};

} // namespace ble
} // namespace arcana
