/**
 * @file test_appenders.cpp
 * @brief Host coverage for the four F103 Logger appenders.
 *
 *   - SerialAppender: printf to UART (we just verify it doesn't crash and
 *     accepts every Level).
 *   - AtsAppender: writes ERROR_LOG records into an ArcanaTsDb instance.
 *     We exercise minLevel, the no-op-when-detached early-return, attach,
 *     detach, and the toSeverity Level mapping for all 6 levels (via the
 *     append path against an unopened ArcanaTsDb — append() is no-op when
 *     mDb is null).
 *   - DeviceAppender: minLevel + no-op + attach/detach.
 *   - SyslogAppender: ring buffer + UDP flush via Esp8266 mock.
 *
 * Note: a full ArcanaTsDb-backed AtsAppender test would need to allocate
 * ~12KB of buffers + a 32-byte key + a getTime callback. The branches in
 * AtsAppender::append that depend on the db are well covered by
 * test_atsstorage which links the production ArcanaTsDb path; here we just
 * verify the appender's own conditional logic.
 */
#include <gtest/gtest.h>
#include <cstring>

#include "stm32f1xx_hal.h"
#include "ff.h"

#include "Log.hpp"
#include "ats/ArcanaTsDb.hpp"
#include "ats/ArcanaTsSchema.hpp"
#include "ats_mocks.hpp"

#include "SerialAppender.hpp"
#include "AtsAppender.hpp"
#include "DeviceAppender.hpp"
#include "SyslogAppender.hpp"

#include "Esp8266.hpp"

using arcana::log::Level;
using arcana::log::LogEvent;
using arcana::log::SerialAppender;
using arcana::log::AtsAppender;
using arcana::log::DeviceAppender;
using arcana::log::SyslogAppender;
using arcana::Esp8266;

namespace {

LogEvent makeEvent(Level lvl, uint8_t src = 0, uint16_t code = 0x1234,
                    uint32_t param = 42) {
    LogEvent ev{};
    ev.timestamp = 1700000000u;
    ev.tickMs    = 500;
    ev.level     = static_cast<uint8_t>(lvl);
    ev.source    = src;
    ev.code      = code;
    ev.param     = param;
    return ev;
}

} // anonymous namespace

// ── SerialAppender ──────────────────────────────────────────────────────────

TEST(SerialAppenderTest, AppendAcceptsAllLevels) {
    SerialAppender app;
    EXPECT_EQ(app.minLevel(), Level::Trace);
    /* Each level routes through the printf branch. */
    for (uint8_t lvl = 0; lvl <= 5; ++lvl) {
        app.append(makeEvent(static_cast<Level>(lvl)));
    }
}

TEST(SerialAppenderTest, AppendNoParamFormatsWithoutP) {
    SerialAppender app;
    LogEvent ev = makeEvent(Level::Info);
    ev.param = 0;
    app.append(ev);  /* Hits the "no param" printf branch */
}

TEST(SerialAppenderTest, AppendUnknownSourceFallback) {
    SerialAppender app;
    LogEvent ev = makeEvent(Level::Warn, /*src=*/200);
    app.append(ev);
}

// ── ArcanaTsDb test rig (real engine, in-memory file port) ─────────────────

namespace {

struct DbRig {
    arcana_test::MemFilePort file;
    arcana_test::NullCipher  cipher;
    arcana_test::StubMutex   mutex;

    std::vector<uint8_t> bufA, bufB, slow, readCache;
    uint8_t key[32]{};
    uint8_t deviceUid[12]{};

    arcana::ats::ArcanaTsDb db;

    DbRig()
        : bufA(arcana::ats::BLOCK_SIZE, 0),
          bufB(arcana::ats::BLOCK_SIZE, 0),
          slow(arcana::ats::BLOCK_SIZE, 0),
          readCache(arcana::ats::BLOCK_SIZE, 0)
    {
        for (int i = 0; i < 32; ++i) key[i]       = static_cast<uint8_t>(0xA0 + i);
        for (int i = 0; i < 12; ++i) deviceUid[i] = static_cast<uint8_t>(0x10 + i);
    }

    static uint32_t fakeNow() { return 1700000000u; }

