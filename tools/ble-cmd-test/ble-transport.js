/**
 * @file ble-transport.js
 * @brief Native BLE GATT transport for HC-08 transparent UART
 *
 * Uses @abandonware/noble to:
 *   1. Scan for "ArcanaBLE" peripheral (HC-08 BLE 4.0)
 *   2. Connect via GATT
 *   3. Discover FFE0 service / FFE1 characteristic
 *   4. Write command frames → FFE1
 *   5. Receive response frames via FFE1 notify
 *
 * HC-08 transparent UART profile:
 *   Service:        FFE0
 *   Characteristic: FFE1 (read | write | writeWithoutResponse | notify)
 */

import noble from '@abandonware/noble';
import { FrameAssembler } from './protocol.js';

// HC-08 UUIDs (16-bit, lowercase for noble)
const HC08_SERVICE_UUID = 'ffe0';
const HC08_CHAR_UUID = 'ffe1';
const TARGET_NAME = 'ArcanaBLE';

export class NobleBleTransport {
  constructor() {
    this.peripheral = null;
    this.characteristic = null;
    this.assembler = new FrameAssembler(112); // KE response: 109B frame
    this.pendingResolve = null;
    this.pendingTimeout = null;
    this.connected = false;
    this.mtu = 20; // BLE 4.0 default ATT MTU - 3 = 20 bytes payload
  }

  /**
   * Scan, connect, and set up notifications.
   * @param {number} scanTimeoutMs Max scan time (default 10s)
   */
  async open(scanTimeoutMs = 10000) {
    // Wait for Bluetooth adapter to power on
    await this._waitPoweredOn();
    log('Bluetooth adapter powered on');

    // Scan for ArcanaBLE
    const peripheral = await this._scan(scanTimeoutMs);
    if (!peripheral) {
      throw new Error(`"${TARGET_NAME}" not found within ${scanTimeoutMs / 1000}s scan`);
    }
    this.peripheral = peripheral;

    // Connect
    log(`Connecting to ${peripheral.advertisement.localName || peripheral.id}...`);
    await this._connect(peripheral);
    this.connected = true;
    log(`Connected! RSSI: ${peripheral.rssi} dBm`);

    // Discover service + characteristic
    const char = await this._discover(peripheral);
    if (!char) {
      throw new Error('FFE1 characteristic not found');
    }
    this.characteristic = char;
    log(`UART characteristic found: ${char.uuid}`);

    // Subscribe to notifications
    await this._subscribe(char);
    log('Subscribed to notifications — ready for commands');

    // Handle disconnect
    peripheral.once('disconnect', () => {
      log('BLE disconnected');
      this.connected = false;
    });
  }

  close() {
    if (this.peripheral && this.connected) {
      this.peripheral.disconnect(() => {});
    }
    // Stop scanning if still active
    try { noble.stopScanning(); } catch (_) {}
  }

  /**
   * Send framed data to HC-08.
   * Splits into BLE MTU-sized chunks if needed.
   */
  async send(buf) {
    if (!this.characteristic) throw new Error('Not connected');

    // HC-08 with BLE 4.0: max 20 bytes per write
    for (let offset = 0; offset < buf.length; offset += this.mtu) {
      const chunk = buf.subarray(offset, Math.min(offset + this.mtu, buf.length));
      await new Promise((resolve, reject) => {
        // Use writeWithoutResponse for speed (HC-08 supports it)
        this.characteristic.write(Buffer.from(chunk), true, (err) => {
          if (err) reject(err);
          else resolve();
        });
      });
    }
  }

  /**
   * Wait for next complete frame from FrameAssembler.
   * @param {number} timeoutMs
   * @returns {Promise<Buffer|null>}
   */
  waitFrame(timeoutMs) {
    return new Promise((resolve) => {
      this.pendingResolve = resolve;
      this.pendingTimeout = setTimeout(() => {
        this.pendingResolve = null;
        this.pendingTimeout = null;
        resolve(null);
      }, timeoutMs);
    });
  }

  // ─── Private ────────────────────────────────────────────────────────────

  _waitPoweredOn() {
    return new Promise((resolve, reject) => {
      if (noble.state === 'poweredOn') { resolve(); return; }
      const timeout = setTimeout(() => {
        reject(new Error(`Bluetooth not ready (state: ${noble.state})`));
      }, 5000);
      noble.once('stateChange', (state) => {
        clearTimeout(timeout);
        if (state === 'poweredOn') resolve();
        else reject(new Error(`Bluetooth state: ${state}`));
      });
    });
  }

  _scan(timeoutMs) {
    return new Promise((resolve) => {
      let found = false;
      const timer = setTimeout(() => {
        if (!found) {
          noble.stopScanning();
          resolve(null);
        }
      }, timeoutMs);

      noble.on('discover', (peripheral) => {
        const name = peripheral.advertisement.localName;
        const services = peripheral.advertisement.serviceUuids || [];
        // Log named devices to help debugging
        if (name) {
          log(`  Found: ${name} [${peripheral.id}] RSSI:${peripheral.rssi} services:[${services}]`);
        }

        if (name === TARGET_NAME || services.includes(HC08_SERVICE_UUID)) {
          found = true;
          clearTimeout(timer);
          noble.stopScanning();
          resolve(peripheral);
        }
      });

      log(`Scanning for "${TARGET_NAME}" (${timeoutMs / 1000}s)...`);
      // Scan all services — HC-08 may not advertise FFE0 in scan response
      noble.startScanning([], false);
    });
  }

  _connect(peripheral) {
    return new Promise((resolve, reject) => {
      peripheral.connect((err) => {
        if (err) reject(err);
        else resolve();
      });
    });
  }

  _discover(peripheral) {
    return new Promise((resolve, reject) => {
      peripheral.discoverSomeServicesAndCharacteristics(
        [HC08_SERVICE_UUID],
        [HC08_CHAR_UUID],
        (err, services, characteristics) => {
          if (err) { reject(err); return; }

          if (services.length > 0) {
            log(`  Service: ${services[0].uuid}`);
          }
          for (const c of characteristics || []) {
            log(`  Char: ${c.uuid} props:[${c.properties.join(',')}]`);
          }

          const uart = (characteristics || []).find(c => c.uuid === HC08_CHAR_UUID);
          resolve(uart || null);
        }
      );
    });
  }

  _subscribe(char) {
    return new Promise((resolve, reject) => {
      char.on('data', (data, isNotification) => {
        // Feed bytes into FrameAssembler
        for (const byte of data) {
          const frameBuf = this.assembler.feedByte(byte);
          if (frameBuf && this.pendingResolve) {
            clearTimeout(this.pendingTimeout);
            const res = this.pendingResolve;
            this.pendingResolve = null;
            this.pendingTimeout = null;
            res(frameBuf);
          }
        }
      });

      char.subscribe((err) => {
        if (err) reject(err);
        else resolve();
      });
    });
  }
}

function log(msg) { console.log(`  ${msg}`); }
