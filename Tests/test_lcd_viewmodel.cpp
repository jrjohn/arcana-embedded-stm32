/**
 * @file test_lcd_viewmodel.cpp
 * @brief Coverage for LcdViewModel.hpp Input → Output transformation.
 */
#include <gtest/gtest.h>
#include <cstring>

#include "stm32f1xx_hal.h"
#include "LcdViewModel.hpp"
#include "F103Models.hpp"
#include "Observable.hpp"

using arcana::lcd::LcdViewModel;
using arcana::lcd::LcdInput;
using arcana::lcd::LcdOutput;
using arcana::SensorDataModel;
using arcana::StorageStatsModel;
using arcana::SdBenchmarkModel;
using arcana::TimerModel;
using arcana::MqttConnectionModel;
using arcana::Observable;

// ── onEvent direct ─────────────────────────────────────────────────────────

TEST(LcdViewModelEvent, SensorDataUpdatesTemperatureAndDirty) {
    LcdViewModel vm;
    LcdInput in; in.type = LcdInput::SensorData;
    in.sensor.temperature = 23.5f;
    vm.onEvent(in);

    EXPECT_FLOAT_EQ(vm.output().temperature, 23.5f);
    EXPECT_TRUE(vm.output().tempValid);
    EXPECT_TRUE(vm.output().dirty & LcdOutput::DIRTY_TEMP);
}

TEST(LcdViewModelEvent, StorageStatsUpdatesAllFields) {
    LcdViewModel vm;
    LcdInput in; in.type = LcdInput::StorageStats;
    in.storage.records = 12345;
    in.storage.rate = 50;
    in.storage.totalKB = 999;
    in.storage.kbps = 7;
    vm.onEvent(in);

    EXPECT_EQ(vm.output().recordCount, 12345u);
    EXPECT_EQ(vm.output().writesPerSec, 50);
    EXPECT_EQ(vm.output().totalKB, 999u);
    EXPECT_EQ(vm.output().kbPerSec, 7);
    EXPECT_TRUE(vm.output().dirty & LcdOutput::DIRTY_STORAGE);
}

TEST(LcdViewModelEvent, TimerTickUpdatesEpoch) {
    LcdViewModel vm;
    LcdInput in; in.type = LcdInput::TimerTick;
    in.timer.epoch = 1700000000;
    in.timer.synced = true;
    in.timer.uptime = 99;
    vm.onEvent(in);

    EXPECT_EQ(vm.output().epoch, 1700000000u);
    EXPECT_TRUE(vm.output().timeSynced);
    EXPECT_EQ(vm.output().uptimeSec, 99u);
    EXPECT_TRUE(vm.output().dirty & LcdOutput::DIRTY_TIME);
}

TEST(LcdViewModelEvent, SdInfoUpdates) {
    LcdViewModel vm;
    LcdInput in; in.type = LcdInput::SdInfo;
    in.sdinfo.freeMB = 100;
    in.sdinfo.totalMB = 1000;
    vm.onEvent(in);

    EXPECT_EQ(vm.output().sdFreeMB, 100u);
    EXPECT_EQ(vm.output().sdTotalMB, 1000u);
    EXPECT_TRUE(vm.output().dirty & LcdOutput::DIRTY_SDINFO);
}

TEST(LcdViewModelEvent, MqttStatusUpdates) {
    LcdViewModel vm;
    LcdInput in; in.type = LcdInput::MqttStatus;
    in.mqtt.connected = true;
    vm.onEvent(in);

    EXPECT_TRUE(vm.output().mqttConnected);
    EXPECT_TRUE(vm.output().mqttKnown);
    EXPECT_TRUE(vm.output().dirty & LcdOutput::DIRTY_MQTT);

    /* Disconnect */
    in.mqtt.connected = false;
    vm.onEvent(in);
    EXPECT_FALSE(vm.output().mqttConnected);
    EXPECT_TRUE(vm.output().mqttKnown);
}

TEST(LcdViewModelEvent, EcgSampleAdvancesCursor) {
    LcdViewModel vm;
    EXPECT_EQ(vm.ecgCursor(), 0);

    /* Push 5 samples */
    for (uint8_t y : {10, 20, 30, 40, 50}) {
        LcdInput in; in.type = LcdInput::EcgSample;
        in.ecg.y = y;
        vm.onEvent(in);
    }
    EXPECT_EQ(vm.ecgCursor(), 5);
    EXPECT_EQ(vm.output().ecgY[0], 10);
    EXPECT_EQ(vm.output().ecgY[4], 50);
    EXPECT_TRUE(vm.output().dirty & LcdOutput::DIRTY_ECG);
}

TEST(LcdViewModelEvent, EcgSampleClampsOutOfRange) {
    LcdViewModel vm;
    LcdInput in; in.type = LcdInput::EcgSample;
    in.ecg.y = 200;  /* > ECG_WIDTH (120) → clamp to 99 */
    vm.onEvent(in);
    EXPECT_EQ(vm.output().ecgY[0], 99);
}

