#!/usr/bin/env node
/**
 * @file index.js
 * @brief Arcana BLE Command Test Tool v2.0
 *
 * Interactive CLI to send commands to STM32 board via BLE.
 * Supports native BLE GATT (noble), serial dongle, plaintext, and encrypted modes.
 *
 * Usage:
 *   node index.js --ble                              # native BLE GATT (recommended)
 *   node index.js --ble --monitor                    # BLE + serial debug monitor
 *   node index.js --ble --auto                       # BLE + auto test all commands
 *   node index.js --ble --encrypt --uid <UID>        # BLE + AES-256-CCM
 *   node index.js --port /dev/tty.usbmodemXXX        # serial dongle fallback
 *   node index.js --encrypt --psk <64-char-hex>      # encrypt with explicit PSK
 *   node index.js --encrypt --key-exchange           # ECDH P-256 → session key
 *   node index.js --monitor                          # serial debug monitor
 *   node index.js --list                             # list serial ports
 *   node index.js --auto                             # send all commands once
 */

import { SerialPort } from 'serialport';
import * as readline from 'readline';
import {
  frame, deframe, FrameAssembler,
  encodeRequest, decodeResponse,
  Cluster as ClusterEnum,
  CommandStatusName,
} from './protocol.js';
import { commands, findCommand } from './commands.js';
import {
  CryptoEngine, KeyExchangeClient,
  ChaChaSession, ChaChaKeyExchangeClient,
  deriveDeviceKey, LEGACY_FLEET_MASTER,
} from './crypto.js';
import { encodeCmdRequest, decodeCmdResponse } from './protobuf.js';

// ─── CLI Argument Parsing ───────────────────────────────────────────────────

const args = process.argv.slice(2);
function getArg(flag) {
  const idx = args.indexOf(flag);
  return idx >= 0 && idx + 1 < args.length ? args[idx + 1] : null;
}
const hasFlag = (flag) => args.includes(flag);

const PORT = getArg('--port');
const BAUD = parseInt(getArg('--baud') || '9600', 10);
const LIST_MODE = hasFlag('--list');
const AUTO_MODE = hasFlag('--auto');
const TIMEOUT_MS = parseInt(getArg('--timeout') || '3000', 10);

// Native BLE mode
const BLE_MODE = hasFlag('--ble');

// Encryption options
const ENCRYPT_MODE = hasFlag('--encrypt');
const KEY_EXCHANGE = hasFlag('--key-exchange');
const PSK_HEX = getArg('--psk');
const UID_HEX = getArg('--uid');
const FLEET_MASTER_HEX = getArg('--fleet-master');

// Serial debug monitor
const MONITOR_MODE = hasFlag('--monitor');
const MONITOR_PORT = getArg('--monitor-port') || '/dev/tty.usbserial-1120';
const MONITOR_BAUD = parseInt(getArg('--monitor-baud') || '115200', 10);

// ─── Helpers ────────────────────────────────────────────────────────────────

function log(msg) { console.log(`  ${msg}`); }
function logHex(label, buf) {
  const hex = [...buf].map(b => b.toString(16).padStart(2, '0')).join(' ');
  log(`${label}: [${hex}] (${buf.length} bytes)`);
}

function banner() {
  const crypto = ENCRYPT_MODE ? 'ChaCha20 + HMAC-SHA256' : 'plaintext';
  const transport = BLE_MODE ? 'native BLE GATT (noble)' : 'serial dongle';
  console.log('');
  console.log('  ╔══════════════════════════════════════════════╗');
  console.log('  ║   Arcana BLE Command Test Tool v2.0          ║');
  console.log(`  ║   Transport: ${transport.padEnd(32)}║`);
  console.log(`  ║   Crypto: ${crypto.padEnd(35)}║`);
  if (KEY_EXCHANGE) {
  console.log('  ║   Key Exchange: ECDH P-256 (PFS)             ║');
  }
  if (MONITOR_MODE) {
  console.log(`  ║   Monitor: ${MONITOR_PORT.padEnd(34)}║`);
  }
  console.log('  ╚══════════════════════════════════════════════╝');
  console.log('');
}

// ─── Serial Debug Monitor ───────────────────────────────────────────────────

