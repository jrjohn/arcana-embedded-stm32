/**
 * @file test_arcanats_db.cpp
 * @brief Known-Answer + structural tests for the ArcanaTS v2 core engine
 *
 * Targets Shared/Src/ats/ArcanaTsDb.cpp + ArcanaTsSchema.hpp.
 * Uses the in-memory MemFilePort + reversible XorCipher PAL mocks so the
 * full encrypt → write → recover → decrypt → query path is exercised.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

#include "ats_mocks.hpp"
#include "ats/ArcanaTsDb.hpp"
#include "ats/ArcanaTsSchema.hpp"
#include "ats/ArcanaTsTypes.hpp"

using arcana::ats::ArcanaTsDb;
using arcana::ats::ArcanaTsSchema;
using arcana::ats::AtsConfig;
using arcana::ats::OverflowPolicy;
using arcana::ats::FieldType;
using arcana::ats::BLOCK_SIZE;
using arcana::ats::BLOCK_PAYLOAD_SIZE;
using arcana::ats::MAX_CHANNELS;
using arcana::ats::MULTI_CHANNEL_ID;

using arcana_test::MemFilePort;
using arcana_test::XorCipher;
using arcana_test::NullCipher;
using arcana_test::StubMutex;
using arcana_test::TestClock;

namespace {

// ── Test fixture ─────────────────────────────────────────────────────────────

struct DbCtx {
    MemFilePort                 file;
    XorCipher                   cipher;
    StubMutex                   mutex;
    std::vector<uint8_t>        bufA;
    std::vector<uint8_t>        bufB;
    std::vector<uint8_t>        slow;
    std::vector<uint8_t>        readCache;
    uint8_t                     deviceUid[12]{};
    uint8_t                     key[32]{};
    uint8_t                     headerKey[32]{};

    DbCtx()
        : bufA(BLOCK_SIZE, 0),
          bufB(BLOCK_SIZE, 0),
          slow(BLOCK_SIZE, 0),
          readCache(BLOCK_SIZE, 0)
    {
        for (int i = 0; i < 12; ++i) deviceUid[i] = static_cast<uint8_t>(0x10 + i);
        for (int i = 0; i < 32; ++i) key[i]       = static_cast<uint8_t>(0xA0 + i);
        for (int i = 0; i < 32; ++i) headerKey[i] = static_cast<uint8_t>(0x40 + i);
    }

    AtsConfig makeCfg(uint8_t primaryCh = 0,
                      OverflowPolicy ov = OverflowPolicy::Block,
                      const uint8_t* hdrKey = nullptr) {
        AtsConfig c{};
        c.file            = &file;
        c.cipher          = &cipher;
        c.mutex           = &mutex;
        c.getTime         = &TestClock::now;
        c.key             = key;
        c.headerKey       = hdrKey;
        c.deviceUid       = deviceUid;
        c.deviceUidSize   = 12;
        c.overflow        = ov;
        c.primaryChannel  = primaryCh;
        c.primaryBufA     = bufA.data();
        c.primaryBufB     = bufB.data();
        c.slowBuf         = slow.data();
        c.readCache       = readCache.data();
        return c;
    }
};

// Schema: 8-byte record [ts:U32][value:U32]
ArcanaTsSchema makeAdcSchema() {
    ArcanaTsSchema s;
    s.setName("ADC8");
    s.addField("ts",  FieldType::U32);
    s.addField("val", FieldType::U32);
    return s;
}

// Build a record from (ts, val)
void mkRec(uint8_t out[8], uint32_t ts, uint32_t val) {
    std::memcpy(out,     &ts,  4);
    std::memcpy(out + 4, &val, 4);
}

// Callback that collects (ts, val) into a vector
struct CollectCtx {
    std::vector<std::pair<uint32_t, uint32_t>> rows;
    uint8_t expectChannel = 0xFF;
    uint16_t stopAfter = 0;  // 0 = never
};

bool collectCb(uint8_t channelId, const uint8_t* rec, uint32_t ts, void* vctx) {
    auto* ctx = static_cast<CollectCtx*>(vctx);
    if (ctx->expectChannel != 0xFF && ctx->expectChannel != channelId) return false;
    uint32_t val;
    std::memcpy(&val, rec + 4, 4);
    ctx->rows.emplace_back(ts, val);
    if (ctx->stopAfter && ctx->rows.size() >= ctx->stopAfter) return true;
    return false;
}

} // namespace

// ── Lifecycle ────────────────────────────────────────────────────────────────

TEST(ArcanaTsDbTest, OpenRequiresFileMutexAndTime) {
    DbCtx d;
    ArcanaTsDb db;

    AtsConfig bad = d.makeCfg();
    bad.file = nullptr;
    EXPECT_FALSE(db.open("/tmp/x.ats", bad));

    bad = d.makeCfg();
    bad.mutex = nullptr;
    EXPECT_FALSE(db.open("/tmp/x.ats", bad));

    bad = d.makeCfg();
    bad.getTime = nullptr;
    EXPECT_FALSE(db.open("/tmp/x.ats", bad));
}

TEST(ArcanaTsDbTest, CreateAddChannelStartClose) {
    DbCtx d;
    TestClock::reset();
    ArcanaTsDb db;

    AtsConfig cfg = d.makeCfg(/*primary*/0);
    ASSERT_TRUE(db.open("dev.ats", cfg));
    EXPECT_TRUE(db.isOpen());
    EXPECT_FALSE(db.isReadOnly());

    auto schema = makeAdcSchema();
    ASSERT_TRUE(db.addChannel(0, schema, 100));
    EXPECT_EQ(db.getChannelCount(), 1u);

    // duplicate channel rejected
    EXPECT_FALSE(db.addChannel(0, schema, 100));
    // out-of-range channel rejected
    EXPECT_FALSE(db.addChannel(MAX_CHANNELS, schema, 100));
    // empty schema rejected
    ArcanaTsSchema empty;
    EXPECT_FALSE(db.addChannel(1, empty, 0));

    ASSERT_TRUE(db.start());
    // double-start rejected
    EXPECT_FALSE(db.start());

    EXPECT_TRUE(db.close());
    EXPECT_FALSE(db.isOpen());
    EXPECT_TRUE(d.file.data.size() >= BLOCK_SIZE);
}

