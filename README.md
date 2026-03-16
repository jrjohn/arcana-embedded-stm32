<p align="center">
  <img src="https://img.shields.io/badge/Architecture-MVVM_Embedded-gold?style=for-the-badge" alt="Architecture">
  <img src="https://img.shields.io/badge/MCU-STM32F051_/_F103-03234B?style=for-the-badge&logo=stmicroelectronics" alt="STM32">
  <img src="https://img.shields.io/badge/RTOS-FreeRTOS-00A86B?style=for-the-badge" alt="FreeRTOS">
  <img src="https://img.shields.io/badge/Language-C++14-00599C?style=for-the-badge&logo=cplusplus" alt="C++">
  <img src="https://img.shields.io/badge/License-MIT-green?style=for-the-badge" alt="License">
</p>

<h1 align="center">Arcana Embedded STM32</h1>

<p align="center">
  <strong>Multi-target embedded platform with MVVM architecture, ArcanaTS time-series database, and Observable pub/sub pattern for STM32 microcontrollers</strong>
</p>

<p align="center">
  <a href="#targets">Targets</a> •
  <a href="#architecture">Architecture</a> •
  <a href="#directory-structure">Structure</a> •
  <a href="#features">Features</a> •
  <a href="#build">Build</a> •
  <a href="#pros--cons">Pros & Cons</a>
</p>

---

## Targets

| Target | MCU | RAM | Flash | Features |
|--------|-----|-----|-------|----------|
| **STM32F051C8** | Cortex-M0, 48MHz | 8KB | 64KB | Observable + Command Pattern + Wire Protocol |
| **STM32F103ZET6** | Cortex-M3, 72MHz | 64KB | 512KB | MVVM LCD + ArcanaTS SD + ECG + WiFi/MQTT |

### F103 Board: 野火霸道 V2

- 3.2" ILI9341 TFT LCD (240x320, FSMC)
- 32GB SD card (SDIO 4-bit, exFAT)
- ESP8266 WiFi (AT commands, NTP)
- DHT11 temperature, MPU6050 IMU, AP3216C light
- fireDAP CMSIS-DAP debugger

---

## Architecture

### MVVM Pattern (F103)

```
┌─────────────────────────────────────────────────────────┐
│  Controller::wireViews()                                │
│                                                         │
│  // ViewModel ← Service outputs                         │
│  sViewModel.input.SensorData   = SensorService.output   │
│  sViewModel.input.StorageStats = AtsStorage.output      │
│  sViewModel.input.BaseTimer    = TimerService.output    │
│                                                         │
│  // View ← ViewModel + LCD hardware                     │
│  sMainView.input.viewModel = &sViewModel                │
│  sMainView.input.lcd       = &LcdService.getLcd()       │
└─────────────────────────────────────────────────────────┘
```

**Dependency direction: `View → ViewModel → Service`**

```
┌──────────┐     ┌──────────────┐     ┌─────────────────┐
│  View    │────▶│  ViewModel   │────▶│    Service       │
│ MainView │     │ LcdViewModel │     │ SensorService    │
│          │     │              │     │ TimerService     │
│ render() │◀────│ dirty flags  │◀────│ AtsStorageService│
│ ECG queue│     │ Observable   │     │ SdBenchService   │
│ LCD mutex│     │ callbacks    │     │ WifiService      │
└──────────┘     └──────────────┘     └─────────────────┘
     │                                        │
     ▼                                        ▼
┌──────────┐                          ┌─────────────────┐
│  Driver  │                          │    Common        │
│Ili9341Lcd│                          │ ChaCha20, Clock  │
│ SdCard   │                          │ DeviceKey, Font  │
└──────────┘                          └─────────────────┘
```

### Observable Pattern (Shared)

```
Service.publish(model)
    │
    ▼
ObservableDispatcher (dual priority queue: 8 normal + 4 high)
    │
    ├──▶ ViewModel.onSensorData()  → dirty |= DIRTY_TEMP
    ├──▶ ViewModel.onBaseTimer()   → dirty |= DIRTY_TIME
    └──▶ ViewModel.onStorageStats()→ dirty |= DIRTY_STORAGE
                                          │
                                   xTaskNotifyGive(renderTask)
                                          │
                                          ▼
                                   View.processRender()
```