class SerialMonitor {
  constructor(portPath, baudRate) {
    this.portPath = portPath;
    this.baudRate = baudRate;
    this.port = null;
    this.lineBuf = '';
  }

  async start() {
    return new Promise((resolve, reject) => {
      this.port = new SerialPort({
        path: this.portPath,
        baudRate: this.baudRate,
        dataBits: 8,
        parity: 'none',
        stopBits: 1,
      });

      this.port.on('open', () => {
        log(`Monitor opened: ${this.portPath} @ ${this.baudRate}`);
        resolve();
      });

      this.port.on('error', (err) => {
        console.error(`  [MON] Error: ${err.message}`);
        reject(err);
      });

      this.port.on('data', (chunk) => {
        this.lineBuf += chunk.toString('utf-8');
        let nl;
        while ((nl = this.lineBuf.indexOf('\n')) >= 0) {
          const line = this.lineBuf.substring(0, nl).replace(/\r$/, '');
          this.lineBuf = this.lineBuf.substring(nl + 1);
          if (line.length > 0) {
            this._printLine(line);
          }
        }
      });
    });
  }

  _printLine(line) {
    // Color-code by severity
    const ts = new Date().toISOString().substring(11, 23);
    if (line.includes('[E]') || line.includes('ERR')) {
      console.log(`  \x1b[31m[MON ${ts}] ${line}\x1b[0m`);
    } else if (line.includes('[W]') || line.includes('WARN')) {
      console.log(`  \x1b[33m[MON ${ts}] ${line}\x1b[0m`);
    } else if (line.includes('BLE') || line.includes('Cmd') || line.includes('KE')) {
      console.log(`  \x1b[36m[MON ${ts}] ${line}\x1b[0m`);
    } else {
      console.log(`  \x1b[90m[MON ${ts}] ${line}\x1b[0m`);
    }
  }

  stop() {
    if (this.port && this.port.isOpen) {
      this.port.close();
    }
  }
}

// ─── List Serial Ports ──────────────────────────────────────────────────────

async function listPorts() {
  const ports = await SerialPort.list();
  if (ports.length === 0) {
    log('No serial ports found.');
    return;
  }
  console.log('');
  log('Available serial ports:');
  log('─'.repeat(60));
  for (const p of ports) {
    const info = [p.manufacturer, p.serialNumber].filter(Boolean).join(', ');
    log(`  ${p.path}  ${info ? `(${info})` : ''}`);
  }
  log('─'.repeat(60));
  console.log('');
}

// ─── Transport Layer ────────────────────────────────────────────────────────

class BleTransport {
  constructor(portPath, baudRate) {
    this.portPath = portPath;
    this.baudRate = baudRate;
    this.port = null;
    this.assembler = new FrameAssembler();
    this.pendingResolve = null;
    this.pendingTimeout = null;
  }

  async open() {
    return new Promise((resolve, reject) => {
      this.port = new SerialPort({
        path: this.portPath,
        baudRate: this.baudRate,
        dataBits: 8,
        parity: 'none',
        stopBits: 1,
      });

      this.port.on('open', () => {
        log(`BLE port opened: ${this.portPath} @ ${this.baudRate}`);
        resolve();
      });

      this.port.on('error', (err) => {
        reject(err);
      });

      this.port.on('data', (chunk) => {
        for (const byte of chunk) {
          const frameBuf = this.assembler.feedByte(byte);
          if (frameBuf && this.pendingResolve) {
            clearTimeout(this.pendingTimeout);
            const resolve = this.pendingResolve;
            this.pendingResolve = null;
            this.pendingTimeout = null;
            resolve(frameBuf);
          }
        }
      });
    });
  }

  close() {
    if (this.port && this.port.isOpen) {
      this.port.close();
    }
  }

  async send(buf) {
    return new Promise((resolve, reject) => {
      this.port.write(buf, (err) => {
        if (err) reject(err);
        else this.port.drain(() => resolve());
      });
    });
  }

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
}

// ─── Command Executor (plaintext) ───────────────────────────────────────────

class PlaintextExecutor {
  constructor(transport, timeoutMs = 3000) {
    this.transport = transport;
    this.timeoutMs = timeoutMs;
    this.encrypted = false;
  }