TEST(ArcanaTsDbTest, AppendBeforeStartFails) {
    DbCtx d;
    TestClock::reset();
    ArcanaTsDb db;
    ASSERT_TRUE(db.open("a.ats", d.makeCfg()));
    ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));

    uint8_t rec[8];
    mkRec(rec, 1, 1);
    EXPECT_FALSE(db.append(0, rec));   // not started yet
    db.close();
}

TEST(ArcanaTsDbTest, AppendInvalidChannelFails) {
    DbCtx d;
    TestClock::reset();
    ArcanaTsDb db;
    ASSERT_TRUE(db.open("a.ats", d.makeCfg()));
    ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));
    ASSERT_TRUE(db.start());

    uint8_t rec[8];
    mkRec(rec, 1, 1);
    EXPECT_FALSE(db.append(7, rec));            // not active
    EXPECT_FALSE(db.append(MAX_CHANNELS, rec)); // out of range
    db.close();
}

// ── Primary channel: append + RAM query ──────────────────────────────────────

TEST(ArcanaTsDbTest, PrimaryChannelAppendQueryRam) {
    DbCtx d;
    TestClock::reset(2000000000u, 1);
    ArcanaTsDb db;
    ASSERT_TRUE(db.open("p.ats", d.makeCfg(/*primary*/0)));
    ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));
    ASSERT_TRUE(db.start());

    // Write 5 records — all stay in primary bufA
    for (uint32_t i = 0; i < 5; ++i) {
        uint8_t rec[8];
        mkRec(rec, 0, i * 10);
        ASSERT_TRUE(db.append(0, rec));
    }
    EXPECT_EQ(db.getStats().totalRecords, 5u);
    EXPECT_EQ(db.getStats().perChannelRecords[0], 5u);

    // queryLatest should return the latest 3 records from RAM
    uint8_t out[8 * 5];
    std::memset(out, 0, sizeof(out));
    uint16_t got = db.queryLatest(0, out, 3);
    EXPECT_EQ(got, 3u);
    for (uint16_t i = 0; i < got; ++i) {
        uint32_t val;
        std::memcpy(&val, out + i * 8 + 4, 4);
        EXPECT_EQ(val, (i + 2) * 10u);
    }

    db.close();
}

