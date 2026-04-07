/**
 * @file test_mqtt_packet.cpp
 * @brief Header-only KAT suite for MqttPacket build/parse helpers.
 */
#include <gtest/gtest.h>
#include <cstring>
#include "MqttPacket.hpp"

using arcana::MqttPacket;

// ── encodeRemLen / decodeRemLen ────────────────────────────────────────────

TEST(MqttPacketRemLen, EncodeSmall) {
    uint8_t buf[4];
    EXPECT_EQ(MqttPacket::encodeRemLen(buf, 0),     1u);
    EXPECT_EQ(buf[0], 0x00);
    EXPECT_EQ(MqttPacket::encodeRemLen(buf, 127),   1u);
    EXPECT_EQ(buf[0], 0x7F);
    EXPECT_EQ(MqttPacket::encodeRemLen(buf, 128),   2u);
    EXPECT_EQ(buf[0], 0x80);
    EXPECT_EQ(buf[1], 0x01);
    EXPECT_EQ(MqttPacket::encodeRemLen(buf, 16383), 2u);
}

TEST(MqttPacketRemLen, EncodeLarge) {
    uint8_t buf[4];
    EXPECT_EQ(MqttPacket::encodeRemLen(buf, 16384),     3u);
    EXPECT_EQ(MqttPacket::encodeRemLen(buf, 2097151),   3u);
    EXPECT_EQ(MqttPacket::encodeRemLen(buf, 2097152),   4u);
    EXPECT_EQ(MqttPacket::encodeRemLen(buf, 268435455), 4u);
}

TEST(MqttPacketRemLen, DecodeSmall) {
    uint8_t buf[4] = {0x7F};
    uint32_t out = 0;
    EXPECT_EQ(MqttPacket::decodeRemLen(buf, 4, out), 1u);
    EXPECT_EQ(out, 127u);

    buf[0] = 0x00;
    EXPECT_EQ(MqttPacket::decodeRemLen(buf, 4, out), 1u);
    EXPECT_EQ(out, 0u);
}

TEST(MqttPacketRemLen, DecodeMultiByte) {
    uint8_t buf[4] = {0x80, 0x01};
    uint32_t out = 0;
    EXPECT_EQ(MqttPacket::decodeRemLen(buf, 4, out), 2u);
    EXPECT_EQ(out, 128u);

    /* 16384 = 0x80 0x80 0x01 */
    buf[0] = 0x80; buf[1] = 0x80; buf[2] = 0x01;
    EXPECT_EQ(MqttPacket::decodeRemLen(buf, 4, out), 3u);
    EXPECT_EQ(out, 16384u);
}

TEST(MqttPacketRemLen, DecodeMalformed) {
    /* All bytes have continuation bit set → 4-byte limit reached, malformed */
    uint8_t buf[4] = {0x80, 0x80, 0x80, 0x80};
    uint32_t out = 0;
    EXPECT_EQ(MqttPacket::decodeRemLen(buf, 4, out), 0u);
}

TEST(MqttPacketRemLen, RoundTrip) {
    uint8_t buf[4];
    uint32_t out = 0;
    for (uint32_t v : {0u, 1u, 127u, 128u, 16383u, 16384u, 200000u, 268435455u}) {
        uint16_t enc = MqttPacket::encodeRemLen(buf, v);
        uint16_t dec = MqttPacket::decodeRemLen(buf, 4, out);
        EXPECT_EQ(enc, dec);
        EXPECT_EQ(out, v);
    }
}

// ── packetType helper ──────────────────────────────────────────────────────

TEST(MqttPacketHelpers, PacketTypeMask) {
    EXPECT_EQ(MqttPacket::packetType(0x10), MqttPacket::CONNECT);
    EXPECT_EQ(MqttPacket::packetType(0x32), MqttPacket::PUBLISH); // QoS1 retain0
    EXPECT_EQ(MqttPacket::packetType(0x82), MqttPacket::SUBSCRIBE);
    EXPECT_EQ(MqttPacket::packetType(0xC0), MqttPacket::PINGREQ);
}

// ── buildConnect ───────────────────────────────────────────────────────────