  async execute(command) {
    const { cluster, commandId } = command.key;
    const params = command.buildParams();

    const payload = encodeRequest(cluster, commandId, params);
    const frameBuf = frame(payload);

    log(`TX ${command.name} [${cluster.toString(16).padStart(2,'0')}:${commandId.toString(16).padStart(2,'0')}]`);
    logHex('   Frame', frameBuf);

    await this.transport.send(frameBuf);
    const rxFrame = await this.transport.waitFrame(this.timeoutMs);
    if (!rxFrame) return { success: false, error: 'Timeout waiting for response' };

    logHex('   RX Frame', rxFrame);
    const deframed = deframe(rxFrame);
    if (!deframed) return { success: false, error: 'Invalid frame (CRC mismatch)' };

    const response = decodeResponse(deframed.payload);
    if (!response) return { success: false, error: 'Invalid response payload' };

    const decoded = response.status === 0
      ? command.decodeResponse(response.data)
      : { error: response.statusName };

    return { success: true, response, decoded };
  }
}

// ─── Command Executor (encrypted — ChaCha20 + HMAC-SHA256) ─────────────────

const SID_ENCRYPTED = 0x10;

class EncryptedExecutor {
  constructor(transport, deviceKey, timeoutMs = 3000) {
    this.transport = transport;
    this.deviceKey = deviceKey;
    this.session = null; // set after key exchange
    this.timeoutMs = timeoutMs;
    this.encrypted = true;
  }

  async execute(command) {
    if (!this.session) return { success: false, error: 'No session — run key exchange first (k)' };

    const { cluster, commandId } = command.key;
    const params = command.buildParams();

    // 1. Binary encode: [cluster][cmdId][paramsLen][params]
    const binary = encodeRequest(cluster, commandId, params);
    log(`TX ${command.name} [${cluster.toString(16).padStart(2,'0')}:${commandId.toString(16).padStart(2,'0')}]`);

    // 2. ChaCha20 + HMAC encrypt
    const encrypted = this.session.encrypt(binary);
    log(`   Encrypted: ${encrypted.length}B (counter=${this.session.txCounter - 1})`);

    // 3. Frame with sid=0x10
    const frameBuf = frame(encrypted, 0x01, SID_ENCRYPTED);
    logHex('   Frame', frameBuf);

    // 4. Send
    await this.transport.send(frameBuf);

    // 5. Wait for response — skip sensor stream frames (sid=0x20)
    let deframed = null;
    const deadline = Date.now() + this.timeoutMs;
    while (Date.now() < deadline) {
      const rxFrame = await this.transport.waitFrame(Math.max(500, deadline - Date.now()));
      if (!rxFrame) break;
      const d = deframe(rxFrame);
      if (!d) continue;
      if (d.streamId === 0x20) continue;
      logHex('   RX Frame', rxFrame);
      deframed = d;
      break;
    }
    if (!deframed) return { success: false, error: 'Timeout waiting for response' };

    // 6. Decrypt + verify HMAC
    let plaintext;
    try {
      plaintext = this.session.decrypt(deframed.payload);
      log(`   Decrypted: ${plaintext.length}B`);
    } catch (e) {
      return { success: false, error: `Decrypt/HMAC failed: ${e.message}` };
    }

    // 8. Binary decode response: [cluster][cmdId][status][dataLen][data]
    const response = decodeResponse(plaintext);
    if (!response) return { success: false, error: 'Invalid response payload' };

    const decoded = response.status === 0
      ? command.decodeResponse(response.data)
      : { error: response.statusName };

    return { success: true, response, decoded };
  }

