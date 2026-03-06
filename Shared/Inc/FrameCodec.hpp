/**
 * @file FrameCodec.hpp
 * @brief Frame codec for wire protocol (header-only, static methods)
 *
 * Ported from ESP32 arcana-embedded-esp32 FrameCodec.
 * Frame format:
 *   [magic:2][ver:1][flags:1][sid:1][len:2 LE][payload:N][crc:2 LE]
 *   0xAC DA   0x01   bit0=FIN          payload length
 *
 * Overhead: 9 bytes (7 header + 2 CRC).
 * CRC-16 computed over header + payload.
 */

#ifndef ARCANA_FRAME_CODEC_HPP
#define ARCANA_FRAME_CODEC_HPP

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "Crc16.hpp"

namespace arcana {

class FrameCodec {
public:
    /* Constants */
    static constexpr uint8_t  kMagic0   = 0xAC;
    static constexpr uint8_t  kMagic1   = 0xDA;
    static constexpr uint8_t  kVersion  = 0x01;
    static constexpr size_t   kOverhead = 9;   /* 7 header + 2 CRC */
    static constexpr uint8_t  kFlagFin  = 0x01;
    static constexpr uint8_t  kSidNone  = 0x00;

    /* Header field offsets */
    static constexpr size_t kOffMagic0  = 0;
    static constexpr size_t kOffMagic1  = 1;
    static constexpr size_t kOffVer     = 2;
    static constexpr size_t kOffFlags   = 3;
    static constexpr size_t kOffSid     = 4;
    static constexpr size_t kOffLenLo   = 5;
    static constexpr size_t kOffLenHi   = 6;
    static constexpr size_t kHeaderSize = 7;

    /**
     * @brief Wrap a payload into a framed buffer
     * @param payload   Source payload bytes
     * @param payloadLen Payload length
     * @param flags     Frame flags (kFlagFin for single-frame)
     * @param streamId  Stream ID (kSidNone for no stream)
     * @param outBuf    Destination buffer (must be >= payloadLen + kOverhead)
     * @param outBufSize Size of outBuf
     * @param outLen    [out] Total frame length written
     * @return true on success
     */
    static bool frame(const uint8_t* payload, size_t payloadLen,
                      uint8_t flags, uint8_t streamId,
                      uint8_t* outBuf, size_t outBufSize, size_t& outLen) {

        size_t totalLen = payloadLen + kOverhead;
        if (totalLen > outBufSize) return false;
        if (payloadLen > 0xFFFF) return false;

        /* Header */
        outBuf[kOffMagic0] = kMagic0;
        outBuf[kOffMagic1] = kMagic1;
        outBuf[kOffVer]    = kVersion;
        outBuf[kOffFlags]  = flags;
        outBuf[kOffSid]    = streamId;
        outBuf[kOffLenLo]  = static_cast<uint8_t>(payloadLen & 0xFF);
        outBuf[kOffLenHi]  = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);

        /* Payload */
        if (payloadLen > 0) {
            memcpy(outBuf + kHeaderSize, payload, payloadLen);
        }

        /* CRC-16 over header + payload */
        uint16_t crc = crc16(0, outBuf, kHeaderSize + payloadLen);
        outBuf[kHeaderSize + payloadLen]     = static_cast<uint8_t>(crc & 0xFF);
        outBuf[kHeaderSize + payloadLen + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);

        outLen = totalLen;
        return true;
    }

    /**
     * @brief Extract payload from a framed buffer
     * @param frameBuf   Raw frame bytes
     * @param frameLen   Length of frame
     * @param outPayload [out] Pointer set to payload start within frameBuf
     * @param outPayloadLen [out] Payload length
     * @param outFlags   [out] Parsed flags byte
     * @param outStreamId [out] Parsed stream ID
     * @return true if frame is valid (magic, version, CRC all OK)
     */
    static bool deframe(const uint8_t* frameBuf, size_t frameLen,
                        const uint8_t*& outPayload, size_t& outPayloadLen,
                        uint8_t& outFlags, uint8_t& outStreamId) {

        /* Minimum frame = header + CRC, no payload */
        if (frameLen < kOverhead) return false;

        /* Magic */
        if (frameBuf[kOffMagic0] != kMagic0 || frameBuf[kOffMagic1] != kMagic1) {
            return false;
        }

        /* Version */
        if (frameBuf[kOffVer] != kVersion) return false;

        /* Payload length */
        size_t payloadLen = static_cast<size_t>(frameBuf[kOffLenLo])
                          | (static_cast<size_t>(frameBuf[kOffLenHi]) << 8);

        if (kHeaderSize + payloadLen + 2 != frameLen) return false;

        /* CRC check */
        uint16_t expected = crc16(0, frameBuf, kHeaderSize + payloadLen);
        uint16_t received = static_cast<uint16_t>(frameBuf[kHeaderSize + payloadLen])
                          | (static_cast<uint16_t>(frameBuf[kHeaderSize + payloadLen + 1]) << 8);
        if (expected != received) return false;

        /* Output */
        outFlags     = frameBuf[kOffFlags];
        outStreamId  = frameBuf[kOffSid];
        outPayload   = frameBuf + kHeaderSize;
        outPayloadLen = payloadLen;
        return true;
    }
};

} // namespace arcana

#endif /* ARCANA_FRAME_CODEC_HPP */