### ArcanaTS v2 (Time-Series Database)

- Cross-platform: PAL interfaces (IFilePort, ICipher, IMutex)
- Multi-channel: up to 8 sensors per .ats file
- 1kHz sustained writes, zero data loss
- ChaCha20 encryption, CRC-32 integrity
- Daily rotation + device.ats lifecycle DB

---

## Directory Structure

### F103 Services (Role-Based MVVM)

```
Targets/stm32f103ze/
├── Services/
│   ├── Controller/     # Controller.hpp/.cpp, F103App.cpp
│   ├── Service/        # Interfaces (contracts)
│   │   ├── ITimerService.hpp, LcdService.hpp, SensorService.hpp
│   │   ├── AtsStorageService.hpp, SdBenchmarkService.hpp
│   │   ├── WifiService.hpp, MqttService.hpp, LedService.hpp, ...
│   │   └── impl/      # Implementations
│   │       ├── TimerServiceImpl.hpp/.cpp
│   │       ├── LcdServiceImpl.hpp/.cpp  (HW init only)
│   │       ├── AtsStorageServiceImpl.hpp/.cpp (1kHz TSDB)
│   │       ├── SdBenchmarkServiceImpl.hpp/.cpp (SD mount/format)
│   │       └── Wifi/Mqtt/Led/Light/Sensor/SdStorage ServiceImpl
│   ├── Driver/         # Hardware abstraction
│   │   ├── Ili9341Lcd.hpp/.cpp     (FSMC LCD)
│   │   ├── SdCard.hpp/.cpp         (SDIO DMA)
│   │   ├── Esp8266.hpp/.cpp        (UART AT)
│   │   ├── I2cBus, DhtSensor, Ap3216c, Mpu6050
│   │   ├── SdFalAdapter.hpp/.cpp   (FlashDB FAL)
│   │   └── FatFsFilePort.hpp/.cpp  (ArcanaTS file I/O)
│   ├── Model/          # F103Models.hpp, ServiceTypes.hpp
│   ├── View/           # LcdView.hpp, MainView.hpp/.cpp
│   ├── ViewModel/      # LcdViewModel.hpp (Input/Output/dirty flags)
│   └── Common/         # ChaCha20, SystemClock, DeviceKey, Font5x7
├── Core/               # HAL init, FreeRTOS config, main.c
├── Drivers/            # STM32F1xx HAL
└── Middlewares/        # FreeRTOS, FatFs (exFAT), FlashDB

Shared/
├── Inc/                # Observable.hpp, Models.hpp, Crc16, FrameCodec
│   └── ats/            # ArcanaTS headers (ArcanaTsDb, Schema, Types)
└── Src/                # Observable.cpp, ArcanaTsDb.cpp
```

---

## Features

### F103 Dashboard

| Feature | Detail |
|---------|--------|
| **LCD Dashboard** | Temperature, SD stats, WiFi status, ECG waveform, clock |
| **ECG Waveform** | 250Hz sweep display, synthetic Lead II, 8px margin scaling |
| **SD Storage** | exFAT, auto-format on corruption, 1kHz sustained writes |
| **ArcanaTS** | Daily .ats rotation, device.ats lifecycle, ChaCha20 encrypted |
| **SDIO Recovery** | Proactive reinit every 200 writes + reactive on failure |
| **Event-Driven LCD** | xTaskNotify render (no timer polling), dirty flag optimization |
| **NTP Clock** | ESP8266 UDP NTP, RTC restore from device.ats on boot |

### F103 Build Output

```
   text    data     bss     dec     hex  filename
  79072     140   57744  136956   216fc  arcana-embedded-f103.elf
```

| Resource | Used | Total | % |
|----------|------|-------|---|
| Flash | 79KB | 512KB | 15% |
| RAM (bss) | 57KB | 64KB | 89% |

---

## Build

### Prerequisites

- [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html) 1.13+
- ARM GNU Toolchain 13.3
- OpenOCD 0.12+ (for command-line flash)

