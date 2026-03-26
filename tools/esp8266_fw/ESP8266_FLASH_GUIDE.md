# ESP8266 Firmware Flash Guide

## 硬體資訊

- **模組**: ESP8266EX (WROOM-02-N, **TX:1 RX:3**)
- **Flash**: **1MB**, mode: **dout**, freq: **26m**
- **板子**: 野火霸道 F103 V2
- **Crystal**: 26MHz
- **MAC**: 78:1c:3c:8b:96:5b

## ⚠️ 重要注意事項

### 1. WROOM-02 vs WROOM-02-N
- 野火板用的是 **WROOM-02-N**（TX:1 RX:3）
- 官方下載的 WROOM-02 firmware 預設 **TX:15 RX:13** → UART pin 不對！
- 必須用正確的 `factory_param.bin`（byte[0x10]=01 byte[0x11]=03）
- 驗證方式: `xxd factory_param.bin | head -2`，看 `0103` 還是 `0f0d`

### 2. Flash Size 1MB vs 2MB
- ESP8266 模組是 **1MB flash**
- 官方 v2.2.2.0 release 是 **2MB** 配置（partition table + address offsets 都是 2MB）
- **不能直接用** 2MB 的 partition-table.bin 燒到 1MB flash
- 必須用 1MB 版本的 partition table 和 address layout

### 3. Flash Mode
- 此模組用 **dout**（不是 dio）
- `--flash-mode dout --flash-freq 26m --flash-size 1MB`

### 4. Factory Image (2MB) 不可用
- `factory_WROOM-02.bin` 是 2MB 全合一 image
- esptool 會拒絕寫入: `will not fit in 1048576 bytes of flash`

## 燒錄方式: USB-TTL 直接燒

### 前置準備

1. **STM32 進入 idle mode**（避免 USART3 搶線）:
   - 在 `Controller.cpp` 用 `#ifdef ESP_FLASH_MODE` 跳過 WiFi/MQTT/BLE init
   - Compile with `-DESP_FLASH_MODE`
   - Flash STM32

2. **硬體接線**:
   - 拔掉 **J84** 跳帽（斷開 STM32 PB11 ↔ ESP8266 TX）
   - 拔掉 **J85** 跳帽（斷開 STM32 PB10 ↔ ESP8266 RX）
   - 短接 **J83 Pin 3-4**（GPIO0 = GND → bootloader mode）

   USB-TTL (Waveshare, **切 3V3**):
   ```
   USB-TTL TXD → J85 ESP8266 側 (WIFI_RXD)
   USB-TTL RXD → J84 ESP8266 側 (WIFI_TXD)
   USB-TTL GND → 板子任意 GND
   USB-TTL VCC → 不接（板子自己供電）
   ```

3. **板子上電**

### 燒錄指令

```bash
# 安裝 esptool
pip3 install esptool

# 測試連線
python3 -m esptool --port /dev/tty.usbserial-XXXX --baud 115200 chip-id

# 擦除（建議每次全燒前先擦）
python3 -m esptool --port /dev/tty.usbserial-XXXX --baud 115200 erase-flash
# ⚠️ erase 後需要重新上電（J83 短接）

# 燒錄完整 1MB firmware
DIR=tools/esp8266_fw/esp_fw
FP=tools/esp8266_fw/sd_esp_fw/factory_param.bin  # WROOM-02-N 版本

python3 -m esptool --port /dev/tty.usbserial-XXXX --baud 115200 \
  --no-stub --after no-reset \
  write-flash --flash-mode dout --flash-freq 26m --flash-size 1MB \
  0x0     $DIR/bootloader.bin \
  0x8000  $DIR/partitions.bin \
  0x9000  $DIR/ota_data.bin \
  0x18000 $DIR/at_customize.bin \
  0x19000 $FP \
  0x1a000 $DIR/client_cert.bin \
  0x1b000 $DIR/client_key.bin \
  0x1c000 $DIR/client_ca.bin \
  0x1d000 $DIR/mqtt_cert.bin \
  0x1e000 $DIR/mqtt_key.bin \
  0x1f000 $DIR/mqtt_ca.bin \
  0x20000 $DIR/esp-at.bin
```

### 1MB Partition Address Map

