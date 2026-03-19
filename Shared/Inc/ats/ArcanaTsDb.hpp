/**
 * @file ArcanaTsDb.hpp
 * @brief ArcanaTS v2 core engine — multi-channel embedded TSDB
 *
 * ZERO platform dependencies. All I/O, crypto, and RTOS via PAL interfaces.
 * Supports: multi-channel append, buffered block I/O, atomic commit,
 *           power-loss recovery, sparse index, on-device query.
 */

#ifndef ARCANA_ATS_DB_HPP
#define ARCANA_ATS_DB_HPP

#include "ArcanaTsTypes.hpp"
#include "ArcanaTsSchema.hpp"
#include "IFilePort.hpp"
#include "ICipher.hpp"
#include "IMutex.hpp"

namespace arcana {
namespace ats {

/**
 * @brief Multi-channel time-series database engine
 *
 * Two usage modes:
 *  - Read-write: open() + addChannel() + start() + append() + close()
 *  - Read-only:  openReadOnly() + query*() + close()
 *
 * Caller provides all buffers (static allocation, no heap).
 */
class ArcanaTsDb {
public:
    ArcanaTsDb();

    // -- Lifecycle ----------------------------------------------------------

    /** @brief Open for read-write (new file or resume existing) */
    bool open(const char* path, const AtsConfig& cfg);

    /** @brief Open for read-only (upload/query) */
    bool openReadOnly(const char* path, const AtsConfig& cfg);

    /** @brief Register a channel with its schema (call before start()) */
    bool addChannel(uint8_t channelId, const ArcanaTsSchema& schema,
                    uint16_t sampleRateHz = 0);

    /** @brief Write file header and begin accepting appends */
    bool start();

    /** @brief Flush all buffers, write index, update header, sync, close file */
    bool close();

    bool isOpen() const { return mOpen; }
    bool isReadOnly() const { return mReadOnly; }

    // -- Write (hot path) ---------------------------------------------------

    /** @brief Append one record to a channel (~0.7us primary, ~1us slow) */
    bool append(uint8_t channelId, const uint8_t* record);

    /** @brief Force flush all pending buffers to disk */
    bool flush();

    // -- Query by channel ID ------------------------------------------------

    /** @brief Get latest N records from channel (RAM-first, then disk) */
    uint16_t queryLatest(uint8_t channelId, uint8_t* outBuf,
                         uint16_t maxRecords) const;

    /** @brief Iterate records in time range, callback per record */
    bool queryByTime(uint8_t channelId, uint32_t startEpoch, uint32_t endEpoch,
                     RecordCallback cb, void* ctx) const;

    // -- Query by schema name -----------------------------------------------

    uint16_t queryLatestBySchema(const char* schemaName, uint8_t* outBuf,
                                 uint16_t maxRecords) const;

    bool queryBySchema(const char* schemaName, uint32_t startEpoch,
                       uint32_t endEpoch, RecordCallback cb, void* ctx) const;

    // -- Query all channels -------------------------------------------------

    bool queryAllChannelsByTime(uint32_t startEpoch, uint32_t endEpoch,
                                RecordCallback cb, void* ctx) const;

    // -- Channel/schema lookup ----------------------------------------------

    int8_t findChannelBySchema(const char* schemaName) const;
    int8_t findChannelBySchemaId(uint32_t schemaId) const;

    // -- Info ---------------------------------------------------------------

    const StorageStats& getStats() const { return mStats; }
    uint8_t getChannelCount() const { return mChannelCount; }
    const ArcanaTsSchema* getSchema(uint8_t channelId) const;

private:
    // -- Per-channel runtime state ------------------------------------------
    struct ChannelState {
        ArcanaTsSchema schema;
        uint16_t       sampleRateHz;
        bool           active;
    };

    // -- Buffer bookkeeping -------------------------------------------------
    struct PrimaryBuf {
        uint8_t* bufA;          // active write buffer
        uint8_t* bufB;          // flush buffer (swap with A)
        uint16_t writeOffset;   // byte offset into bufA
        uint16_t recordCount;   // records in bufA
        uint32_t firstTimestamp;
        uint32_t lastTimestamp;
        bool     flushPending;  // bufB has data to flush
        // Saved state for flush (snapshot at swap time)
        uint16_t flushPayloadLen;
        uint16_t flushRecordCount;
        uint32_t flushFirstTs;
        uint32_t flushLastTs;
    };

    struct SlowBuf {
        uint8_t* buf;
        uint16_t writeOffset;
        uint16_t recordCount;   // total tagged records
        uint32_t firstTimestamp;
        uint32_t lastTimestamp;
        bool     flushPending;
    };

    // -- Sparse index (RAM) -------------------------------------------------
    static const uint16_t MAX_INDEX_ENTRIES = 85;

    // -- Internal methods ---------------------------------------------------

    // File header I/O
    bool writeFileHeader();
    bool readFileHeader();
    bool updateFileHeader();
    bool writeShadowHeader();

    // Channel descriptors & field tables I/O
    bool writeChannelDescriptors();
    bool readChannelDescriptors();

    // Block I/O
    bool flushPrimaryBuffer();
    bool flushSlowBuffer();
    bool writeBlock(uint8_t channelId, const uint8_t* payload,
                    uint16_t payloadLen, uint16_t recordCount,
                    uint32_t firstTs, uint32_t lastTs, uint8_t flags);

    // Nonce construction
    void buildNonce(uint8_t nonce[12], uint32_t seqNo) const;

    // Index
    void addIndexEntry(uint32_t blockNum, uint8_t channelId,
                       uint16_t recordCount, uint32_t firstTs, uint32_t lastTs);
    bool writeIndex();
    bool readIndex();

    // Recovery
    bool recoverFromExisting();
    bool validateBlock(uint32_t blockNum, AtsBlockHeader& hdr) const;

    // Query helpers
    uint8_t* getReadCache() const;
    bool readAndDecryptBlock(uint32_t blockNum, uint8_t* outBuf) const;

    // -- State --------------------------------------------------------------

    AtsConfig       mCfg;
    bool            mOpen;
    bool            mReadOnly;
    bool            mStarted;
    uint8_t         mChannelCount;
    uint32_t        mNextSeqNo;
    uint32_t        mCreatedEpoch;
    uint64_t        mNextBlockOffset;   // file offset of next data block

    ChannelState    mChannels[MAX_CHANNELS];
    PrimaryBuf      mPrimary;
    SlowBuf         mSlow;
    StorageStats    mStats;

    AtsIndexEntry   mIndex[MAX_INDEX_ENTRIES];
    uint16_t        mIndexCount;
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_ATS_DB_HPP */
