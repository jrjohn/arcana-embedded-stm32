# ArcanaTS v2: Cross-Platform Multi-Channel Embedded TSDB

## Context

FlashDB TSDB on SD card: ~80% redundant I/O (sector status, erase simulation, GC, init scan). Current 5-7 rec/s is too slow for ADS1298 (1kHz x 8ch). Need a custom TSDB that:
- **Cross-platform**: STM32, ESP32, any embedded with FreeRTOS
- **Medical/Defense grade**: Encryption mandatory, zero data loss, tamper detection
- **Multi-channel**: One DB file holds ALL sensors + device operational data simultaneously
- **Single upload**: One `.ats` file = complete device snapshot
- **1000+ rec/s** via buffered block I/O
- **On-device query**: Filter by channel, time range, latest N

---

## Architecture Overview

```
Sensor A (1kHz) ──→ append(chA, rec) ──→ [Primary Buffer 4KB]  ──→ swap ──→ [Flush Task]
Sensor B (10Hz) ──→ append(chB, rec) ──→ [Shared Slow Buffer 4KB]          │
Device Status   ──→ append(chC, rec) ──→ [Shared Slow Buffer]              │
                                                                            ▼
                              ┌──────────────────────────────────────┐
                              │ Flush Task (background, ~5ms)        │
                              │ 1. Build block header (channelId)    │
                              │ 2. ICipher::crypt (encrypt payload)  │
                              │ 3. IFilePort::write 4KB              │
                              │ 4. IFilePort::sync                   │
                              │ 5. Flush slow channels if ready      │
                              └──────────────────┬───────────────────┘
                                                 ▼
                                    [Single .ats file on SD/Flash]

Query ──→ Sparse index (by channel + time) ──→ Read block ──→ Decrypt ──→ Records
```

### Multi-Channel Buffer Strategy

RAM is the scarcest resource (F103 has ~17KB free). Strategy:

| Channel Type | Buffer | Flush Trigger | Example |
|---|---|---|---|
| **Primary** (highest rate) | Dedicated double-buffer 2x4KB | Buffer full | ADS1298 @ 1kHz |
| **Slow** (all others) | Shared tagged-record buffer 4KB | Buffer full OR timer (60s) | MPU6050, DHT11, device status |

**Slow buffer format** — records from different channels interleaved with 2-byte tag:
```
[channelId:1][recordLen:1][recordData:N] [channelId:1][recordLen:1][recordData:N] ...
```

When slow buffer flushes, it produces a **multi-channel block** (`channelId = 0xFF`).

**RAM Budget (STM32F103, ~17KB free):**

| Component | Size | Notes |
|---|---|---|
| Primary double-buffer | 8192 | 2 x 4KB, zero-copy swap for high-rate channel |
| Shared slow buffer | 4096 | Tagged records from all slow channels |
| Read cache | 0 | Shares with flush buffer (saves 4KB) |
| Sparse index | 1360 | 85 entries x 16 bytes |
| Per-channel state | ~480 | 8 channels x 60 bytes (schema ptr, counters) |
| DB state | ~200 | Config, file handle, stats |
| **Total** | **~14.3KB** | Fits in 17KB |

**ESP32 (~520KB free):** Can afford dedicated buffers per channel + large read cache + bigger index.

### Multi-Instance Support

Two `ArcanaTsDb` instances can run simultaneously — typical use case: **write today's DB + read/upload yesterday's DB**.

```cpp
// Instance 1: Active writer (today)
static ArcanaTsDb writeDb;
static uint8_t wBufA[4096], wBufB[4096], wSlow[4096];
AtsConfig writeCfg = { .primaryBufA=wBufA, .primaryBufB=wBufB, .slowBuf=wSlow, ... };
writeDb.open("20260313.ats", writeCfg);

// Instance 2: Read-only (yesterday, for upload/query)
static ArcanaTsDb readDb;
static uint8_t rCache[4096];  // only needs read cache
AtsConfig readCfg = { .readCache=rCache, ... };
readDb.openReadOnly("20260312.ats", readCfg);
readDb.queryBySchema("ADS1298_8CH", start, end, uploadCallback, ctx);
readDb.close();  // frees rCache for next file
```

**RAM for dual-instance (STM32F103):**

| Component | Write DB | Read DB | Total |
|---|---|---|---|
| Primary double-buffer | 8192 | — | 8192 |
| Slow buffer | 4096 | — | 4096 |
| Read cache | (shared) | 4096 | 4096 |
| Index + state | 1840 | 1840 | 3680 |
| **Total** | | | **~20KB** |

F103 (17KB free): tight — requires **time-slicing**: close read DB before opening write DB's read cache. Or reduce write index to 42 entries (saves 680B). Or use read DB only when write DB flush is idle (share readCache buffer).

ESP32 (520KB): no problem. Both instances fit easily.

**`openReadOnly()` mode:**
- Reads file header + channel descriptors + builds sparse index
- No write buffers allocated (primaryBufA/B, slowBuf all `nullptr`)
- `append()` returns error
- `queryBySchema()`, `queryByTime()`, `queryAllChannelsByTime()` all work
- Each `IFilePort` instance opens its own file handle (two `FIL` structs for FatFS)

---

## Platform Abstraction Layer (PAL)

ArcanaTS core has **ZERO platform includes**. All platform-specific behavior injected via interfaces.

### IFilePort — File I/O

```cpp
// Shared/Inc/ats/IFilePort.hpp
namespace arcana { namespace ats {
class IFilePort {
public:
    virtual ~IFilePort() {}
    virtual bool open(const char* path, uint8_t mode) = 0;  // mode: R=1,W=2,RW=3,Create=0x10
    virtual bool close() = 0;
    virtual int32_t read(uint8_t* buf, uint32_t size) = 0;   // returns bytes read, -1 on error
    virtual int32_t write(const uint8_t* buf, uint32_t size) = 0;
    virtual bool seek(uint32_t offset) = 0;
    virtual bool sync() = 0;
    virtual uint32_t tell() = 0;
    virtual uint32_t size() = 0;
    virtual bool truncate() = 0;  // at current position
    virtual bool isOpen() const = 0;
};
}}
```

| Platform | Implementation | Wraps |
|---|---|---|
| STM32 (FatFS) | `FatFsFilePort` | `f_open/f_read/f_write/f_sync/f_lseek` |
| ESP32 (VFS) | `VfsFilePort` | `fopen/fread/fwrite/fsync` via ESP-IDF VFS |
| Linux (test) | `PosixFilePort` | `open/read/write/fsync/lseek` |

### ICipher — Pluggable Encryption

```cpp
// Shared/Inc/ats/ICipher.hpp
namespace arcana { namespace ats {
class ICipher {
public:
    virtual ~ICipher() {}
    virtual void crypt(const uint8_t key[32], const uint8_t nonce[12],
                       uint32_t counter, uint8_t* data, uint16_t len) = 0;
    virtual uint8_t cipherType() const = 0;  // stored in file header
};
}}
```

