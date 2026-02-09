/**
 * @file CommandCodec.hpp
 * @brief Binary encode/decode for CommandRequest <-> wire bytes
 *
 * Request payload: [cluster:1][commandId:1][paramsLen:1][params:0-8]
 * Response payload: [cluster:1][commandId:1][status:1][dataLen:1][data:0-16]
 */

#ifndef ARCANA_COMMAND_CODEC_HPP
#define ARCANA_COMMAND_CODEC_HPP

#include <cstdint>
#include <cstddef>
#include "FrameCodec.hpp"
#include "CommandTypes.hpp"

namespace arcana {

class CommandCodec {
public:
    static constexpr size_t MAX_REQUEST_FRAME  = 20;  /* 9 overhead + 3 header + 8 params */
    static constexpr size_t MAX_RESPONSE_FRAME = 29;  /* 9 overhead + 4 header + 16 data */

    /**
     * @brief Decode a raw frame into a CommandRequest
     * @param frame    Raw frame bytes (including magic/CRC)
     * @param frameLen Length of frame
     * @param out      [out] Decoded command request
     * @return true if frame is valid and payload decodes correctly
     */
    static bool decodeRequest(const uint8_t* frame, size_t frameLen,
                              CommandRequest& out);

    /**
     * @brief Encode a CommandResponseModel into a framed buffer
     * @param rsp      Response model to encode
     * @param buf      [out] Destination buffer
     * @param bufSize  Size of destination buffer
     * @param outLen   [out] Total bytes written
     * @param flags    Frame flags (default: FIN)
     * @param streamId Stream ID (default: none)
     * @return true on success
     */
    static bool encodeResponse(const CommandResponseModel& rsp,
                               uint8_t* buf, size_t bufSize, size_t& outLen,
                               uint8_t flags = FrameCodec::kFlagFin,
                               uint8_t streamId = FrameCodec::kSidNone);
};

} // namespace arcana

#endif /* ARCANA_COMMAND_CODEC_HPP */
