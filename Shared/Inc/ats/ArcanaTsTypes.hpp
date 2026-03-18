/**
 * @file ArcanaTsTypes.hpp
 * @brief ArcanaTS v2 type definitions, constants, and on-disk structures
 *
 * All packed structs match the .ats binary file format exactly.
 * ZERO platform dependencies — only <cstdint> and <cstddef>.
 */

#ifndef ARCANA_ATS_TYPES_HPP
#define ARCANA_ATS_TYPES_HPP

#include <cstdint>
#include <cstddef>

namespace arcana {
namespace ats {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const uint16_t BLOCK_SIZE         = 4096;
static const uint16_t BLOCK_HEADER_SIZE  = 32;
static const uint16_t BLOCK_PAYLOAD_SIZE = BLOCK_SIZE - BLOCK_HEADER_SIZE;  // 4064
static const uint8_t  MAX_CHANNELS       = 8;
static const uint8_t  MULTI_CHANNEL_ID   = 0xFF;

// ---------------------------------------------------------------------------
// File mode bitmasks (for IFilePort::open)
// ---------------------------------------------------------------------------

static const uint8_t ATS_MODE_READ   = 0x01;
static const uint8_t ATS_MODE_WRITE  = 0x02;
static const uint8_t ATS_MODE_RW     = 0x03;
static const uint8_t ATS_MODE_CREATE = 0x10;

// ---------------------------------------------------------------------------
// File header flag bitmasks
// ---------------------------------------------------------------------------

static const uint16_t ATS_FLAG_ENCRYPTED  = 0x0001;  // bit 0
static const uint16_t ATS_FLAG_HAS_INDEX  = 0x0002;  // bit 1
static const uint16_t ATS_FLAG_HAS_HMAC   = 0x0004;  // bit 2
static const uint16_t ATS_FLAG_HAS_SHADOW = 0x0008;  // bit 3

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

enum class OverflowPolicy : uint8_t {
    Block = 0,   // Medical: backpressure, zero data loss
    Drop  = 1,   // IoT: drop records if buffer full
};

enum class FieldType : uint8_t {
    U8    = 0,
    U16   = 1,
    U32   = 2,
    I16   = 3,
    I32   = 4,
    F32   = 5,
    I24   = 6,
    U64   = 7,
    BYTES = 8,   // Fixed-length byte array (size in scaleNum)
};

// ---------------------------------------------------------------------------
// Function pointer types
// ---------------------------------------------------------------------------

/** @brief Time source — returns UTC epoch seconds */
using AtsGetTimeFn = uint32_t (*)();

/** @brief Record callback for query iteration
 *  @return true to stop iteration early */
using RecordCallback = bool (*)(uint8_t channelId, const uint8_t* record,
                                uint32_t timestamp, void* ctx);

// ---------------------------------------------------------------------------
// Forward declarations for PAL interfaces (used in AtsConfig)
// ---------------------------------------------------------------------------

class IFilePort;
class ICipher;
class IMutex;

// ---------------------------------------------------------------------------
// On-disk packed structures
// ---------------------------------------------------------------------------

/** @brief Global file header (64 bytes, at offset 0x0000 in block 0) */
struct __attribute__((packed)) AtsFileHeader {
    uint8_t  magic[4];          // "ATS2"
    uint8_t  version;           // 2
    uint8_t  headerBlocks;      // 1
    uint16_t flags;             // ATS_FLAG_* bitmask
    uint8_t  cipherType;        // 0=none, 1=ChaCha20, 2=AES-256-CTR
    uint8_t  channelCount;      // 1-8
    uint8_t  overflowPolicy;    // OverflowPolicy
    uint8_t  deviceUidSize;     // 6 (ESP32) or 12 (STM32)
    uint32_t createdEpoch;      // UTC epoch when file created
    uint8_t  deviceUid[16];     // zero-padded to 16 bytes
    uint32_t totalBlockCount;   // total data blocks written
    uint32_t lastSeqNo;         // last committed block sequence number
    uint32_t indexBlockOffset;  // block# of sparse index (0=none)
    uint32_t headerCrc32;       // CRC-32 of bytes 0x0000-0x002B
    uint8_t  reserved[16];
};
static_assert(sizeof(AtsFileHeader) == 64, "AtsFileHeader must be 64 bytes");

/** @brief Channel descriptor (32 bytes each, 8 slots at offset 0x0040) */
struct __attribute__((packed)) AtsChannelDescriptor {
    uint8_t  channelId;         // 0-7, or 0xFF=unused
    uint8_t  fieldCount;        // 1-16
    uint16_t recordSize;        // bytes per plaintext record
    uint16_t sampleRateHz;      // nominal, 0=variable
    uint16_t recordCount;       // low 16 bits (high bits in stats)
    char     name[24];          // null-terminated schema name
};
static_assert(sizeof(AtsChannelDescriptor) == 32, "AtsChannelDescriptor must be 32 bytes");

/** @brief Data block header (32 bytes, at start of each 4KB block) */
struct __attribute__((packed)) AtsBlockHeader {
    uint32_t blockSeqNo;        // global monotonic sequence (written LAST)
    uint8_t  channelId;         // 0-7, or 0xFF=multi-channel
    uint8_t  flags;             // bit0=partial
    uint16_t recordCount;       // records in this block
    uint32_t firstTimestamp;    // epoch of first record
    uint32_t lastTimestamp;     // epoch of last record
    uint8_t  nonce[12];         // [seqNo:4LE][createdEpoch:4LE][0x00:4]
    uint32_t payloadCrc32;      // CRC-32 of encrypted payload
};
static_assert(sizeof(AtsBlockHeader) == 32, "AtsBlockHeader must be 32 bytes");

/** @brief Index block header (16 bytes, at start of index area) */
struct __attribute__((packed)) AtsIndexHeader {
    uint8_t  magic[4];          // "IDX2"
    uint32_t entryCount;
    uint32_t crc32;             // CRC-32 of entries
    uint8_t  reserved[4];
};
static_assert(sizeof(AtsIndexHeader) == 16, "AtsIndexHeader must be 16 bytes");

/** @brief Sparse index entry (16 bytes) */
struct __attribute__((packed)) AtsIndexEntry {
    uint32_t blockNumber;
    uint8_t  channelId;
    uint8_t  flags;
    uint16_t recordCount;
    uint32_t firstTimestamp;
    uint32_t lastTimestamp;
};
static_assert(sizeof(AtsIndexEntry) == 16, "AtsIndexEntry must be 16 bytes");

// ---------------------------------------------------------------------------
// Runtime structures
// ---------------------------------------------------------------------------

/** @brief Aggregate storage statistics */
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

/** @brief Configuration for opening an ArcanaTS database */
struct AtsConfig {
    IFilePort*      file;
    ICipher*        cipher;
    IMutex*         mutex;
    AtsGetTimeFn    getTime;
    const uint8_t*  key;              // 32-byte encryption key
    const uint8_t*  deviceUid;        // device UID bytes
    uint8_t         deviceUidSize;    // 6 (ESP32) or 12 (STM32)
    OverflowPolicy  overflow;         // Block (medical) or Drop (IoT)
    uint8_t         primaryChannel;   // channel ID for dedicated double-buffer (0xFF = none)
    uint8_t*        primaryBufA;      // 4KB, required if primaryChannel != 0xFF
    uint8_t*        primaryBufB;      // 4KB, required if primaryChannel != 0xFF
    uint8_t*        slowBuf;          // 4KB, for all non-primary channels
    uint8_t*        readCache;        // 4KB, optional (nullptr = share with slowBuf)
};

// ---------------------------------------------------------------------------
// Application-level enums (for predefined schemas)
// ---------------------------------------------------------------------------

enum class UserActionType : uint8_t {
    ButtonPress = 0x01,
    ModeChange  = 0x02,
    ParamAdjust = 0x03,
    MenuNav     = 0x04,
    CommandRx   = 0x05,
    PumpToggle  = 0x06,
    AlarmAck    = 0x07,
};

enum class ErrorSeverity : uint8_t {
    Info  = 0x00,
    Warn  = 0x01,
    Error = 0x02,
    Fatal = 0x03,
};

enum class ErrorSource : uint8_t {
    System = 0x00,
    Sdio   = 0x01,
    Sensor = 0x02,
    Wifi   = 0x03,
    Pump   = 0x04,
    Crypto = 0x05,
    Tsdb   = 0x06,
    Ntp    = 0x07,
};

enum class LifecycleEventType : uint8_t {
    PowerOn  = 0x01,
    PowerOff = 0x02,
    Recovery = 0x03,  // power-loss recovery completed
    FwUpdate = 0x10,
    HwChange = 0x11,
    Calibrate = 0x20,
    Error    = 0x30,
    Config   = 0x40,
    Maintain = 0x50,
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_ATS_TYPES_HPP */