| cipherType | Class | Platform | Notes |
|---|---|---|---|
| 0 | `NullCipher` | All | No-op, debug only |
| 1 | `ChaCha20Cipher` | All (software) | ~350 cycles/block on Cortex-M3, no HW dep |
| 2 | `Aes256CtrCipher` | Software fallback | ~1000 cycles/block without HW |
| 2 | `Esp32HwAesCipher` | ESP32 | `mbedtls_aes_crypt_ctr()` with HW accel, ~10x faster |
| 2 | `Stm32HwAesCipher` | STM32H7/L4 | `HAL_CRYP_Encrypt()` for AES peripheral |

### IMutex — RTOS Abstraction

```cpp
// Shared/Inc/ats/IMutex.hpp
namespace arcana { namespace ats {
class IMutex {
public:
    virtual ~IMutex() {}
    virtual bool lock(uint32_t timeoutMs = 0xFFFFFFFF) = 0;
    virtual void unlock() = 0;
};
}}
```

| Platform | Implementation | Wraps |
|---|---|---|
| STM32 + ESP32 | `FreeRtosMutex` | `xSemaphoreCreateMutexStatic/Take/Give` |
| Linux (test) | `PosixMutex` | `pthread_mutex_t` |

### Time Source — Function Pointer

```cpp
// In ArcanaTsTypes.hpp
using AtsGetTimeFn = uint32_t (*)();  // returns UTC epoch seconds
```

Injected at `open()`. Callers wire to `SystemClock::now()` (STM32), `time(NULL)` (Linux), etc.

---

## File Format: `.ats` v2

### Block 0: File Header (4096 bytes)

```
[0x0000] Global Header (64 bytes)
[0x0040] Channel Descriptors: 8 x 32 bytes = 256 bytes
[0x0140] Field Tables: 8 x (16 fields x 16 bytes) = 2048 bytes
[0x0940] Persisted StorageStats (128 bytes)
[0x09C0] Reserved (64 bytes)
[0x0A00] Shadow copy of [0x0000-0x09FF] (2560 bytes, for crash recovery)
```

**Global Header (64 bytes):**

| Offset | Size | Field | Description |
|---|---|---|---|
| 0x0000 | 4 | magic | `"ATS2"` |
| 0x0004 | 1 | version | 2 |
| 0x0005 | 1 | headerBlocks | 1 |
| 0x0006 | 2 | flags | bit0=encrypted, bit1=has_index, bit2=has_hmac, bit3=has_shadow |
| 0x0008 | 1 | cipherType | 0=none, 1=ChaCha20, 2=AES-256-CTR |
| 0x0009 | 1 | channelCount | Number of active channels (1-8) |
| 0x000A | 1 | overflowPolicy | 0=BLOCK (medical), 1=DROP (IoT) |
| 0x000B | 1 | deviceUidSize | Actual UID bytes (6 for ESP32, 12 for STM32) |
| 0x000C | 4 | createdEpoch | UTC epoch when file created |
| 0x0010 | 16 | deviceUid | Device UID (zero-padded to 16 bytes) |
| 0x0020 | 4 | totalBlockCount | Total data blocks written |
| 0x0024 | 4 | lastSeqNo | Last committed block sequence number |
| 0x0028 | 4 | indexBlockOffset | Block# of sparse index (0=none) |
| 0x002C | 4 | headerCrc32 | CRC-32 of bytes 0x0000-0x002B |
| 0x0030 | 16 | reserved | |

**Channel Descriptor (32 bytes each, 8 slots at 0x0040):**

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | channelId (0-7, or 0xFF=unused) |
| 1 | 1 | fieldCount (1-16) |
| 2 | 2 | recordSize (bytes per plaintext record) |
| 4 | 2 | sampleRateHz (nominal, 0=variable) |
| 6 | 2 | recordCount (low 16 bits, high bits in stats) |
| 8 | 24 | name (null-terminated, e.g. "ADS1298_8CH", "MPU6050", "DEVICE") |

**Field Table (256 bytes per channel, at 0x0140 + channelIdx * 256):**

Each field: 16 bytes x 16 fields max (same as v1)

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | name (null-terminated) |
| 8 | 1 | type (U8=0, U16=1, U32=2, I16=3, I32=4, F32=5, I24=6, U64=7, BYTES=8) |
| 9 | 1 | byte offset in record |
| 10 | 2 | scale numerator |
| 12 | 2 | scale denominator |
| 14 | 2 | reserved |

### Data Blocks (4096 bytes each)

**Single-channel block (channelId = 0-7):**

| Offset | Size | Field | Description |
|---|---|---|---|
| 0x0000 | 4 | blockSeqNo | Global monotonic sequence (written LAST for atomic commit) |
| 0x0004 | 1 | channelId | Which channel's data (0-7) |
| 0x0005 | 1 | flags | bit0=partial (not full block) |
| 0x0006 | 2 | recordCount | Records in this block |
| 0x0008 | 4 | firstTimestamp | Epoch of first record |
| 0x000C | 4 | lastTimestamp | Epoch of last record |
| 0x0010 | 12 | nonce | `[seqNo:4LE][createdEpoch:4LE][0x00:4]` |
| 0x001C | 4 | payloadCrc32 | CRC-32 of encrypted payload |
| 0x0020 | 4064 | payload | Encrypted records (fixed-size per channel) |

Records per block: `4064 / channel.recordSize`

**Multi-channel block (channelId = 0xFF):**

Same 32-byte header, but payload contains tagged variable-length records:
```
payload = [tag0][tag1][tag2]...[padding]
tag = [channelId:1][recordData:channel.recordSize]
```

`recordCount` = total tagged records across all channels in this block.

### Nonce Construction

```
nonce[0:3]  = blockSeqNo (LE)      — unique per block
nonce[4:7]  = file createdEpoch (LE) — unique per file
nonce[8:11] = 0x00000000
```

Guarantees nonce uniqueness without persistent counter.

### Index Blocks (end of file, written on close/rotation)

```
[0x0000] magic "IDX2" (4 bytes)
[0x0004] entryCount (4 bytes)
[0x0008] crc32 of entries (4 bytes)
[0x000C] reserved (4 bytes)
[0x0010] IndexEntry[] (16 bytes each)
```

**IndexEntry (16 bytes):**
```
[blockNumber:4][channelId:1][flags:1][recordCount:2][firstTimestamp:4][lastTimestamp:4]
```

---

## Multi-Channel Schema API

