/**
 * @file ArcanaTsDb.cpp
 * @brief ArcanaTS v2 core engine implementation
 *
 * Multi-channel append, buffered block I/O, atomic commit,
 * power-loss recovery, sparse index, on-device query.
 *
 * ZERO platform dependencies — all via PAL interfaces.
 */

#include "ArcanaTsDb.hpp"
#include "Crc32.hpp"
#include <cstring>

namespace arcana {
namespace ats {

// ---------------------------------------------------------------------------
// File layout constants
// ---------------------------------------------------------------------------

static const uint64_t HEADER_BLOCK_OFFSET    = 0;
static const uint64_t GLOBAL_HEADER_OFFSET   = 0x0000;
static const uint64_t CHANNEL_DESC_OFFSET    = 0x0040;  // 8 x 32 bytes
static const uint64_t FIELD_TABLE_OFFSET     = 0x0140;  // 8 x 256 bytes
static const uint64_t STATS_OFFSET           = 0x0940;  // 128 bytes
static const uint64_t SHADOW_OFFSET          = 0x0A00;  // shadow copy of 0x0000-0x09FF
static const uint64_t DATA_START_OFFSET      = BLOCK_SIZE;  // first data block at 4096

static const uint8_t  ATS_MAGIC[4] = { 'A', 'T', 'S', '2' };
static const uint8_t  IDX_MAGIC[4] = { 'I', 'D', 'X', '2' };

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint32_t computeIeeeCrc32(const uint8_t* data, size_t len) {
    return ~crc32(0xFFFFFFFF, data, len);
}

static bool strEq(const char* a, const char* b, size_t maxLen) {
    for (size_t i = 0; i < maxLen; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ArcanaTsDb::ArcanaTsDb()
    : mOpen(false)
    , mReadOnly(false)
    , mStarted(false)
    , mChannelCount(0)
    , mNextSeqNo(1)
    , mCreatedEpoch(0)
    , mNextBlockOffset(DATA_START_OFFSET)
    , mHeaderBase(0)
    , mIndexCount(0)
{
    memset(&mCfg, 0, sizeof(mCfg));
    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        mChannels[i] = ChannelState();
    }
    memset(&mPrimary, 0, sizeof(mPrimary));
    memset(&mSlow, 0, sizeof(mSlow));
    memset(&mStats, 0, sizeof(mStats));
    memset(mIndex, 0, sizeof(mIndex));
    memset(mHeaderNonce, 0, sizeof(mHeaderNonce));
}

// ---------------------------------------------------------------------------
// Lifecycle: open
// ---------------------------------------------------------------------------

bool ArcanaTsDb::open(const char* path, const AtsConfig& cfg) {
    if (mOpen) return false;
    if (!cfg.file || !cfg.mutex || !cfg.getTime) return false;

    mCfg = cfg;
    mReadOnly = false;

    // Try to open existing file first
    if (cfg.file->open(path, ATS_MODE_RW)) {
        // Existing file — attempt recovery/resume
        if (cfg.file->size() >= BLOCK_SIZE && recoverFromExisting()) {
            mOpen = true;
            mStarted = true;  // channels already registered from file header
            return true;
        }
        // Recovery failed or empty file — close and recreate
        cfg.file->close();
    }

    // Create new file
    if (!cfg.file->open(path, ATS_MODE_RW | ATS_MODE_CREATE)) {
        return false;
    }

    mCreatedEpoch = cfg.getTime();
    mNextSeqNo = 1;
    mNextBlockOffset = DATA_START_OFFSET;
    mChannelCount = 0;
    mIndexCount = 0;
    memset(&mStats, 0, sizeof(mStats));

    // Setup primary double-buffer
    if (cfg.primaryChannel != 0xFF && cfg.primaryBufA && cfg.primaryBufB) {
        mPrimary.bufA = cfg.primaryBufA;
        mPrimary.bufB = cfg.primaryBufB;
        mPrimary.writeOffset = 0;
        mPrimary.recordCount = 0;
        mPrimary.firstTimestamp = 0;
        mPrimary.lastTimestamp = 0;
        mPrimary.flushPending = false;
    }

    // Setup slow buffer
    if (cfg.slowBuf) {
        mSlow.buf = cfg.slowBuf;
        mSlow.writeOffset = 0;
        mSlow.recordCount = 0;
        mSlow.firstTimestamp = 0;
        mSlow.lastTimestamp = 0;
        mSlow.flushPending = false;
    }

    mOpen = true;
    mStarted = false;  // caller must call addChannel() then start()
    return true;
}

bool ArcanaTsDb::openReadOnly(const char* path, const AtsConfig& cfg) {
    if (mOpen) return false;
    if (!cfg.file || !cfg.mutex) return false;

    mCfg = cfg;
    mReadOnly = true;

    if (!cfg.file->open(path, ATS_MODE_READ)) {
        return false;
    }

    if (cfg.file->size() < BLOCK_SIZE) {
        cfg.file->close();
        return false;
    }

    if (!readEntireHeaderBlock()) {
        cfg.file->close();
        return false;
    }

    // Read sparse index if present
    readIndex();

    // If no persisted index, scan block headers to build one
    if (mIndexCount == 0) {
        uint64_t offset = DATA_START_OFFSET;
        uint64_t fileSize = cfg.file->size();
        while (offset + BLOCK_SIZE <= fileSize && mIndexCount < MAX_INDEX_ENTRIES) {
            AtsBlockHeader hdr;
            if (!validateBlock(offset / BLOCK_SIZE, hdr)) break;
            addIndexEntry(offset / BLOCK_SIZE, hdr.channelId, hdr.recordCount,
                          hdr.firstTimestamp, hdr.lastTimestamp);
            offset += BLOCK_SIZE;
        }
    }

    mOpen = true;
    mStarted = true;
    return true;
}

// ---------------------------------------------------------------------------
// Lifecycle: addChannel
// ---------------------------------------------------------------------------

bool ArcanaTsDb::addChannel(uint8_t channelId, const ArcanaTsSchema& schema,
                             uint16_t sampleRateHz) {
    if (!mOpen || mStarted || mReadOnly) return false;
    if (channelId >= MAX_CHANNELS) return false;
    if (mChannels[channelId].active) return false;
    if (schema.recordSize == 0 || schema.fieldCount == 0) return false;

    mChannels[channelId].schema = schema;
    mChannels[channelId].sampleRateHz = sampleRateHz;
    mChannels[channelId].active = true;
    mChannelCount++;
    return true;
}

// ---------------------------------------------------------------------------
// Lifecycle: addChannelLive (schema upgrade for OTA)
// ---------------------------------------------------------------------------

bool ArcanaTsDb::addChannelLive(uint8_t channelId, const ArcanaTsSchema& schema,
                                 uint16_t sampleRateHz) {
    // Must be open AND started (existing DB), not read-only
    if (!mOpen || !mStarted || mReadOnly) return false;
    if (channelId >= MAX_CHANNELS) return false;
    if (mChannels[channelId].active) return true;  // already exists — OK
    if (schema.recordSize == 0 || schema.fieldCount == 0) return false;

    mChannels[channelId].schema = schema;
    mChannels[channelId].sampleRateHz = sampleRateHz;
    mChannels[channelId].active = true;
    mChannelCount++;

    // Rewrite header to persist the new channel descriptor + field table
    bool ok;
    if (mCfg.headerKey) {
        ok = writeEntireHeaderBlock();
    } else {
        ok = writeFileHeader() && writeChannelDescriptors() && writeShadowHeader();
    }
    if (ok) mCfg.file->sync();
    return ok;
}

// ---------------------------------------------------------------------------
// Lifecycle: start
// ---------------------------------------------------------------------------

bool ArcanaTsDb::start() {
    if (!mOpen || mStarted || mReadOnly) return false;
    if (mChannelCount == 0) return false;

    mHeaderBase = mCfg.headerKey ? 16 : 0;

    if (mCfg.headerKey) {
        // Encrypted header: build entire block in RAM, encrypt, write
        if (!writeEntireHeaderBlock()) return false;
    } else {
        // Legacy plaintext: write zeroed block, fill sections individually
        uint8_t* headerBuf = getReadCache();
        if (!headerBuf) return false;

        memset(headerBuf, 0, BLOCK_SIZE);
        if (!mCfg.file->seek(0)) return false;
        if (mCfg.file->write(headerBuf, BLOCK_SIZE) != BLOCK_SIZE) return false;

        if (!writeFileHeader()) return false;
        if (!writeChannelDescriptors()) return false;
        if (!writeShadowHeader()) return false;
    }

    mCfg.file->sync();

    mStarted = true;
    return true;
}

// ---------------------------------------------------------------------------
// Lifecycle: close
// ---------------------------------------------------------------------------

bool ArcanaTsDb::close() {
    if (!mOpen) return false;

    if (!mReadOnly && mStarted) {
        // Flush remaining data
        flush();
        // Write sparse index
        writeIndex();
        // Update header with final stats
        if (mCfg.headerKey) {
            writeEntireHeaderBlock();
        } else {
            updateFileHeader();
            writeShadowHeader();
        }
        mCfg.file->sync();
    }

    mCfg.file->close();
    mOpen = false;
    mStarted = false;
    mReadOnly = false;

    // Reset channel state so addChannel() works after close() + open()
    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        mChannels[i] = ChannelState();
    }
    mChannelCount = 0;
    mNextSeqNo = 1;
    mNextBlockOffset = DATA_START_OFFSET;
    mHeaderBase = 0;
    mIndexCount = 0;
    memset(&mStats, 0, sizeof(mStats));
    memset(&mPrimary, 0, sizeof(mPrimary));
    memset(&mSlow, 0, sizeof(mSlow));
    memset(mIndex, 0, sizeof(mIndex));

    return true;
}

// ---------------------------------------------------------------------------
// Write: append (hot path)
// ---------------------------------------------------------------------------

bool ArcanaTsDb::append(uint8_t channelId, const uint8_t* record) {
    if (!mStarted || mReadOnly) return false;
    if (channelId >= MAX_CHANNELS || !mChannels[channelId].active) return false;

    const uint16_t recSize = mChannels[channelId].schema.recordSize;
    const uint32_t now = mCfg.getTime();

    mCfg.mutex->lock();

    // --- Primary channel: dedicated double-buffer ---
    if (channelId == mCfg.primaryChannel) {
        // Check if bufA is full
        if (mPrimary.writeOffset + recSize > BLOCK_PAYLOAD_SIZE) {
            // Need to flush: swap buffers
            if (mPrimary.flushPending) {
                // Previous flush not done yet
                mCfg.mutex->unlock();
                if (mCfg.overflow == OverflowPolicy::Drop) {
                    mStats.overflowDrops++;
                    return false;
                }
                // Block mode: flush synchronously then retry
                flushPrimaryBuffer();
                mCfg.mutex->lock();
            }

            // Save pre-swap state for flush
            mPrimary.flushPayloadLen = mPrimary.writeOffset;
            mPrimary.flushRecordCount = mPrimary.recordCount;
            mPrimary.flushFirstTs = mPrimary.firstTimestamp;
            mPrimary.flushLastTs = mPrimary.lastTimestamp;

            // Swap A/B
            uint8_t* tmp = mPrimary.bufA;
            mPrimary.bufA = mPrimary.bufB;
            mPrimary.bufB = tmp;
            mPrimary.flushPending = true;

            // Reset write pointer for new bufA
            mPrimary.writeOffset = 0;
            mPrimary.recordCount = 0;
            mPrimary.firstTimestamp = 0;
            mPrimary.lastTimestamp = 0;
        }

        // Append to bufA
        memcpy(mPrimary.bufA + mPrimary.writeOffset, record, recSize);
        mPrimary.writeOffset += recSize;
        if (mPrimary.recordCount == 0) mPrimary.firstTimestamp = now;
        mPrimary.lastTimestamp = now;
        mPrimary.recordCount++;

        mStats.totalRecords++;
        mStats.perChannelRecords[channelId]++;
        mStats.lastTimestamp = now;

        mCfg.mutex->unlock();

        // Trigger flush if bufB has pending data
        if (mPrimary.flushPending) {
            flushPrimaryBuffer();
        }

        return true;
    }

    // --- Slow channel: shared tagged-record buffer ---
    if (!mSlow.buf) {
        mCfg.mutex->unlock();
        return false;
    }

    // Tagged record: [channelId:1][record:recSize]
    const uint16_t taggedSize = 1 + recSize;

    if (mSlow.writeOffset + taggedSize > BLOCK_PAYLOAD_SIZE) {
        // Slow buffer full — flush it
        if (mSlow.flushPending) {
            mCfg.mutex->unlock();
            if (mCfg.overflow == OverflowPolicy::Drop) {
                mStats.overflowDrops++;
                return false;
            }
            flushSlowBuffer();
            mCfg.mutex->lock();
        } else {
            mSlow.flushPending = true;
            mCfg.mutex->unlock();
            flushSlowBuffer();
            mCfg.mutex->lock();
        }
    }

    // Write tagged record
    mSlow.buf[mSlow.writeOffset] = channelId;
    memcpy(mSlow.buf + mSlow.writeOffset + 1, record, recSize);
    mSlow.writeOffset += taggedSize;
    if (mSlow.recordCount == 0) mSlow.firstTimestamp = now;
    mSlow.lastTimestamp = now;
    mSlow.recordCount++;

    mStats.totalRecords++;
    mStats.perChannelRecords[channelId]++;
    mStats.lastTimestamp = now;

    mCfg.mutex->unlock();
    return true;
}

// ---------------------------------------------------------------------------
// Write: flush
// ---------------------------------------------------------------------------

bool ArcanaTsDb::flush() {
    if (!mStarted || mReadOnly) return false;

    bool ok = true;

    // Flush primary (partial block)
    if (mPrimary.bufA && mPrimary.recordCount > 0) {
        mCfg.mutex->lock();
        // Move current data to bufB for flushing
        if (!mPrimary.flushPending) {
            mPrimary.flushPayloadLen = mPrimary.writeOffset;
            mPrimary.flushRecordCount = mPrimary.recordCount;
            mPrimary.flushFirstTs = mPrimary.firstTimestamp;
            mPrimary.flushLastTs = mPrimary.lastTimestamp;

            uint8_t* tmp = mPrimary.bufA;
            mPrimary.bufA = mPrimary.bufB;
            mPrimary.bufB = tmp;
            mPrimary.flushPending = true;
            mPrimary.writeOffset = 0;
            mPrimary.recordCount = 0;
            mPrimary.firstTimestamp = 0;
            mPrimary.lastTimestamp = 0;
        }
        mCfg.mutex->unlock();
        if (!flushPrimaryBuffer()) ok = false;
    }

    // Flush slow buffer (partial block)
    if (mSlow.buf && mSlow.recordCount > 0) {
        mCfg.mutex->lock();
        mSlow.flushPending = true;
        mCfg.mutex->unlock();
        if (!flushSlowBuffer()) ok = false;
    }

    // Update file header with current stats (survives power loss)
    if (ok) {
        if (mCfg.headerKey) {
            writeEntireHeaderBlock();
        } else {
            updateFileHeader();
        }
        mCfg.file->sync();
    }

    return ok;
}

// ---------------------------------------------------------------------------
// Internal: flushPrimaryBuffer
// ---------------------------------------------------------------------------

bool ArcanaTsDb::flushPrimaryBuffer() {
    mCfg.mutex->lock();
    if (!mPrimary.flushPending) {
        mCfg.mutex->unlock();
        return true;
    }

    // Use saved pre-swap state
    uint8_t* flushBuf = mPrimary.bufB;
    uint16_t payloadLen = mPrimary.flushPayloadLen;
    uint16_t recCount = mPrimary.flushRecordCount;
    uint32_t firstTs = mPrimary.flushFirstTs;
    uint32_t lastTs = mPrimary.flushLastTs;

    mCfg.mutex->unlock();

    // Write the block
    uint8_t flags = (payloadLen < BLOCK_PAYLOAD_SIZE) ? 0x01 : 0x00;  // partial flag
    bool ok = writeBlock(mCfg.primaryChannel, flushBuf, payloadLen,
                         recCount, firstTs, lastTs, flags);

    mCfg.mutex->lock();
    mPrimary.flushPending = false;
    mCfg.mutex->unlock();

    if (!ok) mStats.blocksFailed++;
    return ok;
}

bool ArcanaTsDb::flushSlowBuffer() {
    mCfg.mutex->lock();
    if (!mSlow.flushPending && mSlow.recordCount == 0) {
        mCfg.mutex->unlock();
        return true;
    }

    // Copy slow buffer state under lock
    uint16_t payloadLen = mSlow.writeOffset;
    uint16_t recCount = mSlow.recordCount;
    uint32_t firstTs = mSlow.firstTimestamp;
    uint32_t lastTs = mSlow.lastTimestamp;

    // We need to copy the data because slow buffer is single-buffered
    // Use readCache temporarily if available, otherwise write directly
    uint8_t* flushBuf = mSlow.buf;

    // Reset slow buffer for new writes
    mSlow.writeOffset = 0;
    mSlow.recordCount = 0;
    mSlow.firstTimestamp = 0;
    mSlow.lastTimestamp = 0;
    mSlow.flushPending = false;

    mCfg.mutex->unlock();

    if (recCount == 0) return true;

    uint8_t flags = (payloadLen < BLOCK_PAYLOAD_SIZE) ? 0x01 : 0x00;
    bool ok = writeBlock(MULTI_CHANNEL_ID, flushBuf, payloadLen,
                         recCount, firstTs, lastTs, flags);

    if (!ok) mStats.blocksFailed++;
    return ok;
}

// ---------------------------------------------------------------------------
// Internal: writeBlock (atomic commit)
// ---------------------------------------------------------------------------

bool ArcanaTsDb::writeBlock(uint8_t channelId, const uint8_t* payload,
                             uint16_t payloadLen, uint16_t recordCount,
                             uint32_t firstTs, uint32_t lastTs, uint8_t flags) {
    // Build block into a temp buffer on stack (32-byte header + payload)
    // We write the full 4KB block at once for simplicity
    uint8_t* blockBuf = getReadCache();
    if (!blockBuf) return false;

    memset(blockBuf, 0xFF, BLOCK_SIZE);  // fill unused with 0xFF

    // Copy payload at offset 32
    memcpy(blockBuf + BLOCK_HEADER_SIZE, payload, payloadLen);

    // Compute CRC-32 of encrypted payload area (pre-encryption CRC for now)
    uint32_t payloadCrc;

    // Build nonce
    uint8_t nonce[12];
    buildNonce(nonce, mNextSeqNo);

    // Encrypt full payload area (including 0xFF padding) so read-side
    // decrypt of BLOCK_PAYLOAD_SIZE restores 0xFF stop markers correctly
    if (mCfg.cipher) {
        mCfg.cipher->crypt(mCfg.key, nonce, 0,
                           blockBuf + BLOCK_HEADER_SIZE, BLOCK_PAYLOAD_SIZE);
    }

    // CRC of encrypted payload
    payloadCrc = computeIeeeCrc32(blockBuf + BLOCK_HEADER_SIZE, BLOCK_PAYLOAD_SIZE);

    // Build header (all fields except blockSeqNo)
    AtsBlockHeader* hdr = reinterpret_cast<AtsBlockHeader*>(blockBuf);
    hdr->blockSeqNo = 0;  // written last for atomic commit
    hdr->channelId = channelId;
    hdr->flags = flags;
    hdr->recordCount = recordCount;
    hdr->firstTimestamp = firstTs;
    hdr->lastTimestamp = lastTs;
    memcpy(hdr->nonce, nonce, 12);
    hdr->payloadCrc32 = payloadCrc;

    // Write to file: header fields + payload first (skip first 4 bytes = blockSeqNo)
    if (!mCfg.file->seek(mNextBlockOffset + 4)) return false;
    if (mCfg.file->write(blockBuf + 4, BLOCK_SIZE - 4) != (BLOCK_SIZE - 4)) return false;

    // Atomic commit: write blockSeqNo at offset 0 LAST
    uint32_t seqNo = mNextSeqNo;
    if (!mCfg.file->seek(mNextBlockOffset)) return false;
    if (mCfg.file->write(reinterpret_cast<const uint8_t*>(&seqNo), 4) != 4) return false;

    if (!mCfg.file->sync()) return false;

    // Update index
    addIndexEntry(mNextBlockOffset / BLOCK_SIZE, channelId, recordCount, firstTs, lastTs);

    // Advance state
    mNextSeqNo++;
    mNextBlockOffset += BLOCK_SIZE;
    mStats.blocksWritten++;

    return true;
}

// ---------------------------------------------------------------------------
// Internal: nonce construction
// ---------------------------------------------------------------------------

void ArcanaTsDb::buildNonce(uint8_t nonce[12], uint32_t seqNo) const {
    memset(nonce, 0, 12);
    // [seqNo:4LE][createdEpoch:4LE][0x00:4]
    memcpy(nonce, &seqNo, 4);
    memcpy(nonce + 4, &mCreatedEpoch, 4);
}

void ArcanaTsDb::generateHeaderNonce(uint8_t nonce[12]) {
    // Unique per file: [createdEpoch:4LE][counter:4LE][0x00:4]
    // Static counter ensures uniqueness even if two files created in same second
    static uint32_t sNonceCounter = 0;
    uint32_t epoch = mCfg.getTime ? mCfg.getTime() : 0;
    uint32_t cnt = ++sNonceCounter;
    memcpy(nonce, &epoch, 4);
    memcpy(nonce + 4, &cnt, 4);
    memset(nonce + 8, 0, 4);
}

// ---------------------------------------------------------------------------
// Encrypted header: write entire block (build in RAM → encrypt → write)
// ---------------------------------------------------------------------------

bool ArcanaTsDb::writeEntireHeaderBlock() {
    uint8_t* buf = getReadCache();
    if (!buf) return false;
    memset(buf, 0, BLOCK_SIZE);

    // Always derive base from config (not mHeaderBase which reflects last READ format)
    const uint16_t base = mCfg.headerKey ? 16 : 0;

    // --- Build file header at buf[base] ---
    {
        AtsFileHeader hdr;
        memset(&hdr, 0, sizeof(hdr));
        memcpy(hdr.magic, ATS_MAGIC, 4);
        hdr.version = 2;
        hdr.headerBlocks = 1;
        hdr.flags = ATS_FLAG_HAS_SHADOW;
        if (mCfg.cipher) hdr.flags |= ATS_FLAG_ENCRYPTED;
        if (mCfg.headerKey) hdr.flags |= ATS_FLAG_ENC_HEADER;
        if (mIndexCount > 0) hdr.flags |= ATS_FLAG_HAS_INDEX;
        hdr.cipherType = mCfg.cipher ? mCfg.cipher->cipherType() : 0;
        hdr.channelCount = mChannelCount;
        hdr.overflowPolicy = static_cast<uint8_t>(mCfg.overflow);
        hdr.deviceUidSize = mCfg.deviceUidSize;
        hdr.createdEpoch = mCreatedEpoch;
        if (mCfg.deviceUid) {
            uint8_t copyLen = mCfg.deviceUidSize;
            if (copyLen > 16) copyLen = 16;
            memcpy(hdr.deviceUid, mCfg.deviceUid, copyLen);
        }
        hdr.totalBlockCount = mStats.blocksWritten;
        hdr.lastSeqNo = mNextSeqNo > 0 ? mNextSeqNo - 1 : 0;
        hdr.indexBlockOffset = 0;
        hdr.headerCrc32 = computeIeeeCrc32(
            reinterpret_cast<const uint8_t*>(&hdr), 44);
        memcpy(buf + base, &hdr, sizeof(hdr));
    }

    // --- Build channel descriptors at buf[base + 0x40] ---
    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        AtsChannelDescriptor desc;
        memset(&desc, 0, sizeof(desc));
        if (mChannels[i].active) {
            desc.channelId = i;
            desc.fieldCount = mChannels[i].schema.fieldCount;
            desc.recordSize = mChannels[i].schema.recordSize;
            desc.sampleRateHz = mChannels[i].sampleRateHz;
            desc.recordCount = 0;
            strncpy(desc.name, mChannels[i].schema.name, 23);
            desc.name[23] = '\0';
        } else {
            desc.channelId = 0xFF;
        }
        memcpy(buf + base + 0x40 + i * sizeof(AtsChannelDescriptor),
               &desc, sizeof(desc));

        // Field table at buf[base + 0x140 + i*256]
        if (mChannels[i].active && mChannels[i].schema.fieldCount > 0) {
            memcpy(buf + base + 0x140 + i * 256,
                   mChannels[i].schema.fields,
                   mChannels[i].schema.fieldCount * sizeof(FieldDesc));
        }
    }

