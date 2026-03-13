<p align="center">
  <img src="https://img.shields.io/badge/Architecture-Multi--Target_Embedded-gold?style=for-the-badge" alt="Architecture">
  <img src="https://img.shields.io/badge/MCU-STM32F1_/_F0-03234B?style=for-the-badge&logo=stmicroelectronics" alt="STM32">
  <img src="https://img.shields.io/badge/RTOS-FreeRTOS-00A86B?style=for-the-badge" alt="FreeRTOS">
  <img src="https://img.shields.io/badge/Language-C++14-00599C?style=for-the-badge&logo=cplusplus" alt="C++">
  <img src="https://img.shields.io/badge/License-MIT-green?style=for-the-badge" alt="License">
</p>

<h1 align="center">Arcana Embedded STM32</h1>

<p align="center">
  <strong>Multi-target embedded data acquisition platform with encrypted SD storage, WiFi/MQTT telemetry, and layered service architecture on FreeRTOS</strong>
</p>

<p align="center">
  <a href="#architecture">Architecture</a> &bull;
  <a href="#targets">Targets</a> &bull;
  <a href="#data-pipeline">Data Pipeline</a> &bull;
  <a href="#features">Features</a> &bull;
  <a href="#getting-started">Getting Started</a> &bull;
  <a href="#architecture-evaluation">Evaluation</a>
</p>

---

## Architecture

### System Overview

```
                          ┌─────────────────────────────────────┐
                          │          CLOUD / BACKEND            │
                          │     MQTT Broker  ←  Dashboard      │
                          └──────────────▲──────────────────────┘
                                         │ MQTT publish
                          ┌──────────────┴──────────────────────┐
                          │        ESP8266 WiFi Module          │
                          │   AT commands · NTP · MQTT client   │
                          └──────────────▲──────────────────────┘
                                         │ UART
┌────────────────────────────────────────┼────────────────────────────────────┐
│                        STM32F103ZET6 (Cortex-M3, 72 MHz)                   │
│                                                                            │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ Controller  │  │  LCD Service │  │ WiFi Service │  │ MQTT Service  │  │
│  │  (init/run) │  │  ILI9341 TFT │  │ ESP8266 AT   │  │  pub/sub      │  │
│  └──────┬──────┘  └──────────────┘  └──────┬───────┘  └───────────────┘  │
│         │                                   │ NTP epoch                    │
│         ▼                                   ▼                             │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │   Sensor    │  │  SD Storage  │  │ SystemClock  │  │  RTC Driver   │  │
│  │   Service   │──▶│  Service     │  │  (ms epoch)  │◀─│  (LSE+VBAT)  │  │
│  │ DHT11/ADC   │  │ FlashDB TSDB │  └──────────────┘  └───────────────┘  │
│  └─────────────┘  └──────┬───────┘                                        │
│                          │ ChaCha20-encrypted records                     │
│                          ▼                                                │
│  ┌──────────────────────────────────────────────────────────────────────┐ │
│  │                    SD Card (exFAT, SDIO DMA)                         │ │
│  │  tsdb.fdb (auto-grow, daily rotation) │ kvdb.fdb │ tsdb_YYYYMMDD.db │ │
│  └──────────────────────────────────────────────────────────────────────┘ │
│                                                                            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ littlefs     │  │ Flash Block  │  │ LED Service  │  │ Light Service │  │
│  │ (int. flash) │  │  Device      │  │  RGB PWM     │  │  AP3216C I2C  │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  └───────────────┘  │
│                                                                            │
├────────────────────────────────────────────────────────────────────────────┤
│                           FreeRTOS Kernel                                  │
├────────────────────────────────────────────────────────────────────────────┤
│  STM32F103ZET6 · 64KB RAM · 512KB Flash · FSMC · SDIO · I2C · USART      │
└────────────────────────────────────────────────────────────────────────────┘
```

### Data Pipeline (Sensor → SD → Cloud)

```
ADC / DHT11                    SdStorageService                    SD Card
  │ raw samples                     │                                │
  ▼                                 ▼                                │
SensorService ──▶ SensorDataModel ──▶ ChaCha20 encrypt ──▶ FlashDB TSDB append
                                     │  26-byte record:              │
                                     │  [nonce:12][ciphertext:14]    │
                                     │                               │
                                     │  64-bit ms epoch timestamp    │
                                     │  (supports 1KHz+ sample rate) │
                                     ▼                               │
                              Daily Rotation                         │
                              (midnight UTC)                         │
                                     │                               │
                                     ├── deinit current tsdb.fdb     │
                                     ├── rename → tsdb_YYYYMMDD.db   │
                                     └── create fresh tsdb.fdb       │
                                                                     │
MqttService ◀── read + decrypt ◀── iterate TSDB ◀───────────────────┘
     │
     ▼
MQTT publish → Cloud
```