```cpp
// Shared/Inc/ats/ArcanaTsSchema.hpp
namespace arcana { namespace ats {

enum class FieldType : uint8_t {
    U8=0, U16=1, U32=2, I16=3, I32=4, F32=5, I24=6, U64=7, BYTES=8
};

struct FieldDesc {
    char name[8];
    FieldType type;
    uint8_t offset;
    uint16_t scaleNum;
    uint16_t scaleDen;
    uint16_t reserved;
};

class ArcanaTsSchema {
public:
    static const uint8_t MAX_FIELDS = 16;

    FieldDesc fields[MAX_FIELDS];
    uint8_t fieldCount;
    uint16_t recordSize;
    char name[24];

    ArcanaTsSchema();
    bool addField(const char* name, FieldType type, uint8_t offset,
                  uint16_t scaleNum = 1, uint16_t scaleDen = 1);
    void setName(const char* schemaName);
    uint32_t schemaId() const;  // CRC-32 of field descriptors
    uint16_t recordsPerBlock() const;  // 4064 / recordSize

    // Predefined convenience factories — sensor data
    static ArcanaTsSchema mpu6050();     // ts,temp,ax,ay,az (14 bytes)
    static ArcanaTsSchema ads1298_8ch(); // ts,ch0-ch7 (28 bytes)
    static ArcanaTsSchema dht11();       // ts,temp,humidity (8 bytes)
    static ArcanaTsSchema deviceStatus();// ts,vbat,cpuTemp,uptime,freeHeap (16 bytes)
    static ArcanaTsSchema pump();        // ts,state,runtime (9 bytes)

    // Daily operational schemas (recorded in daily .ats, uploaded with sensor data)
    static ArcanaTsSchema userAction();      // ts,actionType,actionCode,param (11 bytes)
    static ArcanaTsSchema errorLog();        // ts,severity,source,errorCode,param (12 bytes)
    static ArcanaTsSchema configSnapshot();  // ts,pressTarget,flowRate,timerSec,threshHi/Lo,mode,flags (14 bytes)

    // Device lifecycle schemas (for permanent device.ats)
    static ArcanaTsSchema deviceInfo();      // ts,fw_ver,hw_rev,serial (16 bytes)
    static ArcanaTsSchema lifecycleEvent();  // ts,eventType,eventCode,param (12 bytes)
    static ArcanaTsSchema deviceCounters();  // ts,powerOnHrs,pumpCycles,errCnt,sdWritesMB (20 bytes)
    static ArcanaTsSchema config();          // ts,pressTarget,flowRate,timerSec,threshHi/Lo,mode,flags (14 bytes)
    static ArcanaTsSchema calibration();     // ts,sensorId,offset,gain,zeroPoint,tempCoeff,operator (18 bytes)
};
}}
```

Usage — register sensors + operational channels at open time:
```cpp
ArcanaTsDb db;
AtsConfig cfg = { .file=&fatfsFile, .cipher=&chacha, .mutex=&rtosMutex, ... };

db.open("20260313.ats", cfg);
// Sensor channels
db.addChannel(0, ArcanaTsSchema::ads1298_8ch());     // primary (1kHz)
db.addChannel(1, ArcanaTsSchema::mpu6050());          // slow
db.addChannel(2, ArcanaTsSchema::dht11());             // slow
db.addChannel(3, ArcanaTsSchema::deviceStatus());      // slow
// Operational channels (uploaded with sensor data — daily black box)
db.addChannel(4, ArcanaTsSchema::userAction());        // slow, event-driven
db.addChannel(5, ArcanaTsSchema::configSnapshot());    // slow, event-driven
db.addChannel(6, ArcanaTsSchema::errorLog());          // slow, event-driven
// Channel 7 reserved for future use
db.start();  // begins flush task
```

---

## Core Engine: ArcanaTsDb

```cpp
// Shared/Inc/ats/ArcanaTsDb.hpp
namespace arcana { namespace ats {

static const uint8_t MAX_CHANNELS = 8;
static const uint16_t BLOCK_SIZE = 4096;
static const uint16_t BLOCK_HEADER_SIZE = 32;
static const uint16_t BLOCK_PAYLOAD_SIZE = BLOCK_SIZE - BLOCK_HEADER_SIZE;  // 4064
static const uint8_t MULTI_CHANNEL_ID = 0xFF;

enum class OverflowPolicy : uint8_t { Block = 0, Drop = 1 };

struct StorageStats {
    uint32_t blocksWritten;
    uint32_t blocksFailed;
    uint32_t overflowDrops;
    uint32_t crcErrors;
    uint32_t recoveryTruncations;
    uint32_t totalRecords;
    uint32_t perChannelRecords[MAX_CHANNELS];
    uint32_t lastTimestamp;
};

struct AtsConfig {
    IFilePort* file;
    ICipher* cipher;
    IMutex* mutex;
    AtsGetTimeFn getTime;
    const uint8_t* key;          // 32-byte encryption key
    const uint8_t* deviceUid;    // device UID bytes
    uint8_t deviceUidSize;       // 6 (ESP32) or 12 (STM32)
    OverflowPolicy overflow;     // Block (medical) or Drop (IoT)
    uint8_t primaryChannel;      // channel ID that gets dedicated double-buffer (0xFF = none)
    // Buffers (caller-allocated, static)
    uint8_t* primaryBufA;        // 4KB, required if primaryChannel != 0xFF
    uint8_t* primaryBufB;        // 4KB, required if primaryChannel != 0xFF
    uint8_t* slowBuf;            // 4KB, for all non-primary channels
    uint8_t* readCache;          // 4KB, optional (nullptr = share with slowBuf)
};

using RecordCallback = bool (*)(uint8_t channelId, const uint8_t* record,
                                 uint32_t timestamp, void* ctx);

class ArcanaTsDb {
public:
    // Lifecycle
    bool open(const char* path, const AtsConfig& cfg);          // read-write (new or resume)
    bool openReadOnly(const char* path, const AtsConfig& cfg);  // read-only (for upload/query)
    bool addChannel(uint8_t channelId, const ArcanaTsSchema& schema);
    bool start();   // write file header, begin accepting appends
    bool close();   // flush all, write index, update header stats, sync

    // Write (hot path)
    bool append(uint8_t channelId, const uint8_t* record);  // ~0.7us for primary
    bool flush();   // force flush all pending buffers

    // Query — by channel ID
    uint16_t queryLatest(uint8_t channelId, uint8_t* outBuf, uint16_t maxRecords) const;
    bool queryByTime(uint8_t channelId, uint32_t startEpoch, uint32_t endEpoch,
                     RecordCallback cb, void* ctx);

    // Query — by schema name (resolves to channel ID internally)
    uint16_t queryLatestBySchema(const char* schemaName, uint8_t* outBuf, uint16_t maxRecords) const;
    bool queryBySchema(const char* schemaName, uint32_t startEpoch, uint32_t endEpoch,
                       RecordCallback cb, void* ctx);

    // Query — all channels (cross-channel time-ordered view)
    bool queryAllChannelsByTime(uint32_t startEpoch, uint32_t endEpoch,
                                RecordCallback cb, void* ctx);

    // Channel/Schema lookup
    int8_t findChannelBySchema(const char* schemaName) const;   // returns channelId or -1
    int8_t findChannelBySchemaId(uint32_t schemaId) const;      // by CRC-32

    // Info
    const StorageStats& getStats() const;
    uint8_t getChannelCount() const;
    const ArcanaTsSchema* getSchema(uint8_t channelId) const;
    bool isOpen() const;
};
}}
```

### Append Hot Path

**Primary channel (~0.7us):**
1. Lock mutex
2. `memcpy` record into `primaryBufA` at write offset
3. If buffer full: swap A/B, signal flush semaphore
4. Unlock mutex