    // --- Stats at buf[base + 0x940] ---
    memcpy(buf + base + 0x940, &mStats, sizeof(mStats));

    // --- Shadow: copy [base..base+0x9FF] to [base+0xA00..] ---
    // Only copy what fits within the 4KB block
    {
        uint16_t shadowSrc = base;
        uint16_t shadowDst = base + 0x0A00;
        uint16_t copyLen = 0x0A00;  // primary area size
        if (shadowDst + copyLen > BLOCK_SIZE) {
            copyLen = BLOCK_SIZE - shadowDst;
        }
        if (shadowDst < BLOCK_SIZE && copyLen > 0) {
            memcpy(buf + shadowDst, buf + shadowSrc, copyLen);
        }
    }

    // --- Encrypt if headerKey is set ---
    if (mCfg.headerKey && mCfg.cipher) {
        generateHeaderNonce(mHeaderNonce);
        memcpy(buf, mHeaderNonce, 12);     // nonce at [0..11]
        memset(buf + 12, 0, 4);            // reserved at [12..15]
        // Encrypt [16..4095]
        mCfg.cipher->crypt(mCfg.headerKey, mHeaderNonce, 0,
                           buf + 16, BLOCK_SIZE - 16);
    }

    // --- Write to file ---
    if (!mCfg.file->seek(0)) return false;
    return mCfg.file->write(buf, BLOCK_SIZE) == BLOCK_SIZE;
}

// ---------------------------------------------------------------------------
// Encrypted header: read entire block (detect format → decrypt → parse)
// ---------------------------------------------------------------------------

bool ArcanaTsDb::readEntireHeaderBlock() {
    uint8_t* buf = getReadCache();
    if (!buf) return false;

    if (!mCfg.file->seek(0)) return false;
    if (mCfg.file->read(buf, BLOCK_SIZE) != BLOCK_SIZE) return false;

    // Detect format: plaintext ("ATS2" at [0]) vs encrypted (random bytes at [0])
    if (memcmp(buf, ATS_MAGIC, 4) == 0) {
        // Legacy plaintext format
        mHeaderBase = 0;
        if (!readFileHeader()) return false;
        if (!readChannelDescriptors()) return false;
        return true;
    }

    // Try encrypted format
    if (!mCfg.headerKey || !mCfg.cipher) return false;

    // Extract nonce from [0..11]
    memcpy(mHeaderNonce, buf, 12);

    // Decrypt [16..4095] in the buffer
    mCfg.cipher->crypt(mCfg.headerKey, mHeaderNonce, 0,
                       buf + 16, BLOCK_SIZE - 16);

    // Validate: "ATS2" magic at buf[16]
    if (memcmp(buf + 16, ATS_MAGIC, 4) == 0) {
        mHeaderBase = 16;
        return tryDecryptHeaderFromBuf(buf, 16);
    }

    // Primary failed — try shadow at [16 + 0xA00] = [0xA10]
    // Re-read (decryption was in-place, need original for shadow)
    if (!mCfg.file->seek(0)) return false;
    if (mCfg.file->read(buf, BLOCK_SIZE) != BLOCK_SIZE) return false;
    // Re-decrypt
    mCfg.cipher->crypt(mCfg.headerKey, mHeaderNonce, 0,
                       buf + 16, BLOCK_SIZE - 16);

    uint16_t shadowBase = 16 + 0x0A00;
    if (shadowBase + 4 <= BLOCK_SIZE && memcmp(buf + shadowBase, ATS_MAGIC, 4) == 0) {
        mHeaderBase = shadowBase;
        return tryDecryptHeaderFromBuf(buf, shadowBase);
    }

    return false;  // Decryption failed: wrong key or corrupted header
}

bool ArcanaTsDb::tryDecryptHeaderFromBuf(uint8_t* buf, uint16_t base) {
    // Parse file header from buf[base]
    AtsFileHeader hdr;
    memcpy(&hdr, buf + base, sizeof(hdr));

    // Validate CRC
    uint32_t expectedCrc = computeIeeeCrc32(
        reinterpret_cast<const uint8_t*>(&hdr), 44);
    if (expectedCrc != hdr.headerCrc32) return false;

    mCreatedEpoch = hdr.createdEpoch;
    mNextSeqNo = hdr.lastSeqNo + 1;
    mChannelCount = 0;

    // Parse channel descriptors from buf[base + 0x40]
    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        mChannels[i].active = false;

        AtsChannelDescriptor desc;
        uint16_t descOff = base + 0x40 + i * sizeof(AtsChannelDescriptor);
        if (descOff + sizeof(desc) > BLOCK_SIZE) break;
        memcpy(&desc, buf + descOff, sizeof(desc));

        if (desc.channelId == 0xFF) continue;
        if (desc.channelId >= MAX_CHANNELS) continue;

        ArcanaTsSchema& schema = mChannels[i].schema;
        schema = ArcanaTsSchema();
        schema.fieldCount = desc.fieldCount;
        schema.recordSize = desc.recordSize;
        memcpy(schema.name, desc.name, 24);

        // Field table at buf[base + 0x140 + i*256]
        if (desc.fieldCount > 0) {
            uint16_t ftOff = base + 0x140 + i * 256;
            uint16_t ftLen = desc.fieldCount * sizeof(FieldDesc);
            if (ftOff + ftLen <= BLOCK_SIZE) {
                memcpy(schema.fields, buf + ftOff, ftLen);
            }
        }

        mChannels[i].sampleRateHz = desc.sampleRateHz;
        mChannels[i].active = true;
        mChannelCount++;
    }