---

## Targets

This is a mono-repo supporting multiple STM32 targets with shared code.

| | STM32F051C8 | STM32F103ZET6 |
|---|---|---|
| **Core** | Cortex-M0, 48 MHz | Cortex-M3, 72 MHz |
| **RAM / Flash** | 8 KB / 64 KB | 64 KB / 512 KB |
| **Board** | Generic dev board | 野火霸道 V2 |
| **Role** | Observable + Command demo | Full data acquisition platform |
| **Storage** | None | SD card (SDIO DMA) + littlefs (internal flash) |
| **Display** | None | 3.2" ILI9341 TFT (FSMC) |
| **Connectivity** | None | ESP8266 WiFi (NTP + MQTT) |
| **Encryption** | None | ChaCha20 (RFC 7539) |
| **Sensors** | Timer-based demo | DHT11, AP3216C, MPU6050, ADC simulator |
| **RTC** | None | LSE 32.768 KHz + CR1220 VBAT backup |
| **Build output** | text ~22 KB, bss ~5.5 KB | text ~111 KB, bss ~49 KB |

### Repository Structure

```
arcana-embedded-stm32/
├── Shared/
│   ├── Inc/                    # Observable, Models, FrameCodec, Crc16, CommandTypes
│   └── Src/                    # Observable.cpp
├── Targets/
│   ├── stm32f051c8/            # F051 CubeIDE project
│   │   ├── Core/               # HAL, FreeRTOS config, startup
│   │   ├── Services/
│   │   │   ├── controller/     # App.cpp
│   │   │   ├── service/        # TimerService, CounterService, TimeDisplayService
│   │   │   └── command/        # CommandService, CommandCodec, CommandDispatcher
│   │   │       └── impl/       # PingCommand, GetCounterCommand
│   │   └── Debug/              # Build output
│   └── stm32f103ze/            # F103 CubeIDE project
│       ├── Core/               # HAL, FreeRTOS config, startup
│       ├── Services/
│       │   ├── controller/     # Controller.hpp/.cpp, F103App.cpp
│       │   ├── service/        # Interfaces: ITimerService, LcdService, SensorService, ...
│       │   │   └── impl/       # Implementations: LcdServiceImpl, SdStorageServiceImpl, ...
│       │   ├── driver/         # HW drivers: Ili9341Lcd, SdCard, SdFalAdapter, Esp8266, ...
│       │   ├── model/          # F103Models.hpp
│       │   └── common/         # ChaCha20, DeviceKey, Font5x7, SystemClock
│       ├── Middlewares/        # FatFs, FlashDB, littlefs
│       └── Debug/              # Build output
└── read_serial.py              # Serial monitor (auto-reconnect)
```

---

## Features

### F103 Platform Features

| Feature | Details |
|---------|---------|
| **Encrypted SD Storage** | ChaCha20 per-record encryption, per-device key derived from silicon UID |
| **FlashDB TSDB** | Time-series database on SD card with 64-bit ms epoch timestamps |
| **Virtual FAL Adapter** | RAM bitmap + fake sector headers → ~50 ms init (vs 14 s full format) |
| **Auto-Growing TSDB** | File extends on first write to each sector (no pre-allocation) |
| **Daily TSDB Rotation** | Midnight UTC: deinit → rename → create fresh file |
| **KVDB Upload Tracking** | Per-day upload status in FlashDB key-value store |
| **SDIO DMA Writes** | 24 MHz 4-bit bus, async double-buffered writes |
| **Polling Reads** | Avoids DMA direction switching bug on STM32F103 shared DMA channel |
| **WiFi (ESP8266)** | AT command driver, auto-reconnect |
| **NTP Time Sync** | Raw UDP to pool.ntp.org, seeds SystemClock + hardware RTC |
| **Hardware RTC** | LSE crystal + CR1220 VBAT backup, time survives power cycles |
| **MQTT Telemetry** | Publish sensor data to cloud broker |
| **ILI9341 LCD** | 240x320 TFT via FSMC, real-time sensor display |
| **littlefs** | Internal flash storage (128 KB partition, 64 blocks x 2 KB) |
| **Static Allocation** | No malloc/new, all memory pre-allocated, FreeRTOS static tasks |

### Shared Framework (F051 + F103)