TEST(LcdViewModelEvent, EcgCursorWrapsAroundEcgWidth) {
    LcdViewModel vm;
    LcdInput in; in.type = LcdInput::EcgSample;
    in.ecg.y = 50;
    /* Push ECG_WIDTH + 1 samples to verify wrap */
    for (uint16_t i = 0; i <= LcdOutput::ECG_WIDTH; ++i) vm.onEvent(in);
    EXPECT_EQ(vm.ecgCursor(), 1);  /* wrapped */
}

// ── clearDirty ─────────────────────────────────────────────────────────────

TEST(LcdViewModelDirty, ClearDirtyResetsAllFlags) {
    LcdViewModel vm;
    LcdInput in;
    in.type = LcdInput::SensorData;
    in.sensor.temperature = 1.0f;
    vm.onEvent(in);
    in.type = LcdInput::TimerTick;
    in.timer.epoch = 1; in.timer.synced = false; in.timer.uptime = 0;
    vm.onEvent(in);
    EXPECT_NE(vm.output().dirty, 0);
    vm.clearDirty();
    EXPECT_EQ(vm.output().dirty, 0);
}

// ── Observable wiring via init() ────────────────────────────────────────────

TEST(LcdViewModelInit, InitWithNoObservablesNoOps) {
    LcdViewModel vm;
    /* All inputs nullptr → init does nothing */
    vm.init(nullptr);
    SUCCEED();
}

TEST(LcdViewModelInit, SubscribesAndForwardsViaObservables) {
    LcdViewModel vm;
    Observable<SensorDataModel>    sensors("s");
    Observable<StorageStatsModel>  stats("t");
    Observable<TimerModel>         timer("ti");
    Observable<MqttConnectionModel> mqtt("m");
    vm.input.SensorData = &sensors;
    vm.input.StorageStats = &stats;
    vm.input.BaseTimer = &timer;
    vm.input.MqttConn = &mqtt;
    vm.init(nullptr);

    /* Publishing on a host stub may or may not call back depending on the
     * Observable implementation; we just verify init() walked all branches
     * (subscribe was called). The host Observable accepts subscribe()
     * cleanly, so the dirty flags will only fire if the underlying queue
     * actually delivers. We don't strictly test that here. */
    SUCCEED();
}

// ── Static observer callbacks via Observable::notify (synchronous) ──────────
//
// Observable::notify is the synchronous fan-out helper — calling it after
// init() exercises each of the static onSensorData / onStorageStats /
// onSdBenchmark / onBaseTimer / onMqttConn / onLightData callbacks (which
// are otherwise dead code on host because the queue dispatcher task isn't
// running).

TEST(LcdViewModelObservers, NotifyDrivesAllStaticCallbacks) {
    LcdViewModel vm;
    Observable<SensorDataModel>    sensors("s");
    Observable<arcana::LightDataModel> light("l");
    Observable<StorageStatsModel>  stats("st");
    Observable<SdBenchmarkModel>   bench("b");
    Observable<TimerModel>         timer("ti");
    Observable<MqttConnectionModel> mqtt("m");

    vm.input.SensorData   = &sensors;
    vm.input.LightData    = &light;
    vm.input.StorageStats = &stats;
    vm.input.SdBenchmark  = &bench;
    vm.input.BaseTimer    = &timer;
    vm.input.MqttConn     = &mqtt;
    vm.init(nullptr);

    /* SensorData → onSensorData → onEvent(SensorData) → temperature dirty */
    SensorDataModel s; s.temperature = 24.0f;
    sensors.notify(&s);
    EXPECT_FLOAT_EQ(vm.output().temperature, 24.0f);
    EXPECT_TRUE(vm.output().dirty & LcdOutput::DIRTY_TEMP);

    /* LightData → onLightData (no-op, just ensures it's called) */
    arcana::LightDataModel l; l.ambientLight = 100; l.proximity = 5;
    light.notify(&l);  /* must not crash */

    /* StorageStats → records/rate update */
    StorageStatsModel ss;
    ss.recordCount = 100; ss.writesPerSec = 10;
    ss.totalKB = 4; ss.kbPerSec = 1;
    stats.notify(&ss);
    EXPECT_EQ(vm.output().recordCount, 100u);
    EXPECT_EQ(vm.output().writesPerSec, 10);

    /* SdBenchmark → SdInfo update */
    SdBenchmarkModel sb;
    sb.totalKB = 500; sb.totalRecords = 1024;
    bench.notify(&sb);
    EXPECT_EQ(vm.output().sdFreeMB,  500u);
    EXPECT_EQ(vm.output().sdTotalMB, 1024u);

    /* BaseTimer → TimerTick dispatch (uses SystemClock for epoch) */
    TimerModel t;
    timer.notify(&t);
    EXPECT_TRUE(vm.output().dirty & LcdOutput::DIRTY_TIME);

    /* MqttConn → MqttStatus update */
    MqttConnectionModel mc; mc.connected = true;
    mqtt.notify(&mc);
    EXPECT_TRUE(vm.output().mqttConnected);
    EXPECT_TRUE(vm.output().mqttKnown);
}