    // Restore stats from buf[base + 0x940]
    uint16_t statsOff = base + 0x940;
    if (statsOff + sizeof(mStats) <= BLOCK_SIZE) {
        memcpy(&mStats, buf + statsOff, sizeof(mStats));
    }

    return true;
}

// ---------------------------------------------------------------------------
// Internal: file header I/O
// ---------------------------------------------------------------------------

bool ArcanaTsDb::writeFileHeader() {
    AtsFileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));

    memcpy(hdr.magic, ATS_MAGIC, 4);
    hdr.version = 2;
    hdr.headerBlocks = 1;
    hdr.flags = ATS_FLAG_HAS_SHADOW;
    if (mCfg.cipher) hdr.flags |= ATS_FLAG_ENCRYPTED;
    hdr.cipherType = mCfg.cipher ? mCfg.cipher->cipherType() : 0;
    hdr.channelCount = mChannelCount;
    hdr.overflowPolicy = static_cast<uint8_t>(mCfg.overflow);
    hdr.deviceUidSize = mCfg.deviceUidSize;
    hdr.createdEpoch = mCreatedEpoch;
    if (mCfg.deviceUid) {
        uint8_t copyLen = mCfg.deviceUidSize;
        if (copyLen > 16) copyLen = 16;
        memcpy(hdr.deviceUid, mCfg.deviceUid, copyLen);
    }
    hdr.totalBlockCount = mStats.blocksWritten;
    hdr.lastSeqNo = mNextSeqNo > 0 ? mNextSeqNo - 1 : 0;
    hdr.indexBlockOffset = 0;  // updated on close

    // Compute header CRC (bytes 0x0000-0x002B = 44 bytes, everything before headerCrc32)
    hdr.headerCrc32 = computeIeeeCrc32(reinterpret_cast<const uint8_t*>(&hdr), 44);

    if (!mCfg.file->seek(GLOBAL_HEADER_OFFSET)) return false;
    return mCfg.file->write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr);
}