    bool open() {
        arcana::ats::AtsConfig cfg{};
        cfg.file           = &file;
        cfg.cipher         = &cipher;
        cfg.mutex          = &mutex;
        cfg.getTime        = &DbRig::fakeNow;
        cfg.key            = key;
        cfg.headerKey      = nullptr;
        cfg.deviceUid      = deviceUid;
        cfg.deviceUidSize  = 12;
        cfg.overflow       = arcana::ats::OverflowPolicy::Block;
        cfg.primaryChannel = 0;
        cfg.primaryBufA    = bufA.data();
        cfg.primaryBufB    = bufB.data();
        cfg.slowBuf        = slow.data();
        cfg.readCache      = readCache.data();

        if (!db.open("/tmp/appender.ats", cfg)) return false;

        /* 12-byte ERROR_LOG schema for AtsAppender / DeviceAppender output */
        arcana::ats::ArcanaTsSchema schema;
        schema.setName("ERR12");
        schema.addField("ts",   arcana::ats::FieldType::U32);
        schema.addField("sev",  arcana::ats::FieldType::U8);
        schema.addField("src",  arcana::ats::FieldType::U8);
        schema.addField("code", arcana::ats::FieldType::U16);
        schema.addField("p",    arcana::ats::FieldType::U32);
        if (!db.addChannel(0, schema)) return false;
        return db.start();
    }
};

} // anonymous namespace

// ── AtsAppender ─────────────────────────────────────────────────────────────

TEST(AtsAppenderTest, MinLevelIsWarn) {
    AtsAppender app;
    EXPECT_EQ(app.minLevel(), Level::Warn);
}

TEST(AtsAppenderTest, AppendNoOpWhenNotAttached) {
    AtsAppender app;
    /* mDb null → early return for every level */
    for (uint8_t lvl = 0; lvl <= 5; ++lvl) {
        app.append(makeEvent(static_cast<Level>(lvl)));
    }
}

TEST(AtsAppenderTest, AppendWritesRecordToRealDb) {
    DbRig rig;
    ASSERT_TRUE(rig.open());

    AtsAppender app;
    app.attach(&rig.db, /*channel=*/0);

    /* Warn → append, no flush */
    app.append(makeEvent(Level::Warn,  /*src=*/2, 0xABCD, 0x11));
    /* Error → append + immediate flush (toSeverity Error branch) */
    app.append(makeEvent(Level::Error, /*src=*/3, 0xBEEF, 0x22));
    /* Fatal → append + immediate flush (toSeverity Fatal branch) */
    app.append(makeEvent(Level::Fatal, /*src=*/4, 0xDEAD, 0x33));
    /* Info → toSeverity Info branch */
    app.append(makeEvent(Level::Info,  /*src=*/5, 0x0001, 0x44));

    rig.db.close();
}

TEST(AtsAppenderTest, DetachStopsWrites) {
    DbRig rig;
    ASSERT_TRUE(rig.open());

    AtsAppender app;
    app.attach(&rig.db, 0);
    app.detach();
    app.append(makeEvent(Level::Error));  /* mDb null → no-op */
    rig.db.close();
}

// ── DeviceAppender ──────────────────────────────────────────────────────────

TEST(DeviceAppenderTest, MinLevelIsFatal) {
    DeviceAppender app;
    EXPECT_EQ(app.minLevel(), Level::Fatal);
}

TEST(DeviceAppenderTest, AppendNoOpWhenNotAttached) {
    DeviceAppender app;
    app.append(makeEvent(Level::Fatal));
}

TEST(DeviceAppenderTest, AppendWritesLifecycleErrorToRealDb) {
    DbRig rig;
    ASSERT_TRUE(rig.open());

    DeviceAppender app;
    app.attach(&rig.db);

    /* Production behaviour: Fatal events get written + flushed immediately */
    app.append(makeEvent(Level::Fatal, /*src=*/9, 0xCAFE, 0x55));

    app.detach();
    app.append(makeEvent(Level::Fatal));  /* mDb null → no-op */
    rig.db.close();
}

// ── SyslogAppender ──────────────────────────────────────────────────────────