**Slow channel (~1us):**
1. Lock mutex
2. Write `[channelId:1][record:N]` into `slowBuf` at write offset
3. If buffer full: signal flush for slow buffer
4. Unlock mutex

**Overflow (POLICY_BLOCK):** If flush buffer is busy, `append()` blocks on semaphore until flush completes. Zero data loss. Acceptable latency for medical: worst case ~5ms (one SD write).

**Overflow (POLICY_DROP):** If flush buffer is busy, increment `overflowDrops`, return false. Caller can log or retry.

### Flush Task

Background FreeRTOS task, signals via binary semaphore:

1. **Primary buffer flush:**
   - Build 32-byte block header: `channelId = primaryChannel`, seqNo++
   - Compute CRC-32 of payload
   - Encrypt payload via `cipher->crypt()`
   - Write payload first, then header fields, then `blockSeqNo` LAST (atomic commit)
   - `file->sync()`
   - Update sparse index

2. **Slow buffer flush** (after primary, or on timer):
   - If slow buffer has data AND (buffer full OR timer expired):
   - Build multi-channel block: `channelId = 0xFF`
   - Tagged records already in buffer
   - Encrypt, CRC, write, sync
   - Update sparse index

3. **Timer-based slow flush:**
   - Configurable interval (default 60s)
   - Ensures low-rate channel data is persisted even if buffer never fills
   - Critical for medical: limits max data-loss window to flush interval

---

## Data Integrity (Medical/Defense)

### Atomic Block Commit Protocol

Write order ensures partially-written blocks are detectable:
1. Write encrypted payload at offset 0x0020
2. Write header fields 0x0004-0x001F (timestamps, CRC, nonce)
3. Write `blockSeqNo` at offset 0x0000 as FINAL write
4. `sync()`

On recovery: `blockSeqNo == 0x00000000` or `0xFFFFFFFF` → uncommitted, truncate.

### Integrity Features

| Feature | Protection | Cost |
|---|---|---|
| CRC-32 per block | Detects bit rot, corruption | 4 bytes/block, ~0.1ms compute |
| Atomic commit (seqNo last) | Detects incomplete writes | Zero overhead |
| Shadow file header | Recovers from header corruption | 2.5KB in header block |
| Sequence gap detection | Detects missing blocks on recovery | Zero overhead (seqNo monotonic) |
| OverflowPolicy::Block | Prevents silent data loss | Backpressure on append (~5ms max) |
| `overflowDrops` counter | Audit trail for drops (IoT mode) | 4 bytes in stats |
| `headerCrc32` | Validates header integrity | 4 bytes |

### Optional HMAC-SHA256 (Defense mode)

When `flags.bit2 = has_hmac`:
- 32-byte HMAC appended after block header (offset 0x0020)
- Payload starts at 0x0040 (4032 bytes instead of 4064)
- HMAC computed over: header[0x0000-0x001F] + encrypted payload
- Requires SHA-256 implementation (~1KB code, or mbedtls on ESP32)
- Detects tampering, not just corruption

### Power-Loss Recovery

On `open()` of existing file:
1. Read file header, validate `magic == "ATS2"` and `headerCrc32`
2. If header CRC fails: try shadow header at offset 0x0A00
3. Scan from last known block (from `lastSeqNo` in header) forward
4. For each block: validate `blockSeqNo` is valid AND `payloadCrc32` matches
5. Truncate at first invalid block
6. Set `nextSeqNo = lastValidBlock.seqNo + 1`
7. Rebuild sparse index from valid block headers (unencrypted)
8. Log `recoveryTruncations` count for audit

---

## On-Device Query API

Query supports **three access patterns**: by channel ID, by schema name, or all channels.

### Query by Schema Name (Primary Usage)

```cpp
// Query by sensor type — resolves schema name → channelId internally
db.queryLatestBySchema("ADS1298_8CH", buf, 10);
db.queryBySchema("MPU6050", startEpoch, endEpoch, callback, ctx);
db.queryBySchema("DEVICE", startEpoch, endEpoch, callback, ctx);

// Or resolve once, reuse:
int8_t ch = db.findChannelBySchema("DHT11");  // returns channelId or -1
if (ch >= 0) db.queryByTime(ch, start, end, cb, ctx);
```

### Query by Channel ID (Low-Level)

```cpp
db.queryLatest(0, buf, 10);  // latest 10 records from channel 0
db.queryByTime(0, startEpoch, endEpoch, callback, ctx);
```

### Query All Channels (Cross-Channel View)

```cpp
// All sensor data + device status in time order
db.queryAllChannelsByTime(startEpoch, endEpoch, callback, ctx);
// Caller uses channelId in callback to select correct schema decoder:
bool onRecord(uint8_t ch, const uint8_t* rec, uint32_t ts, void* ctx) {
    const ArcanaTsSchema* schema = db.getSchema(ch);
    // decode rec using schema->fields, schema->recordSize
    return false;  // continue
}
```

### queryLatest(channelId, N) — Zero I/O

1. Lock mutex
2. If `channelId == primaryChannel`: copy from active primary buffer (plaintext RAM)
3. Else: scan slow buffer for matching tagged records
4. If need more: decrypt last flushed block(s) from read cache
5. Unlock mutex

### queryByTime(channelId, startEpoch, endEpoch, callback) — Binary Search

1. Binary search sparse index for first block where `channelId` matches AND `lastTimestamp >= startEpoch`
2. Iterate matching blocks forward until `firstTimestamp > endEpoch`
3. For each block: read, validate CRC-32, decrypt, iterate records
4. For multi-channel blocks (0xFF): parse tags, filter by channelId
5. Call `callback(channelId, record, timestamp, ctx)` per matching record
6. Callback returns `true` to stop early

### queryAllChannelsByTime — Cross-Channel

Iterates ALL blocks in time order. Single-channel blocks deliver with their channelId. Multi-channel blocks deliver each tagged record with its channelId. Useful for building a complete device snapshot for upload/display.

---

## Cross-Platform File Organization

```
Shared/
  Inc/
    ats/                           # Core engine (ZERO platform includes)
      ArcanaTsDb.hpp              # Multi-channel DB engine declaration
      ArcanaTsSchema.hpp          # Schema builder (header-only)
      ArcanaTsTypes.hpp           # Enums, structs, constants
      ICipher.hpp                 # Cipher interface
      IFilePort.hpp               # File I/O interface
      IMutex.hpp                  # Mutex interface
      Crc32.hpp                   # CRC-32 (header-only, IEEE 802.3)
    crypto/
      ChaCha20.hpp               # Moved from Services/common/ (pure C++)
  Src/
    ats/
      ArcanaTsDb.cpp             # Core engine implementation

Targets/stm32f103ze/
  Services/
    driver/
      FatFsFilePort.hpp/.cpp     # IFilePort -> FatFS
    common/
      FreeRtosMutex.hpp          # IMutex -> FreeRTOS (header-only)
      DeviceKey.hpp              # Stays (needs UID_BASE from CMSIS)
    service/
      AtsStorageService.hpp      # Service interface
    service/impl/
      AtsStorageServiceImpl.hpp/.cpp  # Wires PAL + channels + FreeRTOS task

Targets/esp32/ (future)
  port/
    VfsFilePort.hpp/.cpp         # IFilePort -> ESP-IDF VFS
    Esp32HwAesCipher.hpp         # ICipher -> mbedtls HW AES
    Esp32DeviceUid.hpp           # Device UID -> esp_efuse_mac_get_default()

tools/
  arcanats.py                    # Python reader (all platforms)
```

