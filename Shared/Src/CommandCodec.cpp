/**
 * @file CommandCodec.cpp
 * @brief CommandCodec implementation — binary encode/decode
 */

#include "CommandCodec.hpp"
#include <cstring>

namespace arcana {

bool CommandCodec::decodeRequest(const uint8_t* frame, size_t frameLen,
                                 CommandRequest& out) {
    /* Deframe */
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    uint8_t flags = 0;
    uint8_t streamId = 0;

    if (!FrameCodec::deframe(frame, frameLen, payload, payloadLen, flags, streamId)) {
        return false;
    }

    /* Minimum payload: cluster(1) + commandId(1) + paramsLen(1) = 3 */
    if (payloadLen < 3) return false;

    uint8_t paramsLen = payload[2];
    if (paramsLen > 8) return false;
    if (payloadLen != static_cast<size_t>(3 + paramsLen)) return false;

    /* Fill output */
    out.key.cluster = static_cast<Cluster>(payload[0]);
    out.key.commandId = payload[1];
    out.paramsLength = paramsLen;
    if (paramsLen > 0) {
        memcpy(out.params, payload + 3, paramsLen);
    }

    return true;
}

bool CommandCodec::encodeResponse(const CommandResponseModel& rsp,
                                  uint8_t* buf, size_t bufSize, size_t& outLen,
                                  uint8_t flags, uint8_t streamId) {
    /* Build payload: cluster(1) + commandId(1) + status(1) + dataLen(1) + data(0-16) */
    uint8_t dataLen = rsp.dataLength;
    if (dataLen > CommandResponseModel::MAX_DATA_LENGTH) return false;

    size_t payloadLen = 4 + dataLen;
    uint8_t payload[4 + CommandResponseModel::MAX_DATA_LENGTH];

    payload[0] = static_cast<uint8_t>(rsp.key.cluster);
    payload[1] = rsp.key.commandId;
    payload[2] = static_cast<uint8_t>(rsp.status);
    payload[3] = dataLen;
    if (dataLen > 0) {
        memcpy(payload + 4, rsp.data, dataLen);
    }

    /* Frame it */
    return FrameCodec::frame(payload, payloadLen, flags, streamId,
                             buf, bufSize, outLen);
}

} // namespace arcana