| Offset | File | 說明 |
|--------|------|------|
| 0x00000 | bootloader.bin | ESP8266 bootloader |
| 0x08000 | partitions.bin | Partition table (1MB layout) |
| 0x09000 | ota_data.bin | OTA data |
| 0x18000 | at_customize.bin | AT customize |
| 0x19000 | factory_param.bin | **WROOM-02-N (TX:1 RX:3)** |
| 0x1A000 | client_cert.bin | TLS client cert |
| 0x1B000 | client_key.bin | TLS client key |
| 0x1C000 | client_ca.bin | TLS client CA |
| 0x1D000 | mqtt_cert.bin | MQTT TLS cert |
| 0x1E000 | mqtt_key.bin | MQTT TLS key |
| 0x1F000 | mqtt_ca.bin | MQTT TLS CA |
| 0x20000 | esp-at.bin | AT firmware main binary |

### 燒錄後恢復

1. 拔掉 J83 短接線
2. 插回 J84、J85 跳帽
3. 拔掉 USB-TTL
4. 重新 compile STM32（不帶 `-DESP_FLASH_MODE`）
5. Flash STM32 正常 firmware
6. 上電 → ESP8266 預設 115200 → STM32 auto fallback → AT+UART_DEF=460800

## 燒錄方式: SD 卡 EspFlasher

### 步驟

1. SD 卡根目錄建 `esp_fw/` 資料夾
2. 複製所有 `.bin` 到 `esp_fw/`（檔名必須完全符合上表）
3. 短接 J83 Pin 3-4
4. 上電 → EspFlasher 自動偵測 → 燒錄
5. Serial 看到 `[ESPFW] === ESP8266 Firmware Update ===`
6. 等完成後刪除 SD 卡上的 `esp_fw/`
7. 拔掉 J83，重新上電

### 注意
- EspFlasher 用 USART3（PB10/PB11）通訊，其他 task 可能干擾
- 如果燒錄卡住，建議用 USB-TTL 方式
- `factory_param.bin` 如果 SD 卡上沒有，EspFlasher 會跳過 → 保留 ESP8266 原有設定

## 常見問題

### Q: esptool 連不上 ESP8266
- 確認 J83 Pin 3-4 短接
- 確認 STM32 在 ESP_FLASH_MODE（不驅動 USART3）
- 確認 J84/J85 拔掉（USB-TTL 直連 ESP8266）
- 確認 USB-TTL 切 3V3
- 重新上電再試

### Q: 大檔案 (esp-at.bin) 燒到一半斷
- 加 `--no-stub` 參數（穩定但慢）
- 確保 GND 接觸良好
- 拔掉 USB-TTL 的 RTS/CTS 線（只接 TXD/RXD/GND）

### Q: 燒完後 AT 指令沒回應
- 確認 factory_param.bin 是 WROOM-02-N 版本（TX:1 RX:3）
- 確認 partition table 是 1MB 版本
- 確認 flash mode 是 dout（不是 dio）
- 嘗試 erase-flash 後重燒

### Q: WiFi 連不上
- 新 firmware 預設 115200 baud，STM32 會 auto fallback + AT+UART_DEF=460800
- 第一次可能需要兩次重開機（第一次寫入 UART_DEF，第二次生效）

## 檔案位置

```
tools/esp8266_fw/
├── esp_fw/                    # 可直接燒的 bin 檔（缺 factory_param）
├── sd_esp_fw/                 # SD 卡用（含 factory_param WROOM-02-N）
├── esp8266_1mb_at/            # 完整 1MB AT v2.2.1 release
├── ESP-WROOM-02-AT-V2.2.1.0/ # 官方 v2.2.1 release（2MB, WROOM-02）
└── ESP8266_FLASH_GUIDE.md     # 本文件
```

## 自訂 firmware 編譯（SSL_IN 修改）

ESP-AT source: `../esp-at-esp8266/` (v2.2.2.0_esp8266 branch)

```bash
# Docker build (x86 cross-compile on Apple Silicon)
cd esp-at-esp8266
# 修改 module_config/module_esp8266_1mb/sdkconfig.defaults:
#   CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096  (原 16384)
# 修改 module_info.json: module=ESP8266_1MB

docker build --platform linux/amd64 -f Dockerfile.build -t esp-at-builder .
docker run --platform linux/amd64 --rm -v "$(pwd):/esp-at" -w /esp-at \
  esp-at-builder bash -c 'printf "2\n3\n0\n" | python3 build.py build'
```

⚠️ **注意**: Docker build 可能選錯 module（顯示 WROOM-02 而非 ESP8266_1MB）。
確認 `module_info.json` 設定正確後再 build。
Build 輸出的 address offset 可能是 2MB layout — 燒錄時仍需用 1MB address map。