### Include Chain (No Platform Leakage)

```
ArcanaTsDb.hpp
  -> ArcanaTsTypes.hpp    (only <cstdint>, <cstddef>)
  -> ArcanaTsSchema.hpp   (only <cstdint>, <cstring>, Crc32.hpp)
  -> IFilePort.hpp        (only <cstdint>)
  -> ICipher.hpp          (only <cstdint>)
  -> IMutex.hpp           (only <cstdint>)
```

No `FreeRTOS.h`, no `ff.h`, no `stm32*.h` in any Shared/ file.

---

## Predefined Schemas

### Sensor Schemas

| Schema | Fields | recordSize | recordsPerBlock |
|---|---|---|---|
| ADS1298_8CH | ts(u32), ch0-ch7(i24) | 28 | 145 |
| MPU6050 | ts(u32), temp(f32), ax/ay/az(i16) | 14 | 290 |
| DHT11 | ts(u32), temp(i16), humidity(i16) | 8 | 508 |
| DEVICE_STATUS | ts(u32), vbat(u16), cpuTemp(i16), uptime(u32), freeHeap(u16), errFlags(u16) | 16 | 254 |
| GENERIC_ADC | ts(u32), value(i32) | 8 | 508 |
| PUMP | ts(u32), state(u8), runtime(u32) | 9 | 451 |

### Daily Operational Schemas (recorded in daily .ats)

| Schema | Fields | recordSize | recordsPerBlock |
|---|---|---|---|
| USER_ACTION | ts(u32), actionType(u8), actionCode(u16), param(u32) | 11 | 369 |
| ERROR_LOG | ts(u32), severity(u8), source(u8), errorCode(u16), param(u32) | 12 | 338 |
| CONFIG_SNAPSHOT | ts(u32), pressTarget(u16), flowRate(u16), timerSec(u16), threshHi(i16), threshLo(i16), mode(u8), flags(u8) | 14 | 290 |

### Device Lifecycle Schemas (for permanent device.ats)

| Schema | Fields | recordSize | recordsPerBlock |
|---|---|---|---|
| DEVICE_INFO | ts(u32), fw_major(u8), fw_minor(u8), fw_patch(u8), hw_rev(u8), serial_lo(u32), serial_hi(u32) | 16 | 254 |
| LIFECYCLE | ts(u32), eventType(u8), eventCode(u16), reserved(u8), param(u32) | 12 | 338 |
| COUNTERS | ts(u32), powerOnHrs(u32), pumpCycles(u32), errorCount(u32), sdWritesMB(u32) | 20 | 203 |
| CONFIG | ts(u32), pressTarget(u16), flowRate(u16), timerSec(u16), threshHi(i16), threshLo(i16), mode(u8), flags(u8) | 14 | 290 |
| CALIBRATION | ts(u32), sensorId(u8), pad(u8), gainNum(u16), offsetRaw(i32), zeroPoint(i32), tempCoeff(i16), calOperator(u8), pad2(u8) | 18 | 225 |

Custom schemas via `ArcanaTsSchema::addField()` — any combination up to 16 fields.
`FieldType::BYTES` (type=8) supports fixed-length byte arrays (e.g., 16 bytes for version strings).

**PUMP query pattern (累積型快照):**
- Firmware 維護 `totalRuntimeSec` counter，每 N 秒 append 一筆到 slow channel
- 查詢目前總時間：`queryLatestBySchema("PUMP", buf, 1)` → zero I/O, RAM 直讀
- 查詢歷史趨勢：`queryBySchema("PUMP", startEpoch, endEpoch, cb, ctx)`

---

## Daily .ats Black Box（每日黑盒子）

每天的 `.ats` 檔不只記錄感測器資料，同時包含操作行為、設定變更、錯誤記錄。上傳一個檔案 = 後台取得該日完整快照。

### Daily Channel Allocation

| Channel | Schema | Rate | Type | Description |
|---|---|---|---|---|
| 0 | ADS1298_8CH | 1kHz | Primary | 高速感測器（專用 double-buffer） |
| 1 | MPU6050 | 10Hz | Slow | 加速度/陀螺儀 |
| 2 | DHT11 | 0.1Hz | Slow | 溫溼度 |
| 3 | DEVICE_STATUS | 1/min | Slow | 電池/CPU 溫度/RAM/uptime |
| 4 | USER_ACTION | event | Slow | 使用者操作行為 |
| 5 | CONFIG_SNAPSHOT | event | Slow | 機器設定變更 |
| 6 | ERROR_LOG | event | Slow | 錯誤/警告記錄 |
| 7 | *(reserved)* | — | — | 未來擴充 |

Channels 4-6 全部走 slow buffer（tagged records），事件驅動寫入，RAM 零額外開銷。

### USER_ACTION Schema（使用者操作行為）

記錄操作者對機器的所有互動：按鈕、模式切換、參數調整、選單操作等。

| Field | Type | Size | Description |
|---|---|---|---|
| ts | U32 | 4 | epoch |
| actionType | U8 | 1 | 操作類別 |
| actionCode | U16 | 2 | 具體操作碼 |
| param | U32 | 4 | 操作參數 |
| **Total** | | **11** | 369 rec/block |

**Action Types:**
```cpp
enum UserActionType : uint8_t {
    BUTTON_PRESS  = 0x01,  // param = buttonId (KEY1, KEY2, ...)
    MODE_CHANGE   = 0x02,  // param = newMode
    PARAM_ADJUST  = 0x03,  // param = [paramId:16 | newValue:16]
    MENU_NAV      = 0x04,  // param = menuItemId
    COMMAND_RX    = 0x05,  // param = commandId (from UART/BLE)
    PUMP_TOGGLE   = 0x06,  // param = 0=off, 1=on
    ALARM_ACK     = 0x07,  // param = alarmCode (operator acknowledged alarm)
};
```

```cpp
// Firmware: 按鍵 callback 中記錄
UserActionRecord rec = { .ts=now(), .actionType=BUTTON_PRESS,
                         .actionCode=0x0001, .param=KEY1 };
db.append(4, (const uint8_t*)&rec);

// Backend: 分析操作頻率、使用模式、異常操作序列
db.queryBySchema("USER_ACTION", dayStart, dayEnd, onAction, ctx);
```

### ERROR_LOG Schema（錯誤記錄）

記錄所有警告、錯誤、故障事件，含模組來源與上下文參數。

| Field | Type | Size | Description |
|---|---|---|---|
| ts | U32 | 4 | epoch |
| severity | U8 | 1 | 嚴重等級 |
| source | U8 | 1 | 來源模組 |
| errorCode | U16 | 2 | 錯誤碼 |
| param | U32 | 4 | 錯誤上下文參數 |
| **Total** | | **12** | 338 rec/block |