// ── Primary channel: fill buffer → trigger swap+flush → block on disk ────────

TEST(ArcanaTsDbTest, PrimaryChannelFlushOnFull) {
    DbCtx d;
    TestClock::reset();
    ArcanaTsDb db;
    ASSERT_TRUE(db.open("pf.ats", d.makeCfg(/*primary*/0)));
    ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));
    ASSERT_TRUE(db.start());

    // Each record is 8 bytes; 4064/8 = 508 records per block.
    // Write 508 records to fill bufA exactly, then 1 more to trigger swap+flush.
    uint8_t rec[8];
    for (uint32_t i = 0; i < 510; ++i) {
        mkRec(rec, 0, i);
        ASSERT_TRUE(db.append(0, rec));
    }

    // After the swap a flushPrimaryBuffer ran → at least one full block on disk.
    EXPECT_GE(db.getStats().blocksWritten, 1u);

    // Force pending data out
    ASSERT_TRUE(db.flush());
    EXPECT_EQ(db.getStats().totalRecords, 510u);

    // Latest 5 records should be queryable (mix of disk + RAM)
    uint8_t out[8 * 5];
    uint16_t got = db.queryLatest(0, out, 5);
    EXPECT_EQ(got, 5u);

    // Expect the most recent values are 505..509
    uint32_t lastVal;
    std::memcpy(&lastVal, out + 4 * 8 + 4, 4);
    EXPECT_EQ(lastVal, 509u);

    db.close();
}

// ── Slow channel: append + RAM query ─────────────────────────────────────────

TEST(ArcanaTsDbTest, SlowChannelAppendQueryRam) {
    DbCtx d;
    TestClock::reset();
    ArcanaTsDb db;
    // Primary disabled (0xFF) so channel 0 is also slow
    ASSERT_TRUE(db.open("s.ats", d.makeCfg(/*primary*/0xFF)));
    ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));
    ASSERT_TRUE(db.addChannel(1, makeAdcSchema()));
    ASSERT_TRUE(db.start());

    // Write 4 records to ch0 and 3 to ch1, all in slow buffer
    uint8_t rec[8];
    for (uint32_t i = 0; i < 4; ++i) {
        mkRec(rec, 0, 1000 + i);
        ASSERT_TRUE(db.append(0, rec));
    }
    for (uint32_t i = 0; i < 3; ++i) {
        mkRec(rec, 0, 2000 + i);
        ASSERT_TRUE(db.append(1, rec));
    }
    EXPECT_EQ(db.getStats().totalRecords, 7u);

    // Query latest 2 from ch0
    uint8_t out[8 * 4];
    uint16_t got = db.queryLatest(0, out, 2);
    EXPECT_EQ(got, 2u);
    uint32_t v0, v1;
    std::memcpy(&v0, out + 0 * 8 + 4, 4);
    std::memcpy(&v1, out + 1 * 8 + 4, 4);
    EXPECT_EQ(v0, 1002u);
    EXPECT_EQ(v1, 1003u);

    // Query latest 5 from ch1 (only 3 available)
    got = db.queryLatest(1, out, 5);
    EXPECT_EQ(got, 3u);

    db.close();
}

// ── Slow channel: fill until flush triggered ─────────────────────────────────

TEST(ArcanaTsDbTest, SlowChannelFlushOnFull) {
    DbCtx d;
    TestClock::reset();
    ArcanaTsDb db;
    ASSERT_TRUE(db.open("sf.ats", d.makeCfg(/*primary*/0xFF)));
    ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));
    ASSERT_TRUE(db.start());

    // Slow tagged record = 1 + 8 = 9 bytes; 4064/9 ≈ 451 records per block
    uint8_t rec[8];
    for (uint32_t i = 0; i < 460; ++i) {
        mkRec(rec, 0, i);
        ASSERT_TRUE(db.append(0, rec));
    }
    ASSERT_TRUE(db.flush());
    EXPECT_GE(db.getStats().blocksWritten, 1u);
    EXPECT_EQ(db.getStats().totalRecords, 460u);

    db.close();
}