  /**
   * ECDH P-256 Key Exchange → ChaCha20 session.
   * KE request/response are plaintext (sid=0x00).
   */
  async performKeyExchange() {
    log('─── ECDH P-256 Key Exchange (ChaCha20+HMAC) ───');
    const ke = new ChaChaKeyExchangeClient(this.deviceKey);
    const clientPub = ke.getClientPubKey();
    log(`Client pub: ${clientPub.subarray(0, 8).toString('hex')}...${clientPub.subarray(56).toString('hex')}`);

    // Build plaintext KE request: Security::KeyExchange with 64B client pub key
    // Binary: [cluster=0x04][cmdId=0x01][paramsLen=0x00] + raw 64B pubkey after header
    const kePayload = Buffer.alloc(3 + 64);
    kePayload[0] = ClusterEnum.Security; // 0x04
    kePayload[1] = 0x01; // KeyExchange
    kePayload[2] = 0x00; // paramsLen=0 (pub key follows as extended payload)
    clientPub.copy(kePayload, 3);

    const frameBuf = frame(kePayload, 0x01, 0x00); // plaintext sid=0x00
    log(`TX KeyExchange (${frameBuf.length}B)`);
    await this.transport.send(frameBuf);

    // Wait for response — skip sensor stream frames (sid=0x20)
    let deframed = null;
    const deadline = Date.now() + this.timeoutMs + 10000;
    while (Date.now() < deadline) {
      const rxFrame = await this.transport.waitFrame(Math.max(1000, deadline - Date.now()));
      if (!rxFrame) break;
      const d = deframe(rxFrame);
      if (!d) continue;
      if (d.streamId === 0x20) continue; // skip sensor stream
      logHex('[KE] RX Frame', rxFrame);
      deframed = d;
      break;
    }
    if (!deframed) {
      log('[KE] Timeout — no KE response from board');
      return false;
    }

    // Parse binary response: [cluster][cmdId][status][dataLen][data...]
    const rsp = decodeResponse(deframed.payload);
    if (!rsp) { log('[KE] Response decode failed'); return false; }
    if (rsp.status !== 0) {
      log(`[KE] Server rejected: status=${rsp.statusName}`);
      return false;
    }

    // KE response data = [serverPub:64][authTag:32]
    const session = ke.processResponse(rsp.data);
    if (!session) return false;

    this.session = session;
    log('[KE] Session established — Perfect Forward Secrecy active');
    log('─'.repeat(45));
    return true;
  }
}

// ─── Auto Mode ──────────────────────────────────────────────────────────────

async function runAutoMode(executor) {
  log(`Auto mode — sending all commands (${executor.encrypted ? 'encrypted' : 'plaintext'})...`);
  console.log('');

  let passed = 0, failed = 0;

  for (const cmd of commands) {
    const result = await executor.execute(cmd);
    if (result.success && result.response.status === 0) {
      log(`[PASS] ${cmd.name}: ${JSON.stringify(result.decoded)}`);
      passed++;
    } else if (result.success) {
      log(`[FAIL] ${cmd.name}: status=${result.response.statusName}`);
      failed++;
    } else {
      log(`[FAIL] ${cmd.name}: ${result.error}`);
      failed++;
    }
    console.log('');
    await new Promise(r => setTimeout(r, 200));
  }

  console.log('');
  log(`Results: ${passed} passed, ${failed} failed, ${commands.length} total`);
  console.log('');
}

// ─── Interactive Mode ───────────────────────────────────────────────────────