TEST(MqttPacketBuild, ConnectMinimal) {
    uint8_t buf[64];
    uint16_t n = MqttPacket::buildConnect(buf, sizeof(buf), "client1", nullptr, nullptr);
    /* Minimal: type 0x10 + remLen + var hdr (10) + 2 + 7 client id */
    ASSERT_GT(n, 10u);
    EXPECT_EQ(buf[0], 0x10);
    /* var hdr: 00 04 'M' 'Q' 'T' 'T' 04 02 keepAlive(60=0x003C) */
    EXPECT_EQ(buf[2], 0);
    EXPECT_EQ(buf[3], 4);
    EXPECT_EQ(buf[4], 'M');
    EXPECT_EQ(buf[7], 'T');
    EXPECT_EQ(buf[8], 4);    /* protocol level */
    EXPECT_EQ(buf[9], 0x02); /* clean session, no user/pass */
    EXPECT_EQ(buf[10], 0);
    EXPECT_EQ(buf[11], 60);
}

TEST(MqttPacketBuild, ConnectWithUserPass) {
    uint8_t buf[128];
    uint16_t n = MqttPacket::buildConnect(buf, sizeof(buf),
        "device", "alice", "secret", 120);
    ASSERT_GT(n, 20u);
    EXPECT_EQ(buf[0], 0x10);
    /* flags = 0x02 | 0x80(user) | 0x40(pass) = 0xC2 */
    /* var hdr ends at offset 12 (1+remLenBytes+10). Find flags... */
    /* Quick smoke: keepAlive 120 in big-endian somewhere in first 14 bytes */
    bool foundKeepAlive = false;
    for (int i = 0; i < 14; ++i) {
        if (buf[i] == 0 && buf[i+1] == 120) { foundKeepAlive = true; break; }
    }
    EXPECT_TRUE(foundKeepAlive);
}

// ── buildPublish ───────────────────────────────────────────────────────────

TEST(MqttPacketBuild, PublishQos0) {
    uint8_t buf[64];
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t n = MqttPacket::buildPublish(buf, sizeof(buf),
        "test/topic", payload, sizeof(payload), 0, 0, false);
    /* type 0x30 (PUBLISH QoS0 retain0) */
    EXPECT_EQ(buf[0], 0x30);
    ASSERT_GE(n, 17u);
}

