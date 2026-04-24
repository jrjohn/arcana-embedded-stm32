#pragma once
#include <cstdint>
#include <cstring>

namespace arcana {

/**
 * Minimal MQTT 3.1.1 packet builder/parser — header-only, zero allocation.
 * Option B: manual MQTT over ESP8266 AT+CIPSTART="SSL" raw TCP.
 */
class MqttPacket {
public:
    enum Type : uint8_t {
        CONNECT    = 0x10,
        CONNACK    = 0x20,
        PUBLISH    = 0x30,
        PUBACK     = 0x40,
        SUBSCRIBE  = 0x80,  // fixed header byte = 0x82
        SUBACK     = 0x90,
        PINGREQ    = 0xC0,
        PINGRESP   = 0xD0,
        DISCONNECT = 0xE0,
    };

    // ---- helpers ----

    static uint8_t packetType(uint8_t b) { return b & 0xF0; }

    static uint16_t encodeRemLen(uint8_t* buf, uint32_t len) {
        uint16_t n = 0;
        do {
            uint8_t b = len & 0x7F;
            len >>= 7;
            if (len > 0) b |= 0x80;
            buf[n++] = b;
        } while (len > 0);
        return n;
    }

    static uint16_t decodeRemLen(const uint8_t* buf, uint16_t bufLen,
                                  uint32_t& out) {
        out = 0;
        uint32_t mul = 1;
        for (uint16_t i = 0; i < 4 && i < bufLen; i++) {
            out += (buf[i] & 0x7F) * mul;
            if (!(buf[i] & 0x80)) return i + 1;
            mul <<= 7;
        }
        return 0;  // malformed
    }

    static uint16_t writeStr(uint8_t* buf, const char* s) {
        uint16_t len = (uint16_t)strlen(s);
        buf[0] = (uint8_t)(len >> 8);
        buf[1] = (uint8_t)(len);
        memcpy(buf + 2, s, len);
        return 2 + len;
    }

    // ---- build ----

    static uint16_t buildConnect(uint8_t* buf, uint16_t bufSz,
                                  const char* clientId,
                                  const char* user, const char* pass,
                                  uint16_t keepAlive = 60) {
        uint16_t cLen = (uint16_t)strlen(clientId);
        uint16_t uLen = user ? (uint16_t)strlen(user) : 0;
        uint16_t pLen = pass ? (uint16_t)strlen(pass) : 0;

        uint8_t flags = 0x02;  // clean session
        uint32_t payload = 2 + cLen;
        if (uLen) { flags |= 0x80; payload += 2 + uLen; }
        if (pLen) { flags |= 0x40; payload += 2 + pLen; }

        uint32_t remLen = 10 + payload;  // var hdr 10 bytes
        uint16_t pos = 0;
        buf[pos++] = 0x10;
        pos += encodeRemLen(buf + pos, remLen);

        // Variable header: "MQTT" level 4
        buf[pos++] = 0; buf[pos++] = 4;
        buf[pos++] = 'M'; buf[pos++] = 'Q';
        buf[pos++] = 'T'; buf[pos++] = 'T';
        buf[pos++] = 4;          // protocol level
        buf[pos++] = flags;
        buf[pos++] = (uint8_t)(keepAlive >> 8);
        buf[pos++] = (uint8_t)(keepAlive);

        pos += writeStr(buf + pos, clientId);
        if (uLen) pos += writeStr(buf + pos, user);
        if (pLen) pos += writeStr(buf + pos, pass);
        return pos;
    }

    static uint16_t buildPublish(uint8_t* buf, uint16_t bufSz,
                                  const char* topic,
                                  const uint8_t* payload, uint16_t payloadLen,
                                  uint8_t qos = 0, uint16_t packetId = 0,
                                  bool retain = false) {
        uint16_t tLen = (uint16_t)strlen(topic);
        uint32_t remLen = 2 + tLen + payloadLen;
        if (qos > 0) remLen += 2;  // packet ID
        uint16_t pos = 0;
        buf[pos++] = 0x30 | ((qos & 0x03) << 1) | (retain ? 1 : 0);
        pos += encodeRemLen(buf + pos, remLen);
        buf[pos++] = (uint8_t)(tLen >> 8);
        buf[pos++] = (uint8_t)(tLen);
        memcpy(buf + pos, topic, tLen); pos += tLen;
        if (qos > 0) {
            buf[pos++] = (uint8_t)(packetId >> 8);
            buf[pos++] = (uint8_t)(packetId);
        }
        memcpy(buf + pos, payload, payloadLen); pos += payloadLen;
        return pos;
    }

