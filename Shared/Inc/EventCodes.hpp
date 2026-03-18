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

// ---------------------------------------------------------------------------
// OTA  (0x0A00 - 0x0AFF)
// ---------------------------------------------------------------------------
static const uint16_t OTA_START             = 0x0A00;
static const uint16_t OTA_COMPLETE          = 0x0A01;
static const uint16_t OTA_VERIFY_FAIL       = 0x0A02;
static const uint16_t OTA_DOWNLOAD_FAIL     = 0x0A03;
static const uint16_t OTA_APPLY_FAIL        = 0x0A04;

// ---------------------------------------------------------------------------
// CMD  (0x0B00 - 0x0BFF)
// ---------------------------------------------------------------------------
static const uint16_t CMD_DISPATCH          = 0x0B00;
static const uint16_t CMD_UNKNOWN           = 0x0B01;
static const uint16_t CMD_DECODE_FAIL       = 0x0B02;

} // namespace evt
} // namespace arcana

#endif /* ARCANA_EVENT_CODES_HPP */