| Feature | Details |
|---------|---------|
| **Observable Pattern** | Type-safe `Observable<T>` with dual priority queue (high + normal) |
| **ISR-Safe API** | `publishFromISR()` for interrupt contexts |
| **Command Pattern** | `ICommand` interface + `CommandRegistry` + cluster-based routing |
| **Wire Protocol** | CRC-16 framed binary protocol, ESP32-compatible |
| **Zero-Copy Events** | Model pointers passed directly, no cloning |

---

## Key Design Decisions

### Storage Architecture

**Virtual FAL (Flash Abstraction Layer) on SD Card**

FlashDB expects NOR-flash semantics (erase-before-write, fixed partitions). SD cards don't work that way. `SdFalAdapter` bridges this gap:

- Maps FlashDB `fal_flash_ops` (read/write/erase) to FatFS file operations
- RAM bitmap tracks which 4 KB sectors have been materialized to disk
- Unmaterialized sectors return fake FlashDB headers (magic + STORE_EMPTY status)
- `fdb_tsdb_init` sees a "clean" partition instantly — no `format_all` scan
- Sectors auto-extend the file on first write via `f_lseek` FSIZE expansion

**Why not just use FatFS files directly?**

FlashDB provides append-only time-series semantics, automatic sector management, and iterator-based queries — reimplementing these on raw files would be more code and more bugs.

### Encryption

**ChaCha20 (not AES)**

- STM32F103 has no AES hardware accelerator
- ChaCha20 is pure arithmetic (add/xor/rotate) — fast on Cortex-M3 without crypto coprocessor
- RFC 7539 compliant, header-only implementation
- Per-device keys derived from silicon UID via `DeviceKey.hpp`
- 12-byte nonce: `[counter:4 LE][tick:4 LE][0x00:4]`

### Time Management

**Dual clock sources:**

1. **Hardware RTC** (LSE 32.768 KHz + CR1220 battery) — survives power cycles, provides epoch-second accuracy immediately at boot
2. **NTP sync** — corrects RTC drift, provides network-accurate time, updates hardware RTC

`SystemClock` singleton maintains tick-offset calculation for millisecond precision between NTP syncs.

### SDIO DMA Workaround

STM32F103 shares DMA2 Channel 4 for both SDIO TX and RX. Mixing DMA read + write operations breaks the DATAEND interrupt. Solution:

- **Writes**: DMA at 24 MHz (CLKDIV=1) — high throughput for TSDB appends
- **Reads**: Polling at ~3.8 MHz (CLKDIV=17) — avoids DMA direction switching entirely
- `SDIO->DCTRL = 0` required after every polling read (HAL leaves DTEN=1)

---

## Architecture Evaluation

### Strengths

| Strength | Details |
|----------|---------|
| **End-to-end encryption** | Every sensor record encrypted at rest with per-device ChaCha20 key; no plaintext on SD card |
| **Resilient time keeping** | RTC + NTP dual source ensures valid timestamps even without network |
| **Fast TSDB init** | Virtual sector headers avoid full-disk scan — ~50 ms cold start vs 14+ seconds |
| **Daily rotation** | Automatic file rotation prevents unbounded growth; old files ready for batch upload |
| **Layered architecture** | Clear separation: driver → service → controller; interfaces decouple implementation |
| **Static allocation** | Zero heap fragmentation; deterministic memory usage; suitable for long-running deployments |
| **Multi-target mono-repo** | Shared Observable/Command/Protocol code across MCU families |
| **Transport-agnostic protocol** | CRC-16 framed binary protocol works over UART, SPI, or BLE |
| **SDIO workaround** | Polling reads + DMA writes cleanly solve the shared-channel hardware bug |

### Weaknesses & Risks

| Weakness | Impact | Mitigation |
|----------|--------|------------|
| **No authentication on MQTT** | Data in transit is unprotected | TLS would require more RAM; current scope is LAN/demo |
| **ChaCha20 without MAC** | Encryption without integrity check; tampered ciphertext decrypts to garbage | Add Poly1305 MAC (RFC 7539 AEAD) when record format changes |
| **Single-threaded storage** | TSDB writes block the sensor task; a slow SD card stalls sampling | Acceptable at 10 writes/sec; for 1 KHz, needs dedicated storage task |
| **No wear-leveling awareness** | FlashDB erases sectors sequentially; SD card's internal FTL handles wear | SD FTL is sufficient for exFAT; not an issue in practice |
| **64 KB RAM ceiling** | ~49 KB BSS used (75%); limited room for new features | Optimize buffers or move to STM32F4 for next iteration |
| **ESP8266 AT command fragility** | AT parser is line-based; long responses or firmware bugs can desync | Watchdog + auto-reconnect implemented; consider ESP32 with native MQTT |
| **No OTA update** | Firmware update requires physical access | Bootloader + SD card OTA is feasible but not yet implemented |
| **Fixed observer limit** | Max 4 observers per Observable | Sufficient for current use; increase if needed |
| **No data upload retry** | If MQTT publish fails, data stays on SD but isn't automatically retried | KVDB tracks upload status; retry logic is planned |