    static uint16_t buildSubscribe(uint8_t* buf, uint16_t bufSz,
                                    uint16_t packetId,
                                    const char* topic, uint8_t qos = 0) {
        uint16_t tLen = (uint16_t)strlen(topic);
        uint32_t remLen = 2 + 2 + tLen + 1;
        uint16_t pos = 0;
        buf[pos++] = 0x82;
        pos += encodeRemLen(buf + pos, remLen);
        buf[pos++] = (uint8_t)(packetId >> 8);
        buf[pos++] = (uint8_t)(packetId);
        pos += writeStr(buf + pos, topic);
        buf[pos++] = qos;
        return pos;
    }

    static uint16_t buildPuback(uint8_t* buf, uint16_t packetId) {
        buf[0] = 0x40; buf[1] = 0x02;
        buf[2] = (uint8_t)(packetId >> 8);
        buf[3] = (uint8_t)(packetId);
        return 4;
    }

    static uint16_t buildPingreq(uint8_t* buf) {
        buf[0] = 0xC0; buf[1] = 0x00;
        return 2;
    }

    static uint16_t buildDisconnect(uint8_t* buf) {
        buf[0] = 0xE0; buf[1] = 0x00;
        return 2;
    }

    // ---- parse ----

    /** Returns CONNACK return code (0 = accepted), or -1 on error. */
    static int parseConnack(const uint8_t* buf, uint16_t len) {
        if (len < 4 || packetType(buf[0]) != CONNACK) return -1;
        return buf[3];  // [0]=type [1]=remLen(2) [2]=ackFlags [3]=rc
    }

    /** Returns SUBACK granted QoS (0-2) or 0x80 failure, -1 on error. */
    static int parseSuback(const uint8_t* buf, uint16_t len) {
        if (len < 5 || packetType(buf[0]) != SUBACK) return -1;
        return buf[4];  // [0]=type [1]=remLen [2-3]=pktId [4]=rc
    }

    static bool isPingresp(const uint8_t* buf, uint16_t len) {
        return len >= 2 && packetType(buf[0]) == PINGRESP;
    }

    /** Parse PUBACK — returns packet ID, or 0 on error. */
    static uint16_t parsePuback(const uint8_t* buf, uint16_t len) {
        if (len < 4 || packetType(buf[0]) != PUBACK) return 0;
        return (buf[2] << 8) | buf[3];
    }

    /**
     * Parse incoming PUBLISH — zero-copy pointers into buf.
     * packetId is set only when QoS > 0; else 0.
     */
    static bool parsePublish(const uint8_t* buf, uint16_t bufLen,
                              const char*& topic, uint16_t& topicLen,
                              const uint8_t*& payload, uint16_t& payloadLen,
                              uint16_t& packetId) {
        if (bufLen < 5 || packetType(buf[0]) != PUBLISH) return false;
        uint8_t qos = (buf[0] >> 1) & 0x03;

        uint32_t remLen;
        uint16_t rlBytes = decodeRemLen(buf + 1, bufLen - 1, remLen);
        if (!rlBytes) return false;

        uint16_t hdrSz = 1 + rlBytes;
        uint16_t totalSz = hdrSz + (uint16_t)remLen;
        if (totalSz > bufLen) totalSz = bufLen;

        uint16_t pos = hdrSz;
        if (pos + 2 > totalSz) return false;
        topicLen = (buf[pos] << 8) | buf[pos + 1]; pos += 2;
        if (pos + topicLen > totalSz) return false;
        topic = (const char*)(buf + pos); pos += topicLen;

        packetId = 0;
        if (qos > 0) {
            if (pos + 2 > totalSz) return false;
            packetId = (buf[pos] << 8) | buf[pos + 1]; pos += 2;
        }

        payload = buf + pos;
        payloadLen = totalSz - pos;
        return true;
    }

    // ---- +IPD helper ----

    /**
     * Extract binary MQTT data from ESP8266 +IPD response.
     * Format: "+IPD,<len>:<binary>"
     */
    static bool parseIpd(const char* buf, uint16_t bufLen,
                          const uint8_t*& data, uint16_t& dataLen) {
        const char* end = buf + bufLen;
        for (const char* p = buf; p + 5 <= end; p++) {
            if (memcmp(p, "+IPD,", 5) != 0) continue;
            p += 5;
            dataLen = 0;
            while (p < end && *p >= '0' && *p <= '9') {
                dataLen = dataLen * 10 + (*p - '0');
                p++;
            }
            if (p < end && *p == ':') {
                p++;
                data = (const uint8_t*)p;
                uint16_t avail = (uint16_t)(end - p);
                if (dataLen > avail) dataLen = avail;
                return true;
            }
            return false;
        }
        return false;
    }
};

} // namespace arcana