TEST(MqttPacketBuild, PublishQos1WithPacketId) {
    uint8_t buf[64];
    const uint8_t payload[] = {0xAA};
    uint16_t n = MqttPacket::buildPublish(buf, sizeof(buf),
        "t", payload, 1, /*qos*/1, /*pktId*/0x1234, /*retain*/true);
    /* type 0x30 | 0x02 (qos1) | 0x01 (retain) = 0x33 */
    EXPECT_EQ(buf[0], 0x33);
    /* Find packet ID 0x12 0x34 in buffer */
    bool found = false;
    for (uint16_t i = 0; i + 1 < n; ++i) {
        if (buf[i] == 0x12 && buf[i+1] == 0x34) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

// ── buildSubscribe / buildPuback / buildPingreq / buildDisconnect ─────────

TEST(MqttPacketBuild, Subscribe) {
    uint8_t buf[32];
    uint16_t n = MqttPacket::buildSubscribe(buf, sizeof(buf), 0xABCD,
        "sub/topic", 1);
    EXPECT_EQ(buf[0], 0x82);  /* SUBSCRIBE has fixed header byte 0x82 */
    /* Last byte = QoS = 1 */
    EXPECT_EQ(buf[n - 1], 1);
}

TEST(MqttPacketBuild, Puback) {
    uint8_t buf[8];
    uint16_t n = MqttPacket::buildPuback(buf, 0xCAFE);
    EXPECT_EQ(n, 4u);
    EXPECT_EQ(buf[0], 0x40);
    EXPECT_EQ(buf[1], 0x02);
    EXPECT_EQ(buf[2], 0xCA);
    EXPECT_EQ(buf[3], 0xFE);
}

TEST(MqttPacketBuild, Pingreq) {
    uint8_t buf[4];
    EXPECT_EQ(MqttPacket::buildPingreq(buf), 2u);
    EXPECT_EQ(buf[0], 0xC0);
    EXPECT_EQ(buf[1], 0x00);
}

TEST(MqttPacketBuild, Disconnect) {
    uint8_t buf[4];
    EXPECT_EQ(MqttPacket::buildDisconnect(buf), 2u);
    EXPECT_EQ(buf[0], 0xE0);
    EXPECT_EQ(buf[1], 0x00);
}

// ── parseConnack ───────────────────────────────────────────────────────────

TEST(MqttPacketParse, ConnackAccepted) {
    /* CONNACK: 0x20 0x02 ackFlags rc */
    uint8_t buf[4] = {0x20, 0x02, 0x00, 0x00};
    EXPECT_EQ(MqttPacket::parseConnack(buf, 4), 0);
}

TEST(MqttPacketParse, ConnackRejected) {
    uint8_t buf[4] = {0x20, 0x02, 0x00, 0x05};
    EXPECT_EQ(MqttPacket::parseConnack(buf, 4), 5);
}

TEST(MqttPacketParse, ConnackTooShortReturnsMinusOne) {
    uint8_t buf[3] = {0x20, 0x02, 0x00};
    EXPECT_EQ(MqttPacket::parseConnack(buf, 3), -1);
}

TEST(MqttPacketParse, ConnackWrongType) {
    uint8_t buf[4] = {0x30, 0x02, 0x00, 0x00};
    EXPECT_EQ(MqttPacket::parseConnack(buf, 4), -1);
}

// ── parseSuback ────────────────────────────────────────────────────────────

TEST(MqttPacketParse, SubackQos1Granted) {
    /* SUBACK: 0x90 remLen pktIdH pktIdL rc */
    uint8_t buf[5] = {0x90, 0x03, 0x00, 0x01, 0x01};
    EXPECT_EQ(MqttPacket::parseSuback(buf, 5), 1);
}

TEST(MqttPacketParse, SubackFailureCode) {
    uint8_t buf[5] = {0x90, 0x03, 0x00, 0x01, 0x80};
    EXPECT_EQ(MqttPacket::parseSuback(buf, 5), 0x80);
}

TEST(MqttPacketParse, SubackTooShort) {
    uint8_t buf[4] = {0x90, 0x03, 0x00, 0x01};
    EXPECT_EQ(MqttPacket::parseSuback(buf, 4), -1);
}

// ── isPingresp ─────────────────────────────────────────────────────────────

TEST(MqttPacketParse, PingrespHappy) {
    uint8_t buf[2] = {0xD0, 0x00};
    EXPECT_TRUE(MqttPacket::isPingresp(buf, 2));
}

TEST(MqttPacketParse, PingrespWrongType) {
    uint8_t buf[2] = {0xC0, 0x00};
    EXPECT_FALSE(MqttPacket::isPingresp(buf, 2));
}

TEST(MqttPacketParse, PingrespTooShort) {
    uint8_t buf[1] = {0xD0};
    EXPECT_FALSE(MqttPacket::isPingresp(buf, 1));
}

// ── parsePuback ────────────────────────────────────────────────────────────

TEST(MqttPacketParse, PubackPacketId) {
    uint8_t buf[4] = {0x40, 0x02, 0x12, 0x34};
    EXPECT_EQ(MqttPacket::parsePuback(buf, 4), 0x1234u);
}

TEST(MqttPacketParse, PubackWrongType) {
    uint8_t buf[4] = {0x30, 0x02, 0x12, 0x34};
    EXPECT_EQ(MqttPacket::parsePuback(buf, 4), 0u);
}

// ── parsePublish ───────────────────────────────────────────────────────────

TEST(MqttPacketParse, PublishQos0RoundTrip) {
    uint8_t buf[64];
    const uint8_t payload[] = {'h','e','l','l','o'};
    uint16_t n = MqttPacket::buildPublish(buf, sizeof(buf),
        "topic/x", payload, sizeof(payload));

    const char* topic = nullptr;
    uint16_t topicLen = 0;
    const uint8_t* pp = nullptr;
    uint16_t plen = 0;
    uint16_t pid = 0;
    EXPECT_TRUE(MqttPacket::parsePublish(buf, n, topic, topicLen, pp, plen, pid));
    EXPECT_EQ(topicLen, 7u);
    EXPECT_EQ(0, std::memcmp(topic, "topic/x", 7));
    EXPECT_EQ(plen, 5u);
    EXPECT_EQ(0, std::memcmp(pp, "hello", 5));
    EXPECT_EQ(pid, 0u);
}

TEST(MqttPacketParse, PublishQos1WithPacketId) {
    uint8_t buf[64];
    const uint8_t payload[] = {0xCA, 0xFE};
    uint16_t n = MqttPacket::buildPublish(buf, sizeof(buf),
        "t", payload, 2, 1, 0xABCD, false);

    const char* topic = nullptr;
    uint16_t topicLen = 0;
    const uint8_t* pp = nullptr;
    uint16_t plen = 0;
    uint16_t pid = 0;
    EXPECT_TRUE(MqttPacket::parsePublish(buf, n, topic, topicLen, pp, plen, pid));
    EXPECT_EQ(pid, 0xABCDu);
    EXPECT_EQ(plen, 2u);
}

TEST(MqttPacketParse, PublishWrongType) {
    uint8_t buf[8] = {0x40, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const char* topic = nullptr;
    uint16_t topicLen = 0;
    const uint8_t* pp = nullptr;
    uint16_t plen = 0;
    uint16_t pid = 0;
    EXPECT_FALSE(MqttPacket::parsePublish(buf, 8, topic, topicLen, pp, plen, pid));
}

TEST(MqttPacketParse, PublishTooShort) {
    uint8_t buf[3] = {0x30, 0x02, 0x00};
    const char* topic = nullptr;
    uint16_t topicLen = 0;
    const uint8_t* pp = nullptr;
    uint16_t plen = 0;
    uint16_t pid = 0;
    EXPECT_FALSE(MqttPacket::parsePublish(buf, 3, topic, topicLen, pp, plen, pid));
}

// ── parseIpd ───────────────────────────────────────────────────────────────

TEST(MqttPacketParse, IpdHappyPath) {
    /* "+IPD,5:hello\0" */
    char buf[16] = "+IPD,5:hello";
    const uint8_t* data = nullptr;
    uint16_t dlen = 0;
    EXPECT_TRUE(MqttPacket::parseIpd(buf, (uint16_t)std::strlen(buf), data, dlen));
    EXPECT_EQ(dlen, 5u);
    EXPECT_EQ(0, std::memcmp(data, "hello", 5));
}

TEST(MqttPacketParse, IpdWithLeadingPrefix) {
    char buf[24] = "AT+OK\r\n+IPD,3:abc";
    const uint8_t* data = nullptr;
    uint16_t dlen = 0;
    EXPECT_TRUE(MqttPacket::parseIpd(buf, (uint16_t)std::strlen(buf), data, dlen));
    EXPECT_EQ(dlen, 3u);
}

TEST(MqttPacketParse, IpdMissingColon) {
    char buf[16] = "+IPD,5 hello";
    const uint8_t* data = nullptr;
    uint16_t dlen = 0;
    EXPECT_FALSE(MqttPacket::parseIpd(buf, (uint16_t)std::strlen(buf), data, dlen));
}

TEST(MqttPacketParse, IpdNoMagic) {
    char buf[16] = "no ipd here";
    const uint8_t* data = nullptr;
    uint16_t dlen = 0;
    EXPECT_FALSE(MqttPacket::parseIpd(buf, (uint16_t)std::strlen(buf), data, dlen));
}

TEST(MqttPacketParse, IpdLengthExceedsAvailable) {
    /* len=100 but only "ab" follows → dataLen clamped to 2 */
    char buf[16] = "+IPD,100:ab";
    const uint8_t* data = nullptr;
    uint16_t dlen = 0;
    EXPECT_TRUE(MqttPacket::parseIpd(buf, (uint16_t)std::strlen(buf), data, dlen));
    EXPECT_EQ(dlen, 2u);
}