async function runInteractiveMode(executor) {
  const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
  });

  const question = (prompt) => new Promise(resolve => rl.question(prompt, resolve));

  function showMenu() {
    console.log('');
    const modeTag = executor.encrypted ? ' [ENC]' : ' [PLAIN]';
    log(`Commands${modeTag}:`);
    log('─'.repeat(55));
    commands.forEach((cmd, i) => {
      const { cluster, commandId } = cmd.key;
      const id = `${cluster.toString(16).padStart(2,'0')}:${commandId.toString(16).padStart(2,'0')}`;
      log(`  ${(i + 1).toString().padStart(2)}. ${cmd.name.padEnd(28)} [${id}]`);
    });
    log('─'.repeat(55));
    log('   a  Send ALL commands');
    log('   r  Repeat last command');
    log('   l  Loop ping (Ctrl+C to stop)');
    if (executor.encrypted) {
    log('   k  Perform ECDH key exchange');
    }
    log('   x  Send raw hex frame');
    log('   q  Quit');
    console.log('');
  }

  let lastCmd = null;
  showMenu();

  while (true) {
    const input = (await question('  > ')).trim().toLowerCase();
    if (!input) continue;

    if (input === 'q' || input === 'quit' || input === 'exit') {
      log('Bye.');
      rl.close();
      return;
    }

    if (input === '?') { showMenu(); continue; }

    if (input === 'a' || input === 'all') {
      await runAutoMode(executor);
      continue;
    }

    if (input === 'r' || input === 'repeat') {
      if (lastCmd) {
        const result = await executor.execute(lastCmd);
        printResult(lastCmd, result);
      } else {
        log('No previous command.');
      }
      continue;
    }

    if (input === 'l' || input === 'loop') {
      log('Ping loop (Ctrl+C to stop)...');
      const pingCmd = commands[0];
      let count = 0;
      const abortCtrl = new AbortController();
      const onSigInt = () => { abortCtrl.abort(); };
      process.once('SIGINT', onSigInt);

      while (!abortCtrl.signal.aborted) {
        const result = await executor.execute(pingCmd);
        count++;
        if (result.success && result.response.status === 0) {
          log(`  #${count} PONG: ${JSON.stringify(result.decoded)}`);
        } else {
          log(`  #${count} FAIL: ${result.error || result.response?.statusName}`);
        }
        await new Promise(r => setTimeout(r, 1000));
      }

      process.removeListener('SIGINT', onSigInt);
      log(`Loop stopped after ${count} pings.`);
      continue;
    }

    if ((input === 'k' || input === 'ke') && executor instanceof EncryptedExecutor) {
      await executor.performKeyExchange();
      continue;
    }

    if (input === 'x' || input === 'hex') {
      const hex = (await question('  hex> ')).trim().replace(/\s/g, '');
      if (hex.length % 2 !== 0 || !/^[0-9a-fA-F]+$/.test(hex)) {
        log('Invalid hex string.');
        continue;
      }
      const raw = Buffer.from(hex, 'hex');
      logHex('TX Raw', raw);
      await executor.transport.send(raw);
      const rxFrame = await executor.transport.waitFrame(TIMEOUT_MS);
      if (rxFrame) {
        logHex('RX Frame', rxFrame);
        const d = deframe(rxFrame);
        if (d) {
          if (executor.encrypted) {
            try {
              const plain = executor.crypto.decrypt(d.payload);
              const rsp = decodeCmdResponse(plain);
              log(`  Response: ${JSON.stringify(rsp)}`);
            } catch (e) {
              log(`  Decrypt/decode failed: ${e.message}`);
            }
          } else {
            const rsp = decodeResponse(d.payload);
            if (rsp) log(`  Response: ${JSON.stringify(rsp)}`);
          }
        }
      } else {
        log('  No response (timeout).');
      }
      continue;
    }

    const num = parseInt(input, 10);
    if (num >= 1 && num <= commands.length) {
      const cmd = commands[num - 1];
      lastCmd = cmd;
      const result = await executor.execute(cmd);
      printResult(cmd, result);
      continue;
    }

    log(`Unknown input "${input}". Press ? for help.`);
  }
}

function printResult(cmd, result) {
  if (result.success && result.response.status === 0) {
    log(`[OK] ${cmd.name}: ${JSON.stringify(result.decoded)}`);
  } else if (result.success) {
    log(`[ERR] ${cmd.name}: status=${result.response.statusName}`);
  } else {
    log(`[ERR] ${cmd.name}: ${result.error}`);
  }
}

// ─── Auto-detect BLE dongle port ────────────────────────────────────────────

async function autoDetectPort() {
  const ports = await SerialPort.list();
  const blePort = ports.find(p =>
    p.path.includes('usbmodem') && p.manufacturer?.includes('Nordic')
  );
  if (blePort) return blePort.path;
  const anyModem = ports.find(p => p.path.includes('usbmodem'));
  if (anyModem) return anyModem.path;
  return null;
}

// ─── PSK Setup ──────────────────────────────────────────────────────────────