// ── Mixed primary + slow on the same DB ──────────────────────────────────────

TEST(ArcanaTsDbTest, MixedPrimaryAndSlowChannels) {
    DbCtx d;
    TestClock::reset();
    ArcanaTsDb db;
    ASSERT_TRUE(db.open("m.ats", d.makeCfg(/*primary*/0)));
    ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));   // primary
    ASSERT_TRUE(db.addChannel(1, makeAdcSchema()));   // slow
    ASSERT_TRUE(db.start());

    uint8_t rec[8];
    for (uint32_t i = 0; i < 10; ++i) {
        mkRec(rec, 0, 100 + i);
        ASSERT_TRUE(db.append(0, rec));
    }
    for (uint32_t i = 0; i < 6; ++i) {
        mkRec(rec, 0, 200 + i);
        ASSERT_TRUE(db.append(1, rec));
    }

    EXPECT_EQ(db.getStats().perChannelRecords[0], 10u);
    EXPECT_EQ(db.getStats().perChannelRecords[1], 6u);

    db.close();
}

// ── Query by time range ──────────────────────────────────────────────────────

TEST(ArcanaTsDbTest, QueryByTimeReturnsExpectedRange) {
    DbCtx d;
    TestClock::reset(1000, 1);
    ArcanaTsDb db;
    ASSERT_TRUE(db.open("qt.ats", d.makeCfg(/*primary*/0)));
    ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));
    ASSERT_TRUE(db.start());

    // Write 600 records with embedded logical timestamps. The engine reads
    // the record's first 4 bytes as the timestamp inside queryByTime, so we
    // control the test by writing ts directly into each record.
    uint8_t rec[8];
    for (uint32_t i = 0; i < 600; ++i) {
        mkRec(rec, /*ts=*/1000 + i, /*val=*/i);
        ASSERT_TRUE(db.append(0, rec));
    }
    ASSERT_TRUE(db.flush());

    CollectCtx ctx;
    ctx.expectChannel = 0;
    EXPECT_TRUE(db.queryByTime(0, 1100, 1109, &collectCb, &ctx));
    EXPECT_EQ(ctx.rows.size(), 10u);
    EXPECT_EQ(ctx.rows.front().first, 1100u);
    EXPECT_EQ(ctx.rows.back().first,  1109u);
    EXPECT_EQ(ctx.rows.front().second, 100u);

    db.close();
}

// ── Query all channels by time ──────────────────────────────────────────────

TEST(ArcanaTsDbTest, QueryAllChannelsByTime) {
    DbCtx d;
    TestClock::reset();
    ArcanaTsDb db;
    ASSERT_TRUE(db.open("qa.ats", d.makeCfg(/*primary*/0xFF)));
    ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));
    ASSERT_TRUE(db.addChannel(1, makeAdcSchema()));
    ASSERT_TRUE(db.start());

    uint8_t rec[8];
    for (uint32_t i = 0; i < 4; ++i) {
        mkRec(rec, 5000 + i, 100 + i);
        ASSERT_TRUE(db.append(0, rec));
    }
    for (uint32_t i = 0; i < 4; ++i) {
        mkRec(rec, 5000 + i, 200 + i);
        ASSERT_TRUE(db.append(1, rec));
    }
    ASSERT_TRUE(db.flush());

    CollectCtx ctx;
    EXPECT_TRUE(db.queryAllChannelsByTime(0, 0xFFFFFFFFu, &collectCb, &ctx));
    EXPECT_GE(ctx.rows.size(), 8u);

    db.close();
}

// ── findChannelBySchema / findChannelBySchemaId / getSchema ──────────────────

TEST(ArcanaTsDbTest, ChannelLookupAccessors) {
    DbCtx d;
    TestClock::reset();
    ArcanaTsDb db;
    ASSERT_TRUE(db.open("l.ats", d.makeCfg(/*primary*/0)));

    auto adc = makeAdcSchema();
    ASSERT_TRUE(db.addChannel(2, adc));
    ASSERT_TRUE(db.start());

    EXPECT_EQ(db.findChannelBySchema("ADC8"), 2);
    EXPECT_EQ(db.findChannelBySchema("MISSING"), -1);
    EXPECT_EQ(db.findChannelBySchemaId(adc.schemaId()), 2);
    EXPECT_EQ(db.findChannelBySchemaId(0xDEADBEEF), -1);

    EXPECT_NE(db.getSchema(2), nullptr);
    EXPECT_EQ(db.getSchema(0), nullptr);
    EXPECT_EQ(db.getSchema(MAX_CHANNELS), nullptr);

    db.close();
}

