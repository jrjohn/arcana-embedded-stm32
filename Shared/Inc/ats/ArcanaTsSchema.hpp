/**
 * @file ArcanaTsSchema.hpp
 * @brief Schema builder and predefined sensor/device schemas
 *
 * Header-only. Each schema defines field layout for one channel.
 * Factory methods return predefined schemas matching the .ats spec.
 */

#ifndef ARCANA_ATS_SCHEMA_HPP
#define ARCANA_ATS_SCHEMA_HPP

#include <cstdint>
#include <cstring>
#include "Crc32.hpp"
#include "ArcanaTsTypes.hpp"

namespace arcana {
namespace ats {

/** @brief Field descriptor (16 bytes, packed, matches on-disk field table) */
struct __attribute__((packed)) FieldDesc {
    char     name[8];       // null-terminated field name
    FieldType type;         // U8..BYTES
    uint8_t  offset;        // byte offset in record
    uint16_t scaleNum;      // scale numerator (or byte count for BYTES)
    uint16_t scaleDen;      // scale denominator
    uint16_t reserved;
};
static_assert(sizeof(FieldDesc) == 16, "FieldDesc must be 16 bytes");

/**
 * @brief Multi-channel schema definition
 *
 * Describes the field layout of one channel's records.
 * Use addField() to build custom schemas, or factory methods for predefined ones.
 */
class ArcanaTsSchema {
public:
    static const uint8_t MAX_FIELDS = 16;

    FieldDesc fields[MAX_FIELDS];
    uint8_t   fieldCount;
    uint16_t  recordSize;
    char      name[24];

    ArcanaTsSchema() : fieldCount(0), recordSize(0) {
        memset(fields, 0, sizeof(fields));
        memset(name, 0, sizeof(name));
    }

    /**
     * @brief Add a field to the schema
     * @param fieldName  Field name (max 7 chars + null)
     * @param type       Field data type
     * @param scaleNum   Scale numerator (or byte count for BYTES type)
     * @param scaleDen   Scale denominator
     * @return true on success, false if MAX_FIELDS reached
     */
    bool addField(const char* fieldName, FieldType type,
                  uint16_t scaleNum = 1, uint16_t scaleDen = 1) {
        if (fieldCount >= MAX_FIELDS) return false;
        FieldDesc& f = fields[fieldCount];
        memset(&f, 0, sizeof(FieldDesc));
        strncpy(f.name, fieldName, 7);
        f.name[7] = '\0';
        f.type = type;
        f.offset = static_cast<uint8_t>(recordSize);
        f.scaleNum = scaleNum;
        f.scaleDen = scaleDen;
        f.reserved = 0;
        recordSize += fieldSize(type, scaleNum);
        fieldCount++;
        return true;
    }

    /** @brief Set schema name (max 23 chars + null) */
    void setName(const char* schemaName) {
        strncpy(name, schemaName, 23);
        name[23] = '\0';
    }

    /** @brief Compute schema ID as CRC-32 of field descriptors */
    uint32_t schemaId() const {
        return ~crc32(0xFFFFFFFF,
                      reinterpret_cast<const uint8_t*>(fields),
                      static_cast<size_t>(fieldCount) * sizeof(FieldDesc));
    }

    /** @brief Records that fit in one block payload */
    uint16_t recordsPerBlock() const {
        if (recordSize == 0) return 0;
        return BLOCK_PAYLOAD_SIZE / recordSize;
    }

    // -----------------------------------------------------------------------
    // Predefined sensor schemas
    // -----------------------------------------------------------------------

    /** @brief ADS1298 8-channel ECG (28 bytes/record, 145 rec/block) */
    static inline ArcanaTsSchema ads1298_8ch() {
        ArcanaTsSchema s;
        s.setName("ADS1298_8CH");
        s.addField("ts",  FieldType::U32);
        s.addField("ch0", FieldType::I24);
        s.addField("ch1", FieldType::I24);
        s.addField("ch2", FieldType::I24);
        s.addField("ch3", FieldType::I24);
        s.addField("ch4", FieldType::I24);
        s.addField("ch5", FieldType::I24);
        s.addField("ch6", FieldType::I24);
        s.addField("ch7", FieldType::I24);
        return s;
    }