**Severity / Source enums:**
```cpp
enum ErrorSeverity : uint8_t {
    INFO  = 0x00,  // 提示（SD mount ok, NTP sync ok）
    WARN  = 0x01,  // 警告（SD retry, sensor timeout, low battery）
    ERROR = 0x02,  // 錯誤（SD write fail, sensor CRC error）
    FATAL = 0x03,  // 致命（SD mount fail, watchdog, HardFault）
};

enum ErrorSource : uint8_t {
    SRC_SYSTEM  = 0x00,  // boot, watchdog, HardFault
    SRC_SDIO    = 0x01,  // SD card I/O
    SRC_SENSOR  = 0x02,  // sensor read/CRC
    SRC_WIFI    = 0x03,  // ESP8266 AT commands
    SRC_PUMP    = 0x04,  // pump driver
    SRC_CRYPTO  = 0x05,  // encryption errors
    SRC_TSDB    = 0x06,  // ArcanaTS internal errors
    SRC_NTP     = 0x07,  // NTP sync
};
```

```cpp
// Firmware: SD write 失敗時記錄
ErrorLogRecord err = { .ts=now(), .severity=ERROR, .source=SRC_SDIO,
                       .errorCode=0x0010, .param=HAL_SD_GetError(&hsd) };
db.append(6, (const uint8_t*)&err);

// 同時寫入 device.ats LIFECYCLE（永久記錄）
LifecycleRecord evt = { .ts=now(), .eventType=ERROR, .eventCode=0x0010,
                        .param=HAL_SD_GetError(&hsd) };
deviceDb.append(1, (const uint8_t*)&evt);
```

### CONFIG_SNAPSHOT Schema（機器設定快照）

操作者變更設定時寫入。與 device.ats 的 CONFIG 相同格式，但記錄在當天檔案以便上傳。

| Field | Type | Size | Description |
|---|---|---|---|
| ts | U32 | 4 | epoch |
| pressTarget | U16 | 2 | 目標壓力 |
| flowRate | U16 | 2 | 目標流量 |
| timerSec | U16 | 2 | 定時秒數 |
| threshHi | I16 | 2 | 高警報閾值 |
| threshLo | I16 | 2 | 低警報閾值 |
| mode | U8 | 1 | 運作模式 |
| flags | U8 | 1 | 功能旗標 |
| **Total** | | **14** | 290 rec/block |

```cpp
// Firmware: 操作者修改設定 → 同時寫入 daily + device.ats
ConfigRecord cfg = { .ts=now(), .pressTarget=1200, .flowRate=500, ... };
db.append(5, (const uint8_t*)&cfg);       // daily .ats → 上傳後台
deviceDb.append(3, (const uint8_t*)&cfg);  // device.ats → 開機載入
```

### Daily vs Device.ats 資料分工

| 資料類型 | Daily .ats | Device.ats | 說明 |
|---|---|---|---|
| 感測器資料 | **寫入** | — | 高頻，按日輪替 |
| 使用者操作 | **寫入** | — | 按日記錄，上傳分析 |
| 錯誤記錄 | **寫入** | 嚴重者寫 LIFECYCLE | daily 完整記錄；device 只保留重大事件 |
| 設定變更 | **寫入** | **寫入** CONFIG | 雙寫：daily 供上傳，device 供開機載入 |
| 裝置身份 | — | **寫入** DEVICE_INFO | 永久，firmware 更新時寫 |
| 累積計數器 | — | **寫入** COUNTERS | 永久，跨日累計 |
| 校正參數 | — | **寫入** CALIBRATION | 永久，開機載入 |

### Backend 收到的完整資料

後台每天收到一個 `.ats` 檔，包含：
1. **感測器原始資料** — 完整波形、環境數據
2. **操作行為軌跡** — 何時按了什麼按鈕、切換了什麼模式
3. **設定變更歷程** — 該日所有參數調整（附 before/after 時間戳）
4. **錯誤與警告日誌** — 發生了什麼異常、嚴重程度、來源模組

Python reader 自動解析所有 channel，後台可以：
- 關聯分析：錯誤發生前操作者做了什麼？
- 使用模式統計：最常用的模式、參數範圍
- 預測維護：錯誤頻率趨勢、感測器漂移
- 合規審計：完整操作 + 資料記錄鏈

---

## Performance Projections

| Metric | FlashDB (current) | ArcanaTS v2 |
|---|---|---|
| Write rate | 5-7 rec/s | **1000+ rec/s** (primary channel) |
| `append()` latency | ~2ms | **~0.7us** (memcpy) |
| SD I/O per record | ~5 operations | **1/145** of one 4KB write |
| Encryption overhead | 12 bytes/record | **12 bytes/block** (98% less) |
| Init time | 50ms+ | **~10ms** (read 1 block header) |
| Multi-sensor support | Separate KVDBs | **Single file, multi-channel** |
| On-device query | Linear scan | **Binary search sparse index** |
| Latest N query | Decrypt + scan | **RAM direct access** |
| Upload | Multiple files | **Single .ats file** |

---

## Device Lifecycle Database (device.ats)

Permanent (non-rotating) ArcanaTS instance for storing device identity, configuration, accumulated counters, and lifecycle events across the entire device lifetime.

### Design

```cpp
// Opened at boot, never rotated, closed on shutdown
static ArcanaTsDb deviceDb;
static uint8_t devSlowBuf[4096];  // no primary double-buffer needed
AtsConfig devCfg = {
    .file = &devFile, .cipher = &chacha, .mutex = &rtosMutex,
    .getTime = SystemClock::now, .key = deviceKey,
    .primaryChannel = 0xFF,  // no primary → all channels use slowBuf
    .slowBuf = devSlowBuf,
    ...
};

deviceDb.open("device.ats", devCfg);
deviceDb.addChannel(0, ArcanaTsSchema::deviceInfo());      // FW/HW identity
deviceDb.addChannel(1, ArcanaTsSchema::lifecycleEvent());   // events
deviceDb.addChannel(2, ArcanaTsSchema::deviceCounters());   // accumulated stats
deviceDb.addChannel(3, ArcanaTsSchema::config());           // operational parameters
deviceDb.addChannel(4, ArcanaTsSchema::calibration());      // sensor calibration values
deviceDb.start();
```

### Channel 0: DEVICE_INFO (identity snapshots)

Written on: firmware update, hardware revision change, factory reset.

| Field | Type | Description |
|---|---|---|
| ts | U32 | epoch |
| fw_major | U8 | firmware major version |
| fw_minor | U8 | firmware minor version |
| fw_patch | U8 | firmware patch version |
| hw_rev | U8 | hardware revision |
| serial_lo | U32 | serial number low 32 bits |
| serial_hi | U32 | serial number high 32 bits |

```cpp
// Query current firmware version → zero I/O
deviceDb.queryLatestBySchema("DEVICE_INFO", buf, 1);
DeviceInfoRecord* info = (DeviceInfoRecord*)buf;
// → "FW v2.1.3, HW rev 4"
```