### Command Line

```bash
# Build
export PATH="/Applications/STM32CubeIDE.app/.../tools/bin:$PATH"
cd Targets/stm32f103ze/Debug
make -j$(nproc) all

# Flash
openocd -f interface/cmsis-dap.cfg -c "transport select swd" \
  -f target/stm32f1x.cfg -c "program arcana-embedded-f103.elf verify reset exit"

# Monitor serial
python3 read_serial.py    # /dev/tty.usbserial-1120 @ 115200
```

### CubeIDE

1. File → Import → Existing Projects into Workspace
2. Select `Targets/stm32f103ze/`
3. Build: Ctrl+B
4. Flash: F11 (Debug)

---

## Pros & Cons

### Architecture Strengths

| Strength | Detail |
|----------|--------|
| **Correct MVVM direction** | View → ViewModel → Service, Service never touches View |
| **Role-based directories** | Consistent with arcana-android / arcana-ios projects |
| **Event-driven render** | Zero polling, xTaskNotify wakes render task on data change |
| **Observable pub/sub** | Type-safe, dual priority, ISR-safe, async dispatch |
| **Controller wiring** | Explicit wireServices() + wireViews() — all bindings visible |
| **SD self-healing** | Auto-format corrupt FS, 3 retries with SDIO HAL reinit |
| **SDIO proactive reinit** | Every 200 polling writes prevents bus degradation |
| **1kHz zero-fail writes** | ArcanaTS + periodic flush + SDIO recovery = sustained throughput |
| **Cross-platform ArcanaTS** | PAL interfaces work on STM32/ESP32/Linux |
| **Static allocation** | No malloc, predictable memory, no fragmentation |

### Known Limitations

| Limitation | Impact | Mitigation Path |
|------------|--------|-----------------|
| **g_mainView global pointer** | ECG push bypasses MVVM (AtsStorage → View) | Add ECG Observable on AtsStorageService.output |
| **ViewModel has FreeRTOS deps** | Not pure platform-independent | Extract callbacks to adapter layer |
| **LcdService nearly empty** | Only initHAL + getLcd, could merge into Driver | Keep for interface consistency |
| **subdir.mk manual sync** | CubeIDE regenerates with old paths on .ioc change | Fixed in .cproject, auto-correct via sed |
| **No unit tests** | All validation via on-board serial debug | Add host-side test with mock PAL |
| **ECG tightly coupled** | AtsStorageService knows about MainView | Route through Observable |
| **Header-only ViewModel** | Large header with Observable + FreeRTOS includes | Split to .hpp/.cpp if compile time grows |
| **Single View** | Only MainView, no navigation | Add SettingsView, ChartView when needed |

### Risk Mitigation

| Risk | Mitigation | Status |
|------|------------|--------|
| SD card corruption | f_mount + f_getfree validation → auto f_mkfs | Done |
| SDIO bus degradation | Proactive reinit every 200 writes | Done |
| Queue overflow | Error callback + overflow stats | Done |
| Power loss data loss | ArcanaTS atomic commit + CRC-32 | Done |
| Memory fragmentation | 100% static allocation | By design |
| LCD tearing | Mutex + dirty flag (only redraw changed) | Done |

---

## Roadmap

- [x] Observable Pattern + Command Pattern + Wire Protocol (F051)
- [x] Multi-target mono-repo (F051 + F103)
- [x] 3.2" LCD MVVM dashboard with ECG waveform
- [x] ArcanaTS v2 time-series database (1kHz, ChaCha20)
- [x] SD card auto-format + SDIO self-healing
- [x] Role-based MVVM directory structure
- [x] Proper MVVM wiring (View → ViewModel → Service)
- [ ] Real ADS1298 SPI ECG (replace synthetic LUT)
- [ ] ECG Observable (decouple AtsStorage → View)
- [ ] SettingsView / ChartView (multi-view navigation)
- [ ] Host-side unit tests with mock PAL
- [ ] UART transport layer (DMA-based)
- [ ] BLE connectivity (ESP32 bridge)

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

<p align="center">
  Made with embedded passion for medical-grade IoT
</p>