// ── Recovery: close then re-open existing file ──────────────────────────────

TEST(ArcanaTsDbTest, CloseAndRecoverPlaintextHeader) {
    DbCtx d;
    TestClock::reset();
    {
        ArcanaTsDb db;
        ASSERT_TRUE(db.open("r.ats", d.makeCfg(/*primary*/0)));
        ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));
        ASSERT_TRUE(db.start());
        uint8_t rec[8];
        for (uint32_t i = 0; i < 600; ++i) {
            mkRec(rec, 7000 + i, i);
            ASSERT_TRUE(db.append(0, rec));
        }
        ASSERT_TRUE(db.flush());
        ASSERT_TRUE(db.close());
    }

    // Re-open: header recovery + sparse-index rebuild
    ArcanaTsDb db2;
    ASSERT_TRUE(db2.open("r.ats", d.makeCfg(/*primary*/0)));
    EXPECT_GE(db2.getChannelCount(), 1u);
    EXPECT_TRUE(db2.isOpen());
    db2.close();
}

// ── openReadOnly path ────────────────────────────────────────────────────────

TEST(ArcanaTsDbTest, OpenReadOnlyAfterClose) {
    DbCtx d;
    TestClock::reset(50000, 1);
    {
        ArcanaTsDb db;
        ASSERT_TRUE(db.open("ro.ats", d.makeCfg(/*primary*/0)));
        ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));
        ASSERT_TRUE(db.start());
        uint8_t rec[8];
        for (uint32_t i = 0; i < 700; ++i) {
            mkRec(rec, 50000 + i, i);
            ASSERT_TRUE(db.append(0, rec));
        }
        ASSERT_TRUE(db.flush());
        db.close();
    }

    ArcanaTsDb db2;
    ASSERT_TRUE(db2.openReadOnly("ro.ats", d.makeCfg(/*primary*/0)));
    EXPECT_TRUE(db2.isReadOnly());

    CollectCtx ctx;
    EXPECT_TRUE(db2.queryByTime(0, 50100, 50104, &collectCb, &ctx));
    EXPECT_GE(ctx.rows.size(), 1u);

    // Mutating ops must be rejected on read-only
    uint8_t rec[8] = {0};
    EXPECT_FALSE(db2.append(0, rec));
    EXPECT_FALSE(db2.flush());
    EXPECT_FALSE(db2.addChannel(1, makeAdcSchema()));
    EXPECT_FALSE(db2.addChannelLive(1, makeAdcSchema()));

    db2.close();
}

// ── Encrypted header roundtrip ───────────────────────────────────────────────

TEST(ArcanaTsDbTest, EncryptedHeaderRoundtrip) {
    DbCtx d;
    TestClock::reset();
    {
        ArcanaTsDb db;
        ASSERT_TRUE(db.open("eh.ats", d.makeCfg(/*primary*/0,
                                                OverflowPolicy::Block,
                                                d.headerKey)));
        ASSERT_TRUE(db.addChannel(3, makeAdcSchema()));
        ASSERT_TRUE(db.start());

        uint8_t rec[8];
        for (uint32_t i = 0; i < 50; ++i) {
            mkRec(rec, 9000 + i, i);
            ASSERT_TRUE(db.append(3, rec));
        }
        ASSERT_TRUE(db.flush());
        db.close();
    }

    // First 4 bytes must NOT be plaintext "ATS2" — encrypted header is opaque
    EXPECT_NE(0, std::memcmp(d.file.data.data(), "ATS2", 4));

    // Re-open with the same headerKey: schema must be recovered
    ArcanaTsDb db2;
    ASSERT_TRUE(db2.open("eh.ats", d.makeCfg(/*primary*/0,
                                             OverflowPolicy::Block,
                                             d.headerKey)));
    EXPECT_NE(db2.getSchema(3), nullptr);
    EXPECT_EQ(db2.findChannelBySchema("ADC8"), 3);
    db2.close();
}