### Channel 1: LIFECYCLE (event log)

Written on: power on/off, calibration, error, hardware change, config change.

| Field | Type | Description |
|---|---|---|
| ts | U32 | epoch |
| eventType | U8 | category (see enum below) |
| eventCode | U16 | specific event code |
| reserved | U8 | alignment padding |
| param | U32 | event-specific parameter |

**Event Types:**
```cpp
enum LifecycleEventType : uint8_t {
    POWER_ON  = 0x01,  // param = reset reason (RCC_CSR bits)
    POWER_OFF = 0x02,  // param = uptime seconds
    FW_UPDATE = 0x10,  // param = old version packed
    HW_CHANGE = 0x11,  // param = component ID
    CALIBRATE = 0x20,  // param = calibration type
    ERROR     = 0x30,  // param = error code
    CONFIG    = 0x40,  // param = config key hash
    MAINTAIN  = 0x50,  // param = maintenance type (pump replace, filter change)
};
```

```cpp
// Query all lifecycle events
deviceDb.queryBySchema("LIFECYCLE", 0, now(), onEvent, ctx);

// Query events in last 30 days
deviceDb.queryBySchema("LIFECYCLE", now()-30*86400, now(), onEvent, ctx);
```

### Channel 2: COUNTERS (accumulated stats)

Written periodically (every hour) and on shutdown.

| Field | Type | Description |
|---|---|---|
| ts | U32 | epoch |
| powerOnHrs | U32 | total power-on hours |
| pumpCycles | U32 | total pump on/off cycles |
| errorCount | U32 | cumulative error count |
| sdWritesMB | U32 | total SD card writes in MB |

```cpp
// Query current accumulated counters → zero I/O
deviceDb.queryLatestBySchema("COUNTERS", buf, 1);
CountersRecord* c = (CountersRecord*)buf;
// → "Power: 12345 hrs, Pump: 5678 cycles, Errors: 42"
```

### Channel 3: CONFIG (operational parameters)

Written on: operator adjusts settings via UI/command.
Read at boot → apply to machine operation. `queryLatest` = current config.

| Field | Type | Description |
|---|---|---|
| ts | U32 | epoch |
| pressTarget | U16 | target pressure (unit depends on app) |
| flowRate | U16 | target flow rate |
| timerSec | U16 | operation timer seconds |
| threshHi | I16 | high alarm threshold |
| threshLo | I16 | low alarm threshold |
| mode | U8 | operation mode |
| flags | U8 | feature flags |

```cpp
// Boot → load operational parameters
uint8_t cfgBuf[14];
if (deviceDb.queryLatestBySchema("CONFIG", cfgBuf, 1) > 0) {
    ConfigRecord* p = (ConfigRecord*)cfgBuf;
    pump.setPressureTarget(p->pressTarget);
    pump.setFlowRate(p->flowRate);
} else {
    applyFactoryDefaults();  // first boot
}

// Operator changes setting → append new config snapshot
ConfigRecord newCfg = { .ts=now(), .pressTarget=1200, ... };
deviceDb.append(3, (const uint8_t*)&newCfg);
deviceDb.flush();
```

### Channel 4: CALIBRATION (sensor calibration values)

Written on: calibration procedure completes.
Read at boot → apply corrections to each sensor. Full calibration history preserved for audit.

| Field | Type | Description |
|---|---|---|
| ts | U32 | epoch |
| sensorId | U8 | which sensor (0=pressure, 1=temp, ...) |
| pad | U8 | alignment |
| gainNum | U16 | gain numerator (e.g. 1024) |
| offsetRaw | I32 | raw offset correction |
| zeroPoint | I32 | zero-point reading |
| tempCoeff | I16 | temperature coefficient |
| calOperator | U8 | who performed calibration |
| pad2 | U8 | alignment |

```cpp
// Boot → load calibration for all sensors
uint8_t calBuf[18 * 4];  // up to 4 sensors
uint16_t n = deviceDb.queryLatest(4, calBuf, 4);
for (uint16_t i = 0; i < n; i++) {
    CalibrationRecord* cal = (CalibrationRecord*)(calBuf + i * 18);
    sensors[cal->sensorId].applyCalibration(cal->offsetRaw,
                                             cal->gainNum, cal->zeroPoint);
}

// Calibration procedure → write new values
CalibrationRecord cal = { .ts=now(), .sensorId=0, .offsetRaw=measuredOffset, ... };
deviceDb.append(4, (const uint8_t*)&cal);
deviceDb.flush();  // 校正值不能丟

// Rollback to previous calibration
uint8_t buf[18 * 2];
deviceDb.queryLatest(4, buf, 2);  // latest 2 records
CalibrationRecord* prev = (CalibrationRecord*)(buf + 18);  // 2nd = previous
sensors[prev->sensorId].applyCalibration(prev->offsetRaw, ...);
```

### Dual-Instance Service Architecture

```
┌───────────────────────────────────────────────────────────┐
│ AtsStorageServiceImpl                                     │
│                                                           │
│  ┌─────────────┐  ┌───────────────────────────────────┐   │
│  │ deviceDb     │  │ sensorDb (daily black box)        │   │
│  │ (permanent)  │  │ Ch0-3: sensor data                │   │
│  │ 5 channels   │  │ Ch4: USER_ACTION (操作行為)        │   │
│  │ slowBuf only │  │ Ch5: CONFIG_SNAPSHOT (設定變更)    │   │
│  │ 4KB RAM      │  │ Ch6: ERROR_LOG (錯誤記錄)         │   │
│  └──────┬───────┘  │ primary+slow buf, 12KB+ RAM       │   │
│         │          └──────────────┬────────────────────┘   │
│         │                        │                         │
│    嚴重錯誤/設定 ──── 雙寫 ────→ daily + device.ats       │
│                                                           │
│  Flush task handles both instances                        │
│  deviceDb: flush on event + hourly timer                  │
│  sensorDb: flush on buffer full + 60s                     │
└───────────────────────────────────────────────────────────┘
```

**雙寫策略**: CONFIG 變更和嚴重 ERROR 同時寫入 daily .ats（上傳用）和 device.ats（永久保存 + 開機載入）。其餘操作行為和一般錯誤只寫 daily .ats。

**RAM impact:** device.db adds only the `ArcanaTsDb` state (~200B) + index entries — **slow buffer can be shared** with sensorDb (mutually exclusive flush, both in same task). Operational channels (4-6) use the same slow buffer as sensor slow channels — zero additional RAM.

### device.ats vs daily .ats comparison

| | Daily .ats (黑盒子) | Device.ats (permanent) |
|---|---|---|
| Rotation | Midnight daily | Never |
| Channels | 7 (sensors + ops) | 5 (identity + lifecycle) |
| Write rate | High (1kHz+ sensor + events) | Very low (~hourly) |
| File size | MB/day | KB/years |
| Primary buffer | Double-buffer 8KB | Not needed (0xFF) |
| Slow buffer | Shared 4KB | Shared with daily DB |
| Contains | 感測器 + 操作 + 設定 + 錯誤 | 身份 + 壽命計數 + 校正 |
| Upload | **每日自動上傳** | On request / maintenance |
| Python reader | Same arcanats.py | Same arcanats.py |

