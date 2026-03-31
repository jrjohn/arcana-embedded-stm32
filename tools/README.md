# Tools

Development and testing utilities for the Arcana Embedded STM32 project.

## BLE Command Test Tool (`ble-cmd-test/`)

Interactive CLI for testing the STM32 Command Pattern protocol over BLE.

```bash
cd ble-cmd-test && npm install

# Native BLE GATT (recommended — uses Mac built-in Bluetooth)
node index.js --ble --auto                    # send all 8 commands
node index.js --ble --monitor                 # interactive + serial debug monitor
node index.js --ble --monitor --auto          # auto test + monitor

# Serial dongle fallback
node index.js --port /dev/tty.usbmodemXXX --auto
```

### Features
- Full Arcana wire protocol (CRC-16, FrameCodec, FrameAssembler, CommandCodec)
- 8 commands: Ping, GetFwVersion, GetCompileTime, GetModel, GetSerialNumber, GetTemperature, GetAccel, GetLight
- Native BLE GATT via `noble` (`--ble`) — connects directly to HC-08 "ArcanaBLE"
- AES-256-CCM + ECDH P-256 crypto layer (`--encrypt`, `--key-exchange`)
- Serial debug monitor (`--monitor`) — real-time board log on `/dev/tty.usbserial-1120`
- Interactive mode with ping loop, raw hex send, command repeat

### Protocol Stack
| Layer | STM32 C++ | Node.js |
|-------|-----------|---------|
| CRC-16 | `Crc16.hpp` (poly 0x8408) | `protocol.js` |
| Frame | `FrameCodec.hpp` (0xAC DA) | `protocol.js` |
| Reassembly | `FrameAssembler.hpp` | `protocol.js` |
| Command | `CommandCodec` | `protocol.js` |
| Command Pattern | `ICommand` + `CommandBridge` | `commands.js` |
| Crypto | `CryptoEngine` + `ChaCha20` | `crypto.js` |
| Protobuf | nanopb `arcana_cmd.pb` | `protobuf.js` |
| BLE GATT | `Hc08Ble` (HC-08 USART2) | `ble-transport.js` (noble) |

## BLE Sensor Monitor (`ble-sensor-monitor.js`)

Real-time BLE sensor stream decoder. Receives ChaCha20 encrypted protobuf frames (same format as MQTT sensor publish).

```bash
# Derive key from device UID (24-hex = GetSerialNumber output)
node ble-sensor-monitor.js --uid 32FFD6054754323910580957

# Explicit key
node ble-sensor-monitor.js --key <64-char-hex>

# JSON lines output (for piping)
node ble-sensor-monitor.js --uid <UID> --json

# Show raw frame hex
node ble-sensor-monitor.js --uid <UID> --raw
```

### Sensor Frame Format
```
FrameCodec: [AC DA][ver][flags][sid=0x20][len][payload][crc]
Payload:    [nonce:12][ChaCha20_encrypted_protobuf:N]
Protobuf:   field1=temp*10 (sint32), field2=ax, field3=ay, field4=az,
            field5=als (uint32), field6=ps (uint32)
```

## Serial Monitor (`read_serial.py`)

Simple STM32 debug serial output reader.

```bash
python3 read_serial.py
# Connects to /dev/tty.usbserial-1120 @ 115200
```

## ArcanaTS Reader (`arcanats.py`)

Python tool for reading and decrypting `.ats` time-series database files from SD card.

```bash
python3 arcanats.py <file.ats>
```

## MQTT Crypto Test (`mqtt_crypto_test.py`)

Send encrypted protobuf commands to STM32 via MQTT (WSS).

```bash
export ARCANA_PSK=<64-char-hex>
python3 mqtt_crypto_test.py --cmd ping
python3 mqtt_crypto_test.py --cmd temperature --key-exchange
python3 mqtt_crypto_test.py --cmd fw_version --no-encrypt
```

## ESP8266 Firmware (`esp8266_fw/`)

AT firmware files for the ESP8266 WiFi module.

## Upload Server (`upload_server.py`, `server/`)

HTTP upload server for receiving `.ats` / `.enc` files from the STM32 board.

## nRF52840 Dongle (`nrf52840/`)

Firmware and DFU tools for the E104-BT5040U (nRF52840) BLE USB dongle. Used with **nRF Connect for Desktop** for BLE debugging and GATT exploration.

### Setup
1. Install nRF Connect for Desktop: `brew install --cask nrf-connect`
2. Open **Bluetooth Low Energy** app
3. Select dongle → auto-program connectivity firmware
4. Scan for "ArcanaBLE" and connect

## Hardware

| Device | Port | Baud | Purpose |
|--------|------|------|---------|
| STM32 debug serial | `/dev/tty.usbserial-1120` | 115200 | Debug log output |
| fireDAP (CMSIS-DAP) | `/dev/tty.usbmodem113301` | — | SWD flash/debug |
| E104-BT5040U dongle | `/dev/tty.usbmodemFD5612E58D242` | 1000000 | BLE connectivity adapter |
| HC-08 BLE module | USART2 (PA2/PA3) | 9600 | BLE transparent UART |