function setupPSK() {
  if (PSK_HEX) {
    // Explicit PSK
    const psk = Buffer.from(PSK_HEX, 'hex');
    if (psk.length !== 32) {
      log('ERROR: --psk must be 64 hex chars (32 bytes)');
      process.exit(1);
    }
    log(`PSK: explicit (${PSK_HEX.substring(0, 16)}...)`);
    return psk;
  }

  if (UID_HEX) {
    // Derive from fleet master + UID
    const uid = Buffer.from(UID_HEX, 'hex');
    if (uid.length !== 12) {
      log('ERROR: --uid must be 24 hex chars (12 bytes)');
      process.exit(1);
    }
    const fleetMaster = FLEET_MASTER_HEX
      ? Buffer.from(FLEET_MASTER_HEX, 'hex')
      : LEGACY_FLEET_MASTER;
    const psk = deriveDeviceKey(fleetMaster, uid);
    log(`PSK: derived from ${FLEET_MASTER_HEX ? 'custom' : 'legacy'} fleet master + UID`);
    log(`     UID: ${UID_HEX}`);
    log(`     PSK: ${psk.toString('hex').substring(0, 16)}...`);
    return psk;
  }

  // No UID provided — will need plaintext first to get serial number
  log('No --psk or --uid provided.');
  log('Tip: first run plaintext mode to get serial number (= UID),');
  log('     then use: --encrypt --uid <serial_number_hex>');
  log('Using legacy fleet master + dummy UID for now (will fail on real device).');
  const dummyUid = Buffer.alloc(12); // will not match real device
  return deriveDeviceKey(LEGACY_FLEET_MASTER, dummyUid);
}

// ─── Main ───────────────────────────────────────────────────────────────────

async function main() {
  banner();

  if (LIST_MODE) {
    await listPorts();
    process.exit(0);
  }

  // Start serial debug monitor (if requested)
  let monitor = null;
  if (MONITOR_MODE) {
    monitor = new SerialMonitor(MONITOR_PORT, MONITOR_BAUD);
    try {
      await monitor.start();
    } catch (err) {
      log(`Warning: Could not open monitor port ${MONITOR_PORT}: ${err.message}`);
      monitor = null;
    }
  }

  // Open transport (native BLE or serial)
  let transport;
  if (BLE_MODE) {
    const { NobleBleTransport } = await import('./ble-transport.js');
    transport = new NobleBleTransport();
    try {
      await transport.open();
    } catch (err) {
      log(`BLE connection failed: ${err.message}`);
      process.exit(1);
    }
  } else {
    let portPath = PORT;
    if (!portPath) {
      portPath = await autoDetectPort();
      if (!portPath) {
        log('No BLE dongle detected. Use --port/--ble to specify, or --list to see ports.');
        process.exit(1);
      }
      log(`Auto-detected serial port: ${portPath}`);
    }
    transport = new BleTransport(portPath, BAUD);
    try {
      await transport.open();
    } catch (err) {
      log(`Failed to open ${portPath}: ${err.message}`);
      process.exit(1);
    }
  }

  // Graceful shutdown
  process.on('SIGINT', () => {
    console.log('');
    log('Shutting down...');
    transport.close();
    if (monitor) monitor.stop();
    process.exit(0);
  });

  // Create executor (plaintext or encrypted)
  let executor;
  if (ENCRYPT_MODE) {
    const deviceKey = setupPSK();
    executor = new EncryptedExecutor(transport, deviceKey, TIMEOUT_MS);
    log('Encryption enabled: ChaCha20 + HMAC-SHA256 (key exchange required)');
  } else {
    executor = new PlaintextExecutor(transport, TIMEOUT_MS);
    log('Mode: plaintext (use --encrypt to enable encryption)');
  }

  // Wait for connection to stabilize (serial only — native BLE is already connected)
  if (!BLE_MODE) {
    log('Waiting 1s for BLE link...');
    await new Promise(r => setTimeout(r, 1000));
  }

  // Auto key exchange if requested
  if (KEY_EXCHANGE && executor instanceof EncryptedExecutor) {
    const ok = await executor.performKeyExchange();
    if (!ok) {
      log('Key exchange failed. Continuing with PSK...');
    }
  }

  if (AUTO_MODE) {
    await runAutoMode(executor);
    transport.close();
    if (monitor) monitor.stop();
    process.exit(0);
  }

  await runInteractiveMode(executor);
  transport.close();
  if (monitor) monitor.stop();
}

main().catch(err => {
  console.error('Fatal:', err);
  process.exit(1);
});
