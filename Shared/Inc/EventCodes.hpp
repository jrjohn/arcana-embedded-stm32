/**
 * @file EventCodes.hpp
 * @brief Structured event codes for ArcanaLog
 *
 * Each module owns a 256-code block (0xMM00-0xMMFF).
 * Codes replace format strings — smaller, faster, machine-parseable.
 */

#ifndef ARCANA_EVENT_CODES_HPP
#define ARCANA_EVENT_CODES_HPP

#include <cstdint>

namespace arcana {
namespace evt {

// ---------------------------------------------------------------------------
// System / Boot  (0x0000 - 0x00FF)
// ---------------------------------------------------------------------------
static const uint16_t SYS_BOOT_OK           = 0x0000;
static const uint16_t SYS_BOOT_RTC_RESTORE  = 0x0001;
static const uint16_t SYS_HEAP_LOW          = 0x0002;
static const uint16_t SYS_WATCHDOG_RESET    = 0x0003;
static const uint16_t SYS_ASSERT_FAIL       = 0x0004;
static const uint16_t SYS_TASK_CREATE_FAIL  = 0x0005;
static const uint16_t SYS_CLOCK_SYNC        = 0x0006;

// ---------------------------------------------------------------------------
// SDIO / SD Card  (0x0100 - 0x01FF)
// ---------------------------------------------------------------------------
static const uint16_t SDIO_INIT_OK          = 0x0100;
static const uint16_t SDIO_INIT_FAIL        = 0x0101;
static const uint16_t SDIO_REINIT           = 0x0102;
static const uint16_t SDIO_READ_FAIL        = 0x0103;
static const uint16_t SDIO_WRITE_FAIL       = 0x0104;
static const uint16_t SDIO_CARD_REMOVED     = 0x0105;
static const uint16_t SDIO_EXFAT_READY      = 0x0106;

// FatFsFilePort (0x0110 - 0x011F)
static const uint16_t SDIO_FP_OPEN_FAIL     = 0x0110;
static const uint16_t SDIO_FP_WRITE_FAIL    = 0x0111;
static const uint16_t SDIO_FP_SEEK_FAIL     = 0x0112;
static const uint16_t SDIO_FP_EXTEND_FAIL   = 0x0113;

// SdBenchmark (0x0120 - 0x012F)
static const uint16_t SDIO_FORMAT_START     = 0x0120;
static const uint16_t SDIO_FORMAT_OK        = 0x0121;
static const uint16_t SDIO_FORMAT_FAIL      = 0x0122;
static const uint16_t SDIO_MOUNT_ATTEMPT    = 0x0123;
static const uint16_t SDIO_MOUNT_CORRUPT    = 0x0124;
static const uint16_t SDIO_MOUNT_ERR        = 0x0125;
static const uint16_t SDIO_MOUNT_RETRY      = 0x0126;
static const uint16_t SDIO_MOUNT_FINAL_FMT  = 0x0127;
static const uint16_t SDIO_MOUNT_FAILED     = 0x0128;
static const uint16_t SDIO_CAPACITY         = 0x0129;
static const uint16_t SDIO_MOUNT_OK         = 0x012A;

// ---------------------------------------------------------------------------
// Sensor  (0x0200 - 0x02FF)
// ---------------------------------------------------------------------------
static const uint16_t SENS_READ_OK          = 0x0200;
static const uint16_t SENS_DHT_TIMEOUT      = 0x0201;
static const uint16_t SENS_DHT_CHECKSUM     = 0x0202;
static const uint16_t SENS_ADS_SPI_FAIL     = 0x0203;
static const uint16_t SENS_ADS_DRDY_MISS    = 0x0204;

// ---------------------------------------------------------------------------
// WiFi  (0x0300 - 0x03FF)
// ---------------------------------------------------------------------------
static const uint16_t WIFI_CONNECTED        = 0x0300;
static const uint16_t WIFI_DISCONNECTED     = 0x0301;
static const uint16_t WIFI_CWJAP_FAIL       = 0x0302;
static const uint16_t WIFI_AT_ERROR         = 0x0303;
static const uint16_t WIFI_TX_FAIL          = 0x0304;
static const uint16_t WIFI_RX_TIMEOUT       = 0x0305;
static const uint16_t WIFI_AT_NO_RESP       = 0x0310;
static const uint16_t WIFI_AT_OK            = 0x0311;
static const uint16_t WIFI_CWMODE_FAIL      = 0x0312;
static const uint16_t WIFI_CWMODE_OK        = 0x0313;
static const uint16_t WIFI_JOINING          = 0x0314;
static const uint16_t WIFI_GOT_IP           = 0x0315;
static const uint16_t WIFI_NTP_SYNC         = 0x0316;
static const uint16_t WIFI_NTP_EPOCH        = 0x0317;

// ---------------------------------------------------------------------------
// Pump  (0x0400 - 0x04FF)
// ---------------------------------------------------------------------------
static const uint16_t PUMP_START            = 0x0400;
static const uint16_t PUMP_STOP             = 0x0401;
static const uint16_t PUMP_STALL            = 0x0402;
static const uint16_t PUMP_OVERCURRENT      = 0x0403;

// ---------------------------------------------------------------------------
// Crypto  (0x0500 - 0x05FF)
// ---------------------------------------------------------------------------
static const uint16_t CRYPTO_KEY_DERIVED    = 0x0500;
static const uint16_t CRYPTO_ENCRYPT_FAIL   = 0x0501;
static const uint16_t CRYPTO_DECRYPT_FAIL   = 0x0502;

// ---------------------------------------------------------------------------
// ATS / TSDB  (0x0600 - 0x06FF)
// ---------------------------------------------------------------------------
static const uint16_t ATS_DB_OPEN_OK        = 0x0600;
static const uint16_t ATS_DB_OPEN_FAIL      = 0x0601;
static const uint16_t ATS_FLUSH_OK          = 0x0602;
static const uint16_t ATS_FLUSH_FAIL        = 0x0603;
static const uint16_t ATS_RECOVERY_OK       = 0x0604;
static const uint16_t ATS_RECOVERY_TRUNC    = 0x0605;
static const uint16_t ATS_ROTATE_OK         = 0x0606;
static const uint16_t ATS_ROTATE_FAIL       = 0x0607;
static const uint16_t ATS_WRITE_TEST_FAIL   = 0x0608;
static const uint16_t ATS_SDIO_REINIT       = 0x0609;
static const uint16_t ATS_CHANNEL_FAIL      = 0x060A;
static const uint16_t ATS_START_FAIL        = 0x060B;
static const uint16_t ATS_RECREATE          = 0x060C;
static const uint16_t ATS_READY             = 0x060D;
static const uint16_t ATS_DB_RETRY_FAIL     = 0x060E;
static const uint16_t ATS_DB_RECOVERED      = 0x060F;
static const uint16_t ATS_LIFECYCLE_ON      = 0x0610;
static const uint16_t ATS_LIFECYCLE_OFF     = 0x0611;
static const uint16_t ATS_LIFECYCLE_RECOV   = 0x0612;
static const uint16_t ATS_LIFECYCLE_FWUPD   = 0x0613;

// AtsStorageServiceImpl extra (0x0640 - 0x065F)
static const uint16_t ATS_SENSOR_OPENING   = 0x0640;
static const uint16_t ATS_SENSOR_DB_INFO   = 0x0641;
static const uint16_t ATS_DEVICE_DB_INFO   = 0x0642;
static const uint16_t ATS_SHUTDOWN         = 0x0643;
static const uint16_t ATS_SAFE_EJECT       = 0x0644;
static const uint16_t ATS_KEY2_RESUME      = 0x0645;
static const uint16_t ATS_REMOUNT_FAIL     = 0x0646;
static const uint16_t ATS_FORMAT_FAIL      = 0x0647;
static const uint16_t ATS_FORMAT_OK        = 0x0648;
static const uint16_t ATS_KEY1_FORMAT      = 0x0649;
static const uint16_t ATS_KEY2_EJECT       = 0x064A;

// ---------------------------------------------------------------------------
// ATS / TSDB (SdStorageServiceImpl)  (0x0620 - 0x063F)
// ---------------------------------------------------------------------------
static const uint16_t SDS_FAL_INIT          = 0x0620;
static const uint16_t SDS_FAL_FAIL          = 0x0621;
static const uint16_t SDS_FAL_OK            = 0x0622;
static const uint16_t SDS_TSDB_INIT         = 0x0623;
static const uint16_t SDS_TSDB_FAIL         = 0x0624;
static const uint16_t SDS_TSDB_OK           = 0x0625;
static const uint16_t SDS_TSDB_SEED         = 0x0626;
static const uint16_t SDS_KVDB_INIT         = 0x0627;
static const uint16_t SDS_KVDB_FAIL         = 0x0628;
static const uint16_t SDS_KVDB_OK           = 0x0629;
static const uint16_t SDS_READY             = 0x062A;
static const uint16_t SDS_WRITE_ERR         = 0x062B;
static const uint16_t SDS_RATE              = 0x062C;

// ---------------------------------------------------------------------------
// NTP  (0x0700 - 0x07FF)
// ---------------------------------------------------------------------------
static const uint16_t NTP_SYNC_OK           = 0x0700;
static const uint16_t NTP_TIMEOUT           = 0x0701;
static const uint16_t NTP_PARSE_FAIL        = 0x0702;

// ---------------------------------------------------------------------------
// MQTT  (0x0800 - 0x08FF)
// ---------------------------------------------------------------------------
static const uint16_t MQTT_CONNECTED        = 0x0800;
static const uint16_t MQTT_DISCONNECTED     = 0x0801;
static const uint16_t MQTT_PUB_OK           = 0x0802;
static const uint16_t MQTT_PUB_FAIL         = 0x0803;
static const uint16_t MQTT_SUB_OK           = 0x0804;

// ---------------------------------------------------------------------------
// BLE  (0x0900 - 0x09FF)
// ---------------------------------------------------------------------------
static const uint16_t BLE_AT_OK             = 0x0900;
static const uint16_t BLE_TIMEOUT           = 0x0901;
static const uint16_t BLE_CONNECTED         = 0x0902;
static const uint16_t BLE_DISCONNECTED      = 0x0903;

// HC-08 driver (0x0910 - 0x091F)
static const uint16_t BLE_HC08_INIT         = 0x0910;
static const uint16_t BLE_HC08_AT_OK        = 0x0911;
static const uint16_t BLE_HC08_NAME         = 0x0912;
static const uint16_t BLE_HC08_DATA_MODE    = 0x0913;

// BleServiceImpl (0x0920 - 0x092F)
static const uint16_t BLE_TRANSPORT_READY   = 0x0920;
static const uint16_t BLE_CMD_COUNT         = 0x0921;
static const uint16_t BLE_SESSION_GATE      = 0x0922;  // Rejected: no session
static const uint16_t BLE_SESSION_UP        = 0x0923;  // Session established
static const uint16_t BLE_SESSION_DOWN      = 0x0924;  // Session cleared

// ---------------------------------------------------------------------------
// OTA  (0x0A00 - 0x0AFF)
// ---------------------------------------------------------------------------
static const uint16_t OTA_START             = 0x0A00;
static const uint16_t OTA_COMPLETE          = 0x0A01;
static const uint16_t OTA_VERIFY_FAIL       = 0x0A02;
static const uint16_t OTA_DOWNLOAD_FAIL     = 0x0A03;
static const uint16_t OTA_APPLY_FAIL        = 0x0A04;
static const uint16_t OTA_HTTP_FAIL         = 0x0A10;
static const uint16_t OTA_TCP_FAIL          = 0x0A11;
static const uint16_t OTA_CIPSEND_FAIL      = 0x0A12;
static const uint16_t OTA_SEND_FAIL         = 0x0A13;
static const uint16_t OTA_FILE_CREATE_FAIL  = 0x0A14;
static const uint16_t OTA_CONN_CLOSED       = 0x0A15;
static const uint16_t OTA_DATA_TIMEOUT      = 0x0A16;
static const uint16_t OTA_HDR_TOO_LARGE     = 0x0A17;
static const uint16_t OTA_NOT_HTTP          = 0x0A18;
static const uint16_t OTA_HTTP_ERROR        = 0x0A19;
static const uint16_t OTA_PROGRESS          = 0x0A1A;
static const uint16_t OTA_DL_COMPLETE       = 0x0A1B;
static const uint16_t OTA_CRC_START         = 0x0A1C;
static const uint16_t OTA_CRC_RESULT        = 0x0A1D;
static const uint16_t OTA_META_FAIL         = 0x0A1E;
static const uint16_t OTA_FLAG_SET          = 0x0A1F;
static const uint16_t OTA_RESETTING         = 0x0A20;

// ---------------------------------------------------------------------------
// CMD  (0x0B00 - 0x0BFF)
// ---------------------------------------------------------------------------
static const uint16_t CMD_DISPATCH          = 0x0B00;
static const uint16_t CMD_UNKNOWN           = 0x0B01;
static const uint16_t CMD_DECODE_FAIL       = 0x0B02;
static const uint16_t CMD_REGISTERED        = 0x0B10;
static const uint16_t CMD_BRIDGE_START      = 0x0B11;
static const uint16_t CMD_BAD_FRAME         = 0x0B12;
static const uint16_t CMD_RX               = 0x0B13;
static const uint16_t CMD_RSP              = 0x0B14;

} // namespace evt
} // namespace arcana

#endif /* ARCANA_EVENT_CODES_HPP */