### Flush Strategy for device.db

- **Event-driven flush**: after writing LIFECYCLE events (critical — don't lose power-on/error events)
- **Periodic flush**: every hour for COUNTERS
- **Shutdown flush**: `deviceDb.close()` on graceful shutdown
- **Shared slow buffer**: when sensorDb flush is idle, device.db can use the same buffer
  - Or dedicate a small 4KB buffer if RAM allows

---

## Python Reader

```
tools/arcanats.py

Usage:
  python arcanats.py info data.ats                              # show header, all channels, stats
  python arcanats.py read data.ats --key KEY                    # dump ALL channels as CSV
  python arcanats.py read data.ats --schema ADS1298_8CH --key KEY   # filter by schema name
  python arcanats.py read data.ats --schema MPU6050 --format json
  python arcanats.py read data.ats --channel 0 --start 2026-03-13 --end 2026-03-14
  python arcanats.py schemas data.ats                           # list all schemas with field details
  python arcanats.py create test.ats --key KEY --uid DEVICE_UID  # create .ats with injected records (testing)
```

Reader flow:
1. Parse global header: magic, version, cipherType, channelCount
2. Parse channel descriptors + field tables → build per-channel schema decoders
3. Select cipher: 0→None, 1→ChaCha20 (PyCryptodome), 2→AES-256-CTR
4. Iterate data blocks: read 32-byte header, check channelId, decrypt payload
5. For multi-channel blocks (0xFF): parse tagged records, dispatch to correct schema decoder
6. Filter by `--schema NAME` or `--channel ID` if specified
7. Decode fields using per-channel schema, output as CSV/JSON

The self-describing format means the Python reader needs NO prior knowledge of sensor types — all field names, types, sizes, and scales are in the file header.

---

## Implementation Phases

### Phase 1: PAL Interfaces + Types (Shared only)
- `Shared/Inc/ats/ArcanaTsTypes.hpp` — enums, structs, constants
- `Shared/Inc/ats/IFilePort.hpp`
- `Shared/Inc/ats/IMutex.hpp`
- `Shared/Inc/ats/ICipher.hpp`
- `Shared/Inc/ats/Crc32.hpp` — header-only, IEEE 802.3 polynomial
- `Shared/Inc/ats/ArcanaTsSchema.hpp` — schema builder + predefined schemas
- Move `ChaCha20.hpp` → `Shared/Inc/crypto/ChaCha20.hpp`

### Phase 2: Core Engine
- `Shared/Inc/ats/ArcanaTsDb.hpp` — class declaration
- `Shared/Src/ats/ArcanaTsDb.cpp` — multi-channel append, flush, recovery, query

### Phase 3: STM32 Platform Port
- `FatFsFilePort.hpp/.cpp` — IFilePort for FatFS (retry logic from SdFalAdapter)
- `FreeRtosMutex.hpp` — IMutex for FreeRTOS (header-only)
- Build system: new `subdir.mk`, update `makefile`, `sources.mk`, `objects.list`

### Phase 4: Service Layer + Integration
- `AtsStorageService.hpp` — service interface
- `AtsStorageServiceImpl.hpp/.cpp` — dual-instance management:
  - `sensorDb`: daily rotation, primary + slow channels, observer callbacks
  - `deviceDb`: permanent, lifecycle events + counters, event-driven flush
  - Single FreeRTOS flush task handles both instances
  - Shared slow buffer (mutually exclusive flush)
- Controller rewiring: replace SdStorageService with AtsStorageService
- Remove FlashDB + SdFalAdapter
- Boot sequence: open device.ats → load CONFIG + CALIBRATION → write POWER_ON event → open today's sensor .ats
- Shutdown: flush both → close both

### Phase 5: Python Reader
- `tools/arcanats.py` — multi-channel aware, per-channel schema decoding

### Phase 6: Validation
- Stress test: 1000 SPS primary + 10 SPS slow channels simultaneously
- Power-loss test: pull power during writes, verify recovery + no data loss
- Multi-channel round-trip: STM32 writes all channels → Python reads → verify
- LCD live stats from queryLatest()
- **device.ats 抽換測試** — 用 Python 預製不同情境的 device.ats，驗證 firmware 行為：
  - 空白 device.ats → 驗證首次開機初始化
  - powerOnHrs=50000, pumpCycles=100000 → 驗證壽命警告 / 維護提醒
  - 含多筆 ERROR lifecycle events → 驗證錯誤累積處理 / 安全鎖定
  - DEVICE_INFO fw=v1.0.0 → 驗證韌體升級流程 / 版本遷移
  - 含 MAINTAIN 事件 → 驗證維修後重置邏輯
- **arcanats.py create 子命令** — 用於產生測試用 device.ats：
  ```
  python arcanats.py create device_test.ats --key KEY \
    --channel 0 --schema COUNTERS --records '[{"ts":...,"powerOnHrs":50000,...}]' \
    --channel 1 --schema LIFECYCLE --records '[{"ts":...,"eventType":0x30,...}]'
  ```

### Phase 7 (Future): ESP32 Port
- VfsFilePort, Esp32HwAesCipher, Esp32DeviceUid
- ESP-IDF component integration

---

## Risks

| Risk | Severity | Mitigation |
|---|---|---|
| RAM tight on F103 (~14.3KB of 17KB) | Medium | Share read cache with flush buffer. Can reduce index to 42 entries (save 680B) |
| Multi-channel block parsing complexity | Low | Only slow channels use tagged format. Primary channel blocks are simple fixed-size |
| Slow buffer 60s timer → max 60s data loss for slow channels | Medium | Configurable. Medical can set 5s. Or flush on primary flush piggyback |
| ChaCha20.hpp relocation breaks DeviceKey.hpp include | Low | Update one `#include` path |
| CRC-32 table (1KB ROM) | Low | Use bitwise (like Crc16.hpp). Slower but zero ROM |
| ArcanaTsDb.cpp too large for F103 flash | Low | Current text=108KB of 384KB. Core engine ~3-5KB code |

---

## Key Reference Files

| Purpose | Path |
|---|---|
| CRC-16 pattern (follow for CRC-32) | `Shared/Inc/Crc16.hpp` |
| ChaCha20 (relocate to Shared) | `Targets/stm32f103ze/Services/common/ChaCha20.hpp` |
| DeviceKey (UID pattern) | `Targets/stm32f103ze/Services/common/DeviceKey.hpp` |
| FatFS I/O patterns | `Targets/stm32f103ze/Services/driver/SdFalAdapter.cpp` |
| Service wiring pattern | `Targets/stm32f103ze/Services/service/impl/SdStorageServiceImpl.cpp` |
| Shared build subdir.mk | `Targets/stm32f103ze/Debug/Shared/Src/subdir.mk` |
| Observable pattern | `Shared/Inc/Observable.hpp` |