bool ArcanaTsDb::readFileHeader() {
    AtsFileHeader hdr;

    if (!mCfg.file->seek(GLOBAL_HEADER_OFFSET)) return false;
    if (mCfg.file->read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) return false;

    // Validate magic
    if (memcmp(hdr.magic, ATS_MAGIC, 4) != 0) {
        // Try shadow header
        if (!mCfg.file->seek(SHADOW_OFFSET)) return false;
        if (mCfg.file->read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) return false;
        if (memcmp(hdr.magic, ATS_MAGIC, 4) != 0) return false;
    }

    // Validate CRC
    uint32_t expectedCrc = computeIeeeCrc32(reinterpret_cast<const uint8_t*>(&hdr), 44);
    if (expectedCrc != hdr.headerCrc32) return false;

    mCreatedEpoch = hdr.createdEpoch;
    mNextSeqNo = hdr.lastSeqNo + 1;
    mChannelCount = 0;  // will be populated by readChannelDescriptors

    // Restore stats
    if (mCfg.file->seek(STATS_OFFSET)) {
        mCfg.file->read(reinterpret_cast<uint8_t*>(&mStats), sizeof(mStats));
    }

    return true;
}

bool ArcanaTsDb::updateFileHeader() {
    AtsFileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));

    memcpy(hdr.magic, ATS_MAGIC, 4);
    hdr.version = 2;
    hdr.headerBlocks = 1;
    hdr.flags = ATS_FLAG_HAS_SHADOW;
    if (mCfg.cipher) hdr.flags |= ATS_FLAG_ENCRYPTED;
    if (mIndexCount > 0) hdr.flags |= ATS_FLAG_HAS_INDEX;
    hdr.cipherType = mCfg.cipher ? mCfg.cipher->cipherType() : 0;
    hdr.channelCount = mChannelCount;
    hdr.overflowPolicy = static_cast<uint8_t>(mCfg.overflow);
    hdr.deviceUidSize = mCfg.deviceUidSize;
    hdr.createdEpoch = mCreatedEpoch;
    if (mCfg.deviceUid) {
        uint8_t copyLen = mCfg.deviceUidSize;
        if (copyLen > 16) copyLen = 16;
        memcpy(hdr.deviceUid, mCfg.deviceUid, copyLen);
    }
    hdr.totalBlockCount = mStats.blocksWritten;
    hdr.lastSeqNo = mNextSeqNo > 0 ? mNextSeqNo - 1 : 0;
    hdr.indexBlockOffset = 0;  // TODO: set if index written

    hdr.headerCrc32 = computeIeeeCrc32(reinterpret_cast<const uint8_t*>(&hdr), 44);

    if (!mCfg.file->seek(GLOBAL_HEADER_OFFSET)) return false;
    if (mCfg.file->write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) return false;

    // Persist stats
    if (!mCfg.file->seek(STATS_OFFSET)) return false;
    if (mCfg.file->write(reinterpret_cast<const uint8_t*>(&mStats), sizeof(mStats)) != sizeof(mStats)) return false;

    return true;
}