    /** @brief MPU6050 IMU (14 bytes/record, 290 rec/block) */
    static inline ArcanaTsSchema mpu6050() {
        ArcanaTsSchema s;
        s.setName("MPU6050");
        s.addField("ts",   FieldType::U32);
        s.addField("temp", FieldType::F32);
        s.addField("ax",   FieldType::I16);
        s.addField("ay",   FieldType::I16);
        s.addField("az",   FieldType::I16);
        return s;
    }

    /** @brief DHT11 temperature/humidity (8 bytes/record, 508 rec/block) */
    static inline ArcanaTsSchema dht11() {
        ArcanaTsSchema s;
        s.setName("DHT11");
        s.addField("ts",   FieldType::U32);
        s.addField("temp", FieldType::I16);
        s.addField("humi", FieldType::I16);
        return s;
    }

    /** @brief Device status (16 bytes/record, 254 rec/block) */
    static inline ArcanaTsSchema deviceStatus() {
        ArcanaTsSchema s;
        s.setName("DEVICE_STATUS");
        s.addField("ts",      FieldType::U32);
        s.addField("vbat",    FieldType::U16);
        s.addField("cpuTemp", FieldType::I16);
        s.addField("uptime",  FieldType::U32);
        s.addField("heap",    FieldType::U16);
        s.addField("errFlg",  FieldType::U16);
        return s;
    }

    /** @brief Generic ADC (8 bytes/record, 508 rec/block) */
    static inline ArcanaTsSchema genericAdc() {
        ArcanaTsSchema s;
        s.setName("GENERIC_ADC");
        s.addField("ts",    FieldType::U32);
        s.addField("value", FieldType::I32);
        return s;
    }

    /** @brief Pump cumulative snapshot (9 bytes/record, 451 rec/block) */
    static inline ArcanaTsSchema pump() {
        ArcanaTsSchema s;
        s.setName("PUMP");
        s.addField("ts",      FieldType::U32);
        s.addField("state",   FieldType::U8);
        s.addField("runtime", FieldType::U32);
        return s;
    }

    // -----------------------------------------------------------------------
    // Daily operational schemas
    // -----------------------------------------------------------------------

    /** @brief User action log (11 bytes/record, 369 rec/block) */
    static inline ArcanaTsSchema userAction() {
        ArcanaTsSchema s;
        s.setName("USER_ACTION");
        s.addField("ts",     FieldType::U32);
        s.addField("actTyp", FieldType::U8);
        s.addField("actCod", FieldType::U16);
        s.addField("param",  FieldType::U32);
        return s;
    }

    /** @brief Error/warning log (12 bytes/record, 338 rec/block) */
    static inline ArcanaTsSchema errorLog() {
        ArcanaTsSchema s;
        s.setName("ERROR_LOG");
        s.addField("ts",      FieldType::U32);
        s.addField("sev",     FieldType::U8);
        s.addField("src",     FieldType::U8);
        s.addField("errCod",  FieldType::U16);
        s.addField("param",   FieldType::U32);
        return s;
    }

    /** @brief Config snapshot (14 bytes/record, 290 rec/block) */
    static inline ArcanaTsSchema configSnapshot() {
        ArcanaTsSchema s;
        s.setName("CONFIG_SNAPSHOT");
        s.addField("ts",      FieldType::U32);
        s.addField("pressTg", FieldType::U16);
        s.addField("flowRt",  FieldType::U16);
        s.addField("tmrSec",  FieldType::U16);
        s.addField("thrHi",   FieldType::I16);
        s.addField("thrLo",   FieldType::I16);
        s.addField("mode",    FieldType::U8);
        s.addField("flags",   FieldType::U8);
        return s;
    }

    // -----------------------------------------------------------------------
    // Device lifecycle schemas (permanent device.ats)
    // -----------------------------------------------------------------------