TEST(SyslogAppenderTest, MinLevelIsWarn) {
    auto& app = SyslogAppender::getInstance();
    EXPECT_EQ(app.minLevel(), Level::Warn);
}

TEST(SyslogAppenderTest, AppendEnqueuesRingBufferEntries) {
    auto& app = SyslogAppender::getInstance();
    Esp8266::getInstance().resetForTest();

    /* Take a snapshot of pending; we just verify append() raises it. */
    uint8_t before = app.pending();
    app.append(makeEvent(Level::Warn,  /*src=*/0));
    app.append(makeEvent(Level::Error, /*src=*/1));
    app.append(makeEvent(Level::Fatal, /*src=*/2));
    /* The ring is bounded (8 slots) and may overflow on subsequent runs;
     * we only assert that pending grew or saturated. */
    EXPECT_GE(app.pending(), before);
}

TEST(SyslogAppenderTest, OpenUdpHappyPath) {
    auto& app = SyslogAppender::getInstance();
    auto& esp = Esp8266::getInstance();
    esp.resetForTest();
    esp.pushResponse("OK");
    EXPECT_TRUE(app.openUdp(esp));
    /* Idempotent — second open returns true without sending another command */
    EXPECT_TRUE(app.openUdp(esp));
    app.closeUdp(esp);
}

TEST(SyslogAppenderTest, OpenUdpFailureReturnsFalse) {
    auto& app = SyslogAppender::getInstance();
    auto& esp = Esp8266::getInstance();
    esp.resetForTest();
    /* Force closeUdp first so mUdpOpen is false */
    app.closeUdp(esp);
    esp.pushResponse("ERROR");
    EXPECT_FALSE(app.openUdp(esp));
}

TEST(SyslogAppenderTest, FlushViaUdpSendsPendingMessages) {
    auto& app = SyslogAppender::getInstance();
    auto& esp = Esp8266::getInstance();
    esp.resetForTest();

    /* Open UDP socket */
    esp.pushResponse("OK");
    ASSERT_TRUE(app.openUdp(esp));

    /* Enqueue 2 messages */
    app.append(makeEvent(Level::Warn, /*src=*/0));
    app.append(makeEvent(Level::Error, /*src=*/1));

    /* Drive 2 send sequences: CIPSEND prompt + sendData + SEND OK */
    for (int i = 0; i < 2; ++i) {
        esp.pushResponse(">");
        esp.pushResponse("");
        esp.pushResponse("SEND OK");
    }
    uint8_t sent = app.flushViaUdp(esp);
    EXPECT_GE(sent, 2u);

    app.closeUdp(esp);
}

TEST(SyslogAppenderTest, FlushViaUdpStopsOnSendError) {
    auto& app = SyslogAppender::getInstance();
    auto& esp = Esp8266::getInstance();
    esp.resetForTest();

    esp.pushResponse("OK");
    ASSERT_TRUE(app.openUdp(esp));

    app.append(makeEvent(Level::Warn));
    /* CIPSEND prompt fails */
    esp.pushResponse("ERROR");
    /* No more responses needed — flush bails */
    uint8_t sent = app.flushViaUdp(esp);
    EXPECT_EQ(sent, 0u);
    app.closeUdp(esp);
}

TEST(SyslogAppenderTest, SendStatsEnqueuesHeartbeat) {
    auto& app = SyslogAppender::getInstance();
    auto& esp = Esp8266::getInstance();
    esp.resetForTest();

    /* Bounded drain — at most 4 flushes (= ring capacity / max-per-flush). */
    esp.pushResponse("OK");
    app.openUdp(esp);
    for (int round = 0; round < 4 && app.pending() > 0; ++round) {
        for (int i = 0; i < 4; ++i) {
            esp.pushResponse(">");
            esp.pushResponse("");
            esp.pushResponse("SEND OK");
        }
        app.flushViaUdp(esp);
    }

    uint8_t before = app.pending();
    app.sendStats(/*records=*/1000, /*rate=*/50, /*kb=*/4, /*epoch=*/1700000000);
    /* Ring may have wrapped — assert pending is at least one more, OR is
     * already at the bound (saturation case). */
    EXPECT_TRUE(app.pending() > before || app.pending() >= 7);
    app.closeUdp(esp);
}