bool ArcanaTsDb::writeShadowHeader() {
    // Read primary header area (0x0000-0x09FF = 2560 bytes)
    uint8_t* cache = getReadCache();
    if (!cache) return false;

    if (!mCfg.file->seek(0)) return false;
    if (mCfg.file->read(cache, SHADOW_OFFSET) != static_cast<int32_t>(SHADOW_OFFSET)) return false;

    // Write shadow at 0x0A00
    if (!mCfg.file->seek(SHADOW_OFFSET)) return false;
    return mCfg.file->write(cache, SHADOW_OFFSET) == static_cast<int32_t>(SHADOW_OFFSET);
}

// ---------------------------------------------------------------------------
// Internal: channel descriptor I/O
// ---------------------------------------------------------------------------

bool ArcanaTsDb::writeChannelDescriptors() {
    // Write channel descriptors at 0x0040
    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        AtsChannelDescriptor desc;
        memset(&desc, 0, sizeof(desc));

        if (mChannels[i].active) {
            desc.channelId = i;
            desc.fieldCount = mChannels[i].schema.fieldCount;
            desc.recordSize = mChannels[i].schema.recordSize;
            desc.sampleRateHz = mChannels[i].sampleRateHz;
            desc.recordCount = 0;
            strncpy(desc.name, mChannels[i].schema.name, 23);
            desc.name[23] = '\0';
        } else {
            desc.channelId = 0xFF;  // unused slot
        }

        uint64_t offset = CHANNEL_DESC_OFFSET + i * sizeof(AtsChannelDescriptor);
        if (!mCfg.file->seek(offset)) return false;
        if (mCfg.file->write(reinterpret_cast<const uint8_t*>(&desc), sizeof(desc)) != sizeof(desc)) return false;

        // Write field table (256 bytes per channel at 0x0140 + i*256)
        if (mChannels[i].active) {
            uint64_t ftOffset = FIELD_TABLE_OFFSET + i * 256;
            if (!mCfg.file->seek(ftOffset)) return false;
            // Write field descriptors
            if (mCfg.file->write(
                    reinterpret_cast<const uint8_t*>(mChannels[i].schema.fields),
                    mChannels[i].schema.fieldCount * sizeof(FieldDesc))
                != static_cast<int32_t>(mChannels[i].schema.fieldCount * sizeof(FieldDesc))) {
                return false;
            }
        }
    }

    return true;
}

