/**
 * @file FrameAssembler.hpp
 * @brief Byte-level frame reassembly state machine (header-only)
 *
 * Handles BLE MTU fragmentation (斷包) and concatenation (黏包).
 * Scans for FrameCodec magic [0xAC 0xDA], accumulates header + payload + CRC.
 * Does NOT verify CRC — that is FrameCodec::deframe()'s job.
 */

#ifndef ARCANA_FRAME_ASSEMBLER_HPP
#define ARCANA_FRAME_ASSEMBLER_HPP

#include <cstdint>
#include <cstring>
#include "FrameCodec.hpp"

namespace arcana {

class FrameAssembler {
public:
    static constexpr uint16_t MAX_FRAME = 64;

    FrameAssembler() { reset(); }

    /**
     * Feed one byte into the state machine.
     * @return true when a complete frame is assembled (call getFrame/getFrameLen)
     */
    bool feedByte(uint8_t b) {
        switch (mState) {
        case State::IDLE:
            if (b == FrameCodec::kMagic0) {
                mBuf[0] = b;
                mPos = 1;
                mState = State::MAGIC1;
            }
            break;

        case State::MAGIC1:
            if (b == FrameCodec::kMagic1) {
                mBuf[mPos++] = b;
                mState = State::HEADER;
                mHeaderLeft = 5;  // ver + flags + sid + lenLo + lenHi
            } else if (b == FrameCodec::kMagic0) {
                // Could be start of new header — stay at pos 1
                mBuf[0] = b;
                mPos = 1;
            } else {
                reset();
            }
            break;

        case State::HEADER:
            if (mPos >= MAX_FRAME) { reset(); break; }
            mBuf[mPos++] = b;
            mHeaderLeft--;
            if (mHeaderLeft == 0) {
                // Extract payload length from header
                uint16_t payloadLen =
                    static_cast<uint16_t>(mBuf[FrameCodec::kOffLenLo]) |
                    (static_cast<uint16_t>(mBuf[FrameCodec::kOffLenHi]) << 8);

                mRemaining = payloadLen + 2;  // payload + 2 CRC bytes
                if (mPos + mRemaining > MAX_FRAME) {
                    // Frame too large for buffer
                    reset();
                } else {
                    mState = State::PAYLOAD;
                }
            }
            break;

        case State::PAYLOAD:
            mBuf[mPos++] = b;
            mRemaining--;
            if (mRemaining == 0) {
                mFrameLen = mPos;
                mState = State::COMPLETE;
                return true;
            }
            break;

        case State::COMPLETE:
            // Caller hasn't consumed yet — ignore new bytes
            break;
        }
        return false;
    }

    const uint8_t* getFrame() const { return mBuf; }
    uint16_t getFrameLen() const { return mFrameLen; }

    void reset() {
        mState = State::IDLE;
        mPos = 0;
        mFrameLen = 0;
        mHeaderLeft = 0;
        mRemaining = 0;
    }

private:
    enum class State : uint8_t {
        IDLE,
        MAGIC1,
        HEADER,
        PAYLOAD,
        COMPLETE
    };

    State    mState;
    uint8_t  mBuf[MAX_FRAME];
    uint16_t mPos;
    uint16_t mFrameLen;
    uint8_t  mHeaderLeft;
    uint16_t mRemaining;
};

} // namespace arcana

#endif /* ARCANA_FRAME_ASSEMBLER_HPP */