TEST(ArcanaTsDbTest, EncryptedHeaderWrongKeyRejected) {
    DbCtx d;
    TestClock::reset();
    {
        ArcanaTsDb db;
        ASSERT_TRUE(db.open("ek.ats", d.makeCfg(/*primary*/0,
                                                OverflowPolicy::Block,
                                                d.headerKey)));
        ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));
        ASSERT_TRUE(db.start());
        db.close();
    }

    uint8_t wrongKey[32];
    for (int i = 0; i < 32; ++i) wrongKey[i] = static_cast<uint8_t>(0xEE);

    ArcanaTsDb db2;
    AtsConfig cfg = d.makeCfg(/*primary*/0, OverflowPolicy::Block, wrongKey);
    EXPECT_FALSE(db2.open("ek.ats", cfg));
}

// ── addChannelLive after start ───────────────────────────────────────────────

TEST(ArcanaTsDbTest, AddChannelLiveAfterStart) {
    DbCtx d;
    TestClock::reset();
    ArcanaTsDb db;
    ASSERT_TRUE(db.open("acl.ats", d.makeCfg(/*primary*/0)));
    ASSERT_TRUE(db.addChannel(0, makeAdcSchema()));
    ASSERT_TRUE(db.start());

    EXPECT_TRUE(db.addChannelLive(1, makeAdcSchema(), 50));
    // Idempotent re-add returns true
    EXPECT_TRUE(db.addChannelLive(1, makeAdcSchema(), 50));
    // Out-of-range rejected
    EXPECT_FALSE(db.addChannelLive(MAX_CHANNELS, makeAdcSchema()));

    uint8_t rec[8];
    mkRec(rec, 1, 1);
    EXPECT_TRUE(db.append(1, rec));

    db.close();
}

// ── Schema accessors / record-size math ──────────────────────────────────────

TEST(ArcanaTsSchemaTest, PredefinedSchemasHaveCorrectRecordSizes) {
    using arcana::ats::ArcanaTsSchema;
    // ADS1298_8CH: U32 + 8x I24 = 4 + 24 = 28
    EXPECT_EQ(ArcanaTsSchema::ads1298_8ch().recordSize, 28u);
    // MPU6050: U32 + F32 + 3xI16 = 4 + 4 + 6 = 14
    EXPECT_EQ(ArcanaTsSchema::mpu6050().recordSize, 14u);
    // DHT11: U32 + I16 + I16 = 8
    EXPECT_EQ(ArcanaTsSchema::dht11().recordSize, 8u);
    // PUMP: U32 + U8 + U32 = 9
    EXPECT_EQ(ArcanaTsSchema::pump().recordSize, 9u);
    // CREDS: U32 + BYTES(232) = 236
    EXPECT_EQ(ArcanaTsSchema::credentials().recordSize, 236u);

    // recordsPerBlock math
    EXPECT_EQ(ArcanaTsSchema::dht11().recordsPerBlock(),
              static_cast<uint16_t>(BLOCK_PAYLOAD_SIZE / 8));
}

TEST(ArcanaTsSchemaTest, AddFieldRespectsMaxFields) {
    ArcanaTsSchema s;
    s.setName("MAX");
    for (int i = 0; i < ArcanaTsSchema::MAX_FIELDS; ++i) {
        EXPECT_TRUE(s.addField("f", FieldType::U8));
    }
    // 17th add must fail
    EXPECT_FALSE(s.addField("over", FieldType::U8));
}

TEST(ArcanaTsSchemaTest, SchemaIdIsStableAndDistinct) {
    auto a = ArcanaTsSchema::dht11();
    auto b = ArcanaTsSchema::mpu6050();
    EXPECT_NE(a.schemaId(), 0u);
    EXPECT_NE(b.schemaId(), 0u);
    EXPECT_NE(a.schemaId(), b.schemaId());

    auto a2 = ArcanaTsSchema::dht11();
    EXPECT_EQ(a.schemaId(), a2.schemaId());
}