    /** @brief Device identity (16 bytes/record, 254 rec/block) */
    static inline ArcanaTsSchema deviceInfo() {
        ArcanaTsSchema s;
        s.setName("DEVICE_INFO");
        s.addField("ts",      FieldType::U32);
        s.addField("fwMaj",   FieldType::U8);
        s.addField("fwMin",   FieldType::U8);
        s.addField("fwPat",   FieldType::U8);
        s.addField("hwRev",   FieldType::U8);
        s.addField("serLo",   FieldType::U32);
        s.addField("serHi",   FieldType::U32);
        return s;
    }

    /** @brief Lifecycle event (12 bytes/record, 338 rec/block) */
    static inline ArcanaTsSchema lifecycleEvent() {
        ArcanaTsSchema s;
        s.setName("LIFECYCLE");
        s.addField("ts",      FieldType::U32);
        s.addField("evtTyp",  FieldType::U8);
        s.addField("evtCod",  FieldType::U16);
        s.addField("rsv",     FieldType::U8);
        s.addField("param",   FieldType::U32);
        return s;
    }

    /** @brief Accumulated device counters (20 bytes/record, 203 rec/block) */
    static inline ArcanaTsSchema deviceCounters() {
        ArcanaTsSchema s;
        s.setName("COUNTERS");
        s.addField("ts",      FieldType::U32);
        s.addField("pwrHrs",  FieldType::U32);
        s.addField("pmpCyc",  FieldType::U32);
        s.addField("errCnt",  FieldType::U32);
        s.addField("sdWrMB",  FieldType::U32);
        return s;
    }

    /** @brief Operational config (17 bytes/record, 238 rec/block) */
    static inline ArcanaTsSchema config() {
        ArcanaTsSchema s;
        s.setName("CONFIG");
        s.addField("ts",      FieldType::U32);
        s.addField("pressTg", FieldType::U16);
        s.addField("flowRt",  FieldType::U16);
        s.addField("tmrSec",  FieldType::U16);
        s.addField("thrHi",   FieldType::I16);
        s.addField("thrLo",   FieldType::I16);
        s.addField("mode",    FieldType::U8);
        s.addField("flags",   FieldType::U8);
        s.addField("tzOff",   FieldType::I16);   // timezone offset in minutes (e.g. +480=UTC+8)
        s.addField("tzAuto",  FieldType::U8);    // 1=auto-check on boot, 0=don't ask
        return s;
    }

    /** @brief Encrypted credentials (236 bytes/record, 17 rec/block) */
    static inline ArcanaTsSchema credentials() {
        ArcanaTsSchema s;
        s.setName("CREDS");
        s.addField("ts",   FieldType::U32);          // 4 bytes
        s.addField("data", FieldType::BYTES, 232);    // [nonce:12][encrypted:220]
        return s;
    }

    /** @brief Sensor calibration (18 bytes/record, 225 rec/block) */
    static inline ArcanaTsSchema calibration() {
        ArcanaTsSchema s;
        s.setName("CALIBRATION");
        s.addField("ts",      FieldType::U32);
        s.addField("sensId",  FieldType::U8);
        s.addField("pad",     FieldType::U8);
        s.addField("gainN",   FieldType::U16);
        s.addField("offRaw",  FieldType::I32);
        s.addField("zeroPt",  FieldType::I32);
        s.addField("tmpCof",  FieldType::I16);
        s.addField("calOp",   FieldType::U8);
        s.addField("pad2",    FieldType::U8);
        return s;
    }

private:
    /** @brief Get byte size for a field type */
    static uint16_t fieldSize(FieldType type, uint16_t scaleNum = 1) {
        switch (type) {
            case FieldType::U8:    return 1;
            case FieldType::U16:   return 2;
            case FieldType::U32:   return 4;
            case FieldType::I16:   return 2;
            case FieldType::I32:   return 4;
            case FieldType::F32:   return 4;
            case FieldType::I24:   return 3;
            case FieldType::U64:   return 8;
            case FieldType::BYTES: return scaleNum;
            default:               return 0;
        }
    }
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_ATS_SCHEMA_HPP */