### Resource Usage (F103)

```
   text    data     bss     dec     hex  filename
 113824     156   50384  164364   2820c  arcana-embedded-f103.elf

Flash: 113,980 / 524,288 bytes (21.7%)  ██░░░░░░░░  headroom for features
RAM:    50,540 /  65,536 bytes (77.1%)  ████████░░  monitor if adding buffers
```

---

## Getting Started

### Prerequisites

- [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html) 1.13+
- ARM toolchain (included with CubeIDE)
- OpenOCD 0.12+ (for command-line flashing)
- CMSIS-DAP debugger (e.g., fireDAP)

### Build

```bash
# F103 target
cd Targets/stm32f103ze/Debug
make -j8

# F051 target
cd Targets/stm32f051c8/Debug
make -j8
```

### Flash

```bash
# F103 via OpenOCD + CMSIS-DAP
openocd -f interface/cmsis-dap.cfg \
        -c "transport select swd" \
        -f target/stm32f1x.cfg \
        -c "program arcana-embedded-f103.elf verify reset exit"
```

### Serial Monitor

```bash
python3 read_serial.py
# Connects to /dev/tty.usbserial-1120 at 115200 baud
# Auto-reconnects on USB hiccups (board reset)
```

---

## F051 Observable + Command Architecture

The F051 target demonstrates the shared framework layers:

```
┌──────────────────────────────────────────────────────────────────┐
│                      APPLICATION LAYER                           │
│  TimerService (publisher) → CounterService / TimeDisplayService  │
├──────────────────────────────────────────────────────────────────┤
│               OBSERVABLE LAYER (Dual Priority Queue)             │
│         HIGH (4 slots)  >>  NORMAL (8 slots)                     │
│                    Dispatcher Task (128 words)                    │
├──────────────────────────────────────────────────────────────────┤
│                        COMMAND LAYER                              │
│  CommandService → Dispatcher → Registry → ICommand               │
│  (PingCommand, GetCounterCommand)                                │
├──────────────────────────────────────────────────────────────────┤
│                       PROTOCOL LAYER                              │
│  CommandCodec → FrameCodec → Crc16 (polynomial 0x8408)           │
│  Wire-compatible with ESP32 (same frame format + CRC)            │
├──────────────────────────────────────────────────────────────────┤
│                      FreeRTOS Kernel                              │
├──────────────────────────────────────────────────────────────────┤
│              STM32F051C8 (8 KB RAM / 64 KB Flash)                │
└──────────────────────────────────────────────────────────────────┘
```

### Wire Frame Format

```
┌───────┬───────┬─────┬───────┬─────┬──────────┬──────────┬──────┐
│ 0xAC  │ 0xDA  │ Ver │ Flags │ SID │ Len (LE) │ Payload  │ CRC  │
│       │       │ 0x01│ FIN   │     │ 2 bytes  │ N bytes  │ (LE) │
└───────┴───────┴─────┴───────┴─────┴──────────┴──────────┴──────┘
                    7-byte header              2-byte CRC = 9 bytes overhead
```

---

## Middleware & Third-Party

| Library | Version | Usage |
|---------|---------|-------|
| FreeRTOS | 10.x | RTOS kernel, static allocation |
| FlashDB | 2.1.99 | TSDB + KVDB on SD card via FAL adapter |
| FatFs | R0.15 | exFAT filesystem on SD card |
| littlefs | 2.9 | Internal flash filesystem (128 KB) |

---

## Roadmap

- [x] Observable pattern with dual priority queue
- [x] Command pattern with wire protocol (ESP32-compatible)
- [x] F103 target with FSMC LCD, SDIO SD card
- [x] ChaCha20 per-record encryption
- [x] FlashDB TSDB with virtual FAL adapter
- [x] 64-bit millisecond timestamps (1 KHz+ ADC support)
- [x] Hardware RTC with VBAT backup
- [x] WiFi (ESP8266) with NTP time sync
- [x] MQTT telemetry
- [x] Daily TSDB rotation
- [ ] ADS1298 8-channel 24-bit ADC integration (hardware)
- [ ] Poly1305 MAC for authenticated encryption
- [ ] MQTT upload retry with KVDB tracking
- [ ] SD card OTA firmware update
- [ ] Power management / low-power modes

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