bool ArcanaTsDb::readChannelDescriptors() {
    mChannelCount = 0;

    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        mChannels[i].active = false;

        AtsChannelDescriptor desc;
        uint64_t offset = CHANNEL_DESC_OFFSET + i * sizeof(AtsChannelDescriptor);
        if (!mCfg.file->seek(offset)) return false;
        if (mCfg.file->read(reinterpret_cast<uint8_t*>(&desc), sizeof(desc)) != sizeof(desc)) return false;

        if (desc.channelId == 0xFF) continue;
        if (desc.channelId >= MAX_CHANNELS) continue;

        // Read field table
        uint64_t ftOffset = FIELD_TABLE_OFFSET + i * 256;
        if (!mCfg.file->seek(ftOffset)) return false;

        ArcanaTsSchema& schema = mChannels[i].schema;
        schema = ArcanaTsSchema();
        schema.fieldCount = desc.fieldCount;
        schema.recordSize = desc.recordSize;
        memcpy(schema.name, desc.name, 24);

        if (desc.fieldCount > 0) {
            if (mCfg.file->read(
                    reinterpret_cast<uint8_t*>(schema.fields),
                    desc.fieldCount * sizeof(FieldDesc))
                != static_cast<int32_t>(desc.fieldCount * sizeof(FieldDesc))) {
                return false;
            }
        }

        mChannels[i].sampleRateHz = desc.sampleRateHz;
        mChannels[i].active = true;
        mChannelCount++;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Internal: sparse index
// ---------------------------------------------------------------------------

void ArcanaTsDb::addIndexEntry(uint32_t blockNum, uint8_t channelId,
                                uint16_t recordCount,
                                uint32_t firstTs, uint32_t lastTs) {
    if (mIndexCount >= MAX_INDEX_ENTRIES) {
        // Evict oldest entry (shift left)
        memmove(mIndex, mIndex + 1, (MAX_INDEX_ENTRIES - 1) * sizeof(AtsIndexEntry));
        mIndexCount = MAX_INDEX_ENTRIES - 1;
    }

    AtsIndexEntry& e = mIndex[mIndexCount];
    e.blockNumber = blockNum;
    e.channelId = channelId;
    e.flags = 0;
    e.recordCount = recordCount;
    e.firstTimestamp = firstTs;
    e.lastTimestamp = lastTs;
    mIndexCount++;
}

bool ArcanaTsDb::writeIndex() {
    if (mIndexCount == 0) return true;

    // Write index at current end of file
    uint64_t indexOffset = mNextBlockOffset;

    // Index header
    AtsIndexHeader idxHdr;
    memcpy(idxHdr.magic, IDX_MAGIC, 4);
    idxHdr.entryCount = mIndexCount;
    idxHdr.crc32 = computeIeeeCrc32(
        reinterpret_cast<const uint8_t*>(mIndex),
        mIndexCount * sizeof(AtsIndexEntry));
    memset(idxHdr.reserved, 0, 4);

    if (!mCfg.file->seek(indexOffset)) return false;
    if (mCfg.file->write(reinterpret_cast<const uint8_t*>(&idxHdr), sizeof(idxHdr)) != sizeof(idxHdr)) return false;

    // Index entries
    if (mCfg.file->write(
            reinterpret_cast<const uint8_t*>(mIndex),
            mIndexCount * sizeof(AtsIndexEntry))
        != static_cast<int32_t>(mIndexCount * sizeof(AtsIndexEntry))) {
        return false;
    }

    return true;
}

bool ArcanaTsDb::readIndex() {
    // Find index from file header's indexBlockOffset
    AtsFileHeader hdr;
    if (!mCfg.file->seek(GLOBAL_HEADER_OFFSET)) return false;
    if (mCfg.file->read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) return false;

    if (!(hdr.flags & ATS_FLAG_HAS_INDEX) || hdr.indexBlockOffset == 0) return false;

    uint64_t indexOffset = hdr.indexBlockOffset * BLOCK_SIZE;
    if (!mCfg.file->seek(indexOffset)) return false;

    AtsIndexHeader idxHdr;
    if (mCfg.file->read(reinterpret_cast<uint8_t*>(&idxHdr), sizeof(idxHdr)) != sizeof(idxHdr)) return false;
    if (memcmp(idxHdr.magic, IDX_MAGIC, 4) != 0) return false;

    uint16_t count = idxHdr.entryCount;
    if (count > MAX_INDEX_ENTRIES) count = MAX_INDEX_ENTRIES;

    if (mCfg.file->read(
            reinterpret_cast<uint8_t*>(mIndex),
            count * sizeof(AtsIndexEntry))
        != static_cast<int32_t>(count * sizeof(AtsIndexEntry))) {
        return false;
    }

    // Validate CRC
    uint32_t crc = computeIeeeCrc32(
        reinterpret_cast<const uint8_t*>(mIndex),
        count * sizeof(AtsIndexEntry));
    if (crc != idxHdr.crc32) {
        mIndexCount = 0;
        return false;
    }

    mIndexCount = count;
    return true;
}

// ---------------------------------------------------------------------------
// Internal: recovery
// ---------------------------------------------------------------------------

bool ArcanaTsDb::recoverFromExisting() {
    // Detect and decrypt header format (encrypted vs plaintext)
    if (!readEntireHeaderBlock()) return false;

    // Future writes use encrypted format if headerKey is configured
    if (mCfg.headerKey) mHeaderBase = 16;

    uint64_t fileSize = mCfg.file->size();
    mIndexCount = 0;

    // -----------------------------------------------------------------------
    // Fast recovery: use persisted stats to jump near the end of file,
    // then verify only the tail blocks.  O(1) instead of O(n).
    // Falls back to full scan if header stats look stale or inconsistent.
    // -----------------------------------------------------------------------

    uint64_t estimatedEnd = DATA_START_OFFSET
                          + (uint64_t)mStats.blocksWritten * BLOCK_SIZE;

    if (mStats.blocksWritten > 0 && estimatedEnd <= fileSize) {
        // Verify the last few blocks to confirm header stats are trustworthy
        const uint32_t VERIFY_COUNT = 8;
        uint64_t verifyStart = estimatedEnd;
        if (verifyStart > (uint64_t)VERIFY_COUNT * BLOCK_SIZE + DATA_START_OFFSET) {
            verifyStart = estimatedEnd - (uint64_t)VERIFY_COUNT * BLOCK_SIZE;
        } else {
            verifyStart = DATA_START_OFFSET;
        }

        bool tailOk = true;

        for (uint64_t off = verifyStart; off < estimatedEnd; off += BLOCK_SIZE) {
            AtsBlockHeader hdr;
            if (validateBlock(off / BLOCK_SIZE, hdr)) {
                if (hdr.blockSeqNo >= mNextSeqNo) {
                    mNextSeqNo = hdr.blockSeqNo + 1;
                }
            } else {
                tailOk = false;
                break;
            }
        }

        if (tailOk) {
            // Also check one block PAST the expected end (uncommitted block)
            if (estimatedEnd + BLOCK_SIZE <= fileSize) {
                AtsBlockHeader pastHdr;
                if (validateBlock(estimatedEnd / BLOCK_SIZE, pastHdr)) {
                    // More blocks than header recorded — scan forward a bit
                    uint64_t off = estimatedEnd;
                    while (off + BLOCK_SIZE <= fileSize) {
                        AtsBlockHeader hdr;
                        if (!validateBlock(off / BLOCK_SIZE, hdr)) break;
                        if (hdr.blockSeqNo >= mNextSeqNo) {
                            mNextSeqNo = hdr.blockSeqNo + 1;
                        }
                        mStats.blocksWritten++;
                        off += BLOCK_SIZE;
                    }
                    estimatedEnd = off;
                }
            }

            mNextBlockOffset = estimatedEnd;

            // Truncate tail if file extends beyond last valid block
            if (estimatedEnd < fileSize) {
                mCfg.file->seek(estimatedEnd);
                mCfg.file->truncate();
            }

            // Build sparse index so queryLatest can find records on disk.
            // Scan all blocks — O(n) reads but only headers, no payload.
            // For typical device.ats (<100 blocks) this takes <1ms.
            {
                uint64_t off = DATA_START_OFFSET;
                while (off < estimatedEnd && mIndexCount < MAX_INDEX_ENTRIES) {
                    AtsBlockHeader bhdr;
                    if (validateBlock(off / BLOCK_SIZE, bhdr)) {
                        addIndexEntry(off / BLOCK_SIZE, bhdr.channelId,
                                      bhdr.recordCount, bhdr.firstTimestamp,
                                      bhdr.lastTimestamp);
                    }
                    off += BLOCK_SIZE;
                }
            }

            goto buffers_init;
        }
        // else: tail verification failed, fall through to full scan
    }

    {
        // Full scan fallback (slow but safe — only if header stats unreliable)
        mNextBlockOffset = DATA_START_OFFSET;
        uint32_t validBlocks = 0;
        uint32_t totalRecords = 0;
        uint32_t skippedBlocks = 0;
        uint64_t lastValidEnd = DATA_START_OFFSET;
        uint32_t consecutiveBad = 0;

        uint64_t offset = DATA_START_OFFSET;
        while (offset + BLOCK_SIZE <= fileSize) {
            AtsBlockHeader hdr;
            if (!validateBlock(offset / BLOCK_SIZE, hdr)) {
                skippedBlocks++;
                consecutiveBad++;
                offset += BLOCK_SIZE;
                if (consecutiveBad >= 4) break;
                continue;
            }
            consecutiveBad = 0;

            if (mIndexCount < MAX_INDEX_ENTRIES) {
                addIndexEntry(offset / BLOCK_SIZE, hdr.channelId,
                              hdr.recordCount, hdr.firstTimestamp, hdr.lastTimestamp);
            }

            totalRecords += hdr.recordCount;
            if (hdr.blockSeqNo >= mNextSeqNo) {
                mNextSeqNo = hdr.blockSeqNo + 1;
            }
            validBlocks++;
            offset += BLOCK_SIZE;
            lastValidEnd = offset;
        }

        mNextBlockOffset = lastValidEnd;

        if (lastValidEnd < fileSize) {
            mCfg.file->seek(lastValidEnd);
            mCfg.file->truncate();
            mStats.recoveryTruncations++;
        }

        if (skippedBlocks > 0) {
            mStats.blocksFailed += skippedBlocks;
        }

        mStats.blocksWritten = validBlocks;
        mStats.totalRecords = totalRecords;
    }

buffers_init:

    // Re-init buffers
    if (mCfg.primaryChannel != 0xFF && mCfg.primaryBufA && mCfg.primaryBufB) {
        mPrimary.bufA = mCfg.primaryBufA;
        mPrimary.bufB = mCfg.primaryBufB;
        mPrimary.writeOffset = 0;
        mPrimary.recordCount = 0;
        mPrimary.firstTimestamp = 0;
        mPrimary.lastTimestamp = 0;
        mPrimary.flushPending = false;
    }
    if (mCfg.slowBuf) {
        mSlow.buf = mCfg.slowBuf;
        mSlow.writeOffset = 0;
        mSlow.recordCount = 0;
        mSlow.firstTimestamp = 0;
        mSlow.lastTimestamp = 0;
        mSlow.flushPending = false;
    }

    return true;
}

bool ArcanaTsDb::validateBlock(uint32_t blockNum, AtsBlockHeader& hdr) const {
    uint64_t offset = blockNum * BLOCK_SIZE;
    if (!mCfg.file->seek(offset)) return false;
    if (mCfg.file->read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) return false;

    // Uncommitted block check
    if (hdr.blockSeqNo == 0 || hdr.blockSeqNo == 0xFFFFFFFF) return false;

    // Sequence must be monotonic (if we have a previous seqNo)
    // Channel must be valid
    if (hdr.channelId != MULTI_CHANNEL_ID && hdr.channelId >= MAX_CHANNELS) return false;

    return true;
}

// ---------------------------------------------------------------------------
// Internal: read cache / block read+decrypt
// ---------------------------------------------------------------------------

uint8_t* ArcanaTsDb::getReadCache() const {
    if (mCfg.readCache) return mCfg.readCache;
    // Share with slow buffer if no dedicated read cache
    return mCfg.slowBuf;
}

bool ArcanaTsDb::readAndDecryptBlock(uint32_t blockNum, uint8_t* outBuf) const {
    uint64_t offset = blockNum * BLOCK_SIZE;
    if (!mCfg.file->seek(offset)) return false;
    if (mCfg.file->read(outBuf, BLOCK_SIZE) != BLOCK_SIZE) return false;

    AtsBlockHeader* hdr = reinterpret_cast<AtsBlockHeader*>(outBuf);

    // Validate CRC of encrypted payload
    uint32_t crc = computeIeeeCrc32(outBuf + BLOCK_HEADER_SIZE, BLOCK_PAYLOAD_SIZE);
    if (crc != hdr->payloadCrc32) return false;

    // Decrypt payload in-place
    if (mCfg.cipher && mCfg.key) {
        mCfg.cipher->crypt(mCfg.key, hdr->nonce, 0,
                           outBuf + BLOCK_HEADER_SIZE, BLOCK_PAYLOAD_SIZE);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Query: findChannelBySchema
// ---------------------------------------------------------------------------

int8_t ArcanaTsDb::findChannelBySchema(const char* schemaName) const {
    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        if (!mChannels[i].active) continue;
        if (strEq(mChannels[i].schema.name, schemaName, 24)) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

int8_t ArcanaTsDb::findChannelBySchemaId(uint32_t schemaId) const {
    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        if (!mChannels[i].active) continue;
        if (mChannels[i].schema.schemaId() == schemaId) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

const ArcanaTsSchema* ArcanaTsDb::getSchema(uint8_t channelId) const {
    if (channelId >= MAX_CHANNELS || !mChannels[channelId].active) return nullptr;
    return &mChannels[channelId].schema;
}

// ---------------------------------------------------------------------------
// Query: queryLatest
// ---------------------------------------------------------------------------

uint16_t ArcanaTsDb::queryLatest(uint8_t channelId, uint8_t* outBuf,
                                  uint16_t maxRecords) const {
    if (!mStarted || channelId >= MAX_CHANNELS || !mChannels[channelId].active) return 0;
    if (maxRecords == 0 || !outBuf) return 0;

    const uint16_t recSize = mChannels[channelId].schema.recordSize;
    uint16_t found = 0;

    mCfg.mutex->lock();

    // First: check RAM buffers for this channel
    if (channelId == mCfg.primaryChannel && mPrimary.bufA) {
        // Primary channel: records are contiguous in bufA
        uint16_t inBuf = mPrimary.recordCount;
        uint16_t toCopy = (inBuf < maxRecords) ? inBuf : maxRecords;
        if (toCopy > 0) {
            // Copy latest records (from end of buffer)
            uint16_t startIdx = inBuf - toCopy;
            memcpy(outBuf, mPrimary.bufA + startIdx * recSize, toCopy * recSize);
            found = toCopy;
        }
    } else if (mSlow.buf) {
        // Slow channel: scan tagged records for matching channelId
        uint16_t matchCount = 0;
        uint16_t scanOff = 0;

        // First pass: count matches
        while (scanOff < mSlow.writeOffset) {
            uint8_t chId = mSlow.buf[scanOff];
            if (chId >= MAX_CHANNELS || !mChannels[chId].active) break;
            uint16_t rs = mChannels[chId].schema.recordSize;
            if (chId == channelId) matchCount++;
            scanOff += 1 + rs;
        }

        // Second pass: copy latest N
        uint16_t toCopy = (matchCount < maxRecords) ? matchCount : maxRecords;
        uint16_t skip = matchCount - toCopy;
        scanOff = 0;
        uint16_t matchIdx = 0;

        while (scanOff < mSlow.writeOffset && found < toCopy) {
            uint8_t chId = mSlow.buf[scanOff];
            if (chId >= MAX_CHANNELS || !mChannels[chId].active) break;
            uint16_t rs = mChannels[chId].schema.recordSize;
            if (chId == channelId) {
                if (matchIdx >= skip) {
                    memcpy(outBuf + found * recSize, mSlow.buf + scanOff + 1, recSize);
                    found++;
                }
                matchIdx++;
            }
            scanOff += 1 + rs;
        }
    }

    mCfg.mutex->unlock();

    // If we need more records, read from disk (latest blocks first)
    if (found < maxRecords && mIndexCount > 0) {
        uint8_t* cache = getReadCache();
        if (cache) {
            // Walk index backwards for matching channel
            for (int16_t i = static_cast<int16_t>(mIndexCount) - 1;
                 i >= 0 && found < maxRecords; i--) {
                const AtsIndexEntry& ie = mIndex[i];
                if (ie.channelId != channelId && ie.channelId != MULTI_CHANNEL_ID) continue;

                if (!readAndDecryptBlock(ie.blockNumber, cache)) continue;

                const AtsBlockHeader* hdr = reinterpret_cast<const AtsBlockHeader*>(cache);
                const uint8_t* payload = cache + BLOCK_HEADER_SIZE;

                if (hdr->channelId == channelId) {
                    // Single-channel block
                    uint16_t inBlock = hdr->recordCount;
                    uint16_t need = maxRecords - found;
                    uint16_t toCopy = (inBlock < need) ? inBlock : need;
                    uint16_t startIdx = inBlock - toCopy;
                    // Shift existing records right to make room at front
                    if (found > 0) {
                        memmove(outBuf + toCopy * recSize, outBuf, found * recSize);
                    }
                    memcpy(outBuf, payload + startIdx * recSize, toCopy * recSize);
                    found += toCopy;
                } else if (hdr->channelId == MULTI_CHANNEL_ID) {
                    // Multi-channel: scan tags for matching channel
                    uint16_t off = 0;
                    uint16_t blockMatches = 0;
                    while (off < BLOCK_PAYLOAD_SIZE) {
                        uint8_t chId = payload[off];
                        if (chId >= MAX_CHANNELS || !mChannels[chId].active) break;
                        uint16_t rs = mChannels[chId].schema.recordSize;
                        if (chId == channelId) blockMatches++;
                        off += 1 + rs;
                    }

                    uint16_t need = maxRecords - found;
                    uint16_t toCopy = (blockMatches < need) ? blockMatches : need;
                    uint16_t skip = blockMatches - toCopy;
                    uint16_t matchIdx = 0;
                    uint16_t copied = 0;
                    off = 0;

                    while (off < BLOCK_PAYLOAD_SIZE && copied < toCopy) {
                        uint8_t chId = payload[off];
                        if (chId >= MAX_CHANNELS || !mChannels[chId].active) break;
                        uint16_t rs = mChannels[chId].schema.recordSize;
                        if (chId == channelId) {
                            if (matchIdx >= skip) {
                                if (found > 0 && copied == 0) {
                                    memmove(outBuf + toCopy * recSize, outBuf, found * recSize);
                                }
                                memcpy(outBuf + copied * recSize, payload + off + 1, recSize);
                                copied++;
                            }
                            matchIdx++;
                        }
                        off += 1 + rs;
                    }
                    found += copied;
                }
            }
        }
    }

    return found;
}

uint16_t ArcanaTsDb::queryLatestBySchema(const char* schemaName, uint8_t* outBuf,
                                          uint16_t maxRecords) const {
    int8_t ch = findChannelBySchema(schemaName);
    if (ch < 0) return 0;
    return queryLatest(static_cast<uint8_t>(ch), outBuf, maxRecords);
}

// ---------------------------------------------------------------------------
// Query: queryByTime
// ---------------------------------------------------------------------------

bool ArcanaTsDb::queryByTime(uint8_t channelId, uint32_t startEpoch,
                              uint32_t endEpoch, RecordCallback cb, void* ctx) const {
    if (!mStarted || channelId >= MAX_CHANNELS || !mChannels[channelId].active) return false;
    if (!cb) return false;

    const uint16_t recSize = mChannels[channelId].schema.recordSize;
    uint8_t* cache = getReadCache();
    if (!cache) return false;

    // Iterate index entries
    for (uint16_t i = 0; i < mIndexCount; i++) {
        const AtsIndexEntry& ie = mIndex[i];

        // Skip blocks outside time range
        if (ie.lastTimestamp < startEpoch) continue;
        if (ie.firstTimestamp > endEpoch) break;

        // Skip blocks for other channels
        if (ie.channelId != channelId && ie.channelId != MULTI_CHANNEL_ID) continue;

        if (!readAndDecryptBlock(ie.blockNumber, cache)) continue;

        const AtsBlockHeader* hdr = reinterpret_cast<const AtsBlockHeader*>(cache);
        const uint8_t* payload = cache + BLOCK_HEADER_SIZE;

        if (hdr->channelId == channelId) {
            // Single-channel block: iterate fixed-size records
            for (uint16_t r = 0; r < hdr->recordCount; r++) {
                const uint8_t* rec = payload + r * recSize;
                // Extract timestamp (first 4 bytes by convention)
                uint32_t ts;
                memcpy(&ts, rec, 4);
                if (ts < startEpoch) continue;
                if (ts > endEpoch) continue;
                if (cb(channelId, rec, ts, ctx)) return true;  // early stop
            }
        } else if (hdr->channelId == MULTI_CHANNEL_ID) {
            // Multi-channel: parse tagged records
            uint16_t off = 0;
            while (off < BLOCK_PAYLOAD_SIZE) {
                uint8_t chId = payload[off];
                if (chId >= MAX_CHANNELS || !mChannels[chId].active) break;
                uint16_t rs = mChannels[chId].schema.recordSize;
                if (chId == channelId) {
                    const uint8_t* rec = payload + off + 1;
                    uint32_t ts;
                    memcpy(&ts, rec, 4);
                    if (ts >= startEpoch && ts <= endEpoch) {
                        if (cb(channelId, rec, ts, ctx)) return true;
                    }
                }
                off += 1 + rs;
            }
        }
    }

    return true;
}

bool ArcanaTsDb::queryBySchema(const char* schemaName, uint32_t startEpoch,
                                uint32_t endEpoch, RecordCallback cb, void* ctx) const {
    int8_t ch = findChannelBySchema(schemaName);
    if (ch < 0) return false;
    return queryByTime(static_cast<uint8_t>(ch), startEpoch, endEpoch, cb, ctx);
}

// ---------------------------------------------------------------------------
// Query: queryAllChannelsByTime
// ---------------------------------------------------------------------------

bool ArcanaTsDb::queryAllChannelsByTime(uint32_t startEpoch, uint32_t endEpoch,
                                         RecordCallback cb, void* ctx) const {
    if (!mStarted || !cb) return false;

    uint8_t* cache = getReadCache();
    if (!cache) return false;

    for (uint16_t i = 0; i < mIndexCount; i++) {
        const AtsIndexEntry& ie = mIndex[i];

        if (ie.lastTimestamp < startEpoch) continue;
        if (ie.firstTimestamp > endEpoch) break;

        if (!readAndDecryptBlock(ie.blockNumber, cache)) continue;

        const AtsBlockHeader* hdr = reinterpret_cast<const AtsBlockHeader*>(cache);
        const uint8_t* payload = cache + BLOCK_HEADER_SIZE;

        if (hdr->channelId != MULTI_CHANNEL_ID && hdr->channelId < MAX_CHANNELS) {
            // Single-channel block
            uint8_t chId = hdr->channelId;
            if (!mChannels[chId].active) continue;
            uint16_t recSize = mChannels[chId].schema.recordSize;

            for (uint16_t r = 0; r < hdr->recordCount; r++) {
                const uint8_t* rec = payload + r * recSize;
                uint32_t ts;
                memcpy(&ts, rec, 4);
                if (ts < startEpoch) continue;
                if (ts > endEpoch) continue;
                if (cb(chId, rec, ts, ctx)) return true;
            }
        } else if (hdr->channelId == MULTI_CHANNEL_ID) {
            // Multi-channel: all tags
            uint16_t off = 0;
            while (off < BLOCK_PAYLOAD_SIZE) {
                uint8_t chId = payload[off];
                if (chId >= MAX_CHANNELS || !mChannels[chId].active) break;
                uint16_t rs = mChannels[chId].schema.recordSize;
                const uint8_t* rec = payload + off + 1;
                uint32_t ts;
                memcpy(&ts, rec, 4);
                if (ts >= startEpoch && ts <= endEpoch) {
                    if (cb(chId, rec, ts, ctx)) return true;
                }
                off += 1 + rs;
            }
        }
    }

    return true;
}

} // namespace ats
} // namespace arcana
