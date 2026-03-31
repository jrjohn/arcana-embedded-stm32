/**
 * @file commands.js
 * @brief Command Pattern — mirrors STM32 CommandBridge registered commands
 *
 * Each command knows its cluster/id, how to build request params,
 * and how to decode the response data into human-readable form.
 */

import { Cluster } from './protocol.js';

// ─── Base Command ───────────────────────────────────────────────────────────

class Command {
  /** @returns {{ cluster: number, commandId: number }} */
  get key() { throw new Error('Not implemented'); }

  /** @returns {string} Human-readable name */
  get name() { throw new Error('Not implemented'); }

  /** @returns {Buffer} Request params (0-8 bytes) */
  buildParams() { return Buffer.alloc(0); }

  /**
   * Decode response data into a human-readable object.
   * @param {Buffer} data - Response data bytes
   * @returns {object} Decoded result
   */
  decodeResponse(data) { return { raw: data.toString('hex') }; }
}

// ─── System Commands ────────────────────────────────────────────────────────

class PingCommand extends Command {
  get key() { return { cluster: Cluster.System, commandId: 0x01 }; }
  get name() { return 'System::Ping'; }

  decodeResponse(data) {
    if (data.length >= 4) {
      const tick = data.readUInt32LE(0);
      return { tick, uptime: `${(tick / 1000).toFixed(1)}s` };
    }
    return { raw: data.toString('hex') };
  }
}

class GetFwVersionCommand extends Command {
  get key() { return { cluster: Cluster.System, commandId: 0x02 }; }
  get name() { return 'System::GetFwVersion'; }

  decodeResponse(data) {
    return { version: data.toString('ascii') };
  }
}

class GetCompileTimeCommand extends Command {
  get key() { return { cluster: Cluster.System, commandId: 0x03 }; }
  get name() { return 'System::GetCompileTime'; }

  decodeResponse(data) {
    return { compileTime: data.toString('ascii') };
  }
}

// ─── Device Commands ────────────────────────────────────────────────────────

class GetDeviceModelCommand extends Command {
  get key() { return { cluster: Cluster.Device, commandId: 0x01 }; }
  get name() { return 'Device::GetModel'; }

  decodeResponse(data) {
    return { model: data.toString('ascii') };
  }
}

class GetSerialNumberCommand extends Command {
  get key() { return { cluster: Cluster.Device, commandId: 0x02 }; }
  get name() { return 'Device::GetSerialNumber'; }

  decodeResponse(data) {
    return { serialNumber: data.toString('ascii') };
  }
}

// ─── Sensor Commands ────────────────────────────────────────────────────────

class GetTemperatureCommand extends Command {
  get key() { return { cluster: Cluster.Sensor, commandId: 0x02 }; }
  get name() { return 'Sensor::GetTemperature'; }

  decodeResponse(data) {
    if (data.length >= 2) {
      const raw = data.readInt16LE(0);
      return { temperature: (raw / 10).toFixed(1) + ' C' };
    }
    return { raw: data.toString('hex') };
  }
}

class GetAccelCommand extends Command {
  get key() { return { cluster: Cluster.Sensor, commandId: 0x03 }; }
  get name() { return 'Sensor::GetAccel'; }

  decodeResponse(data) {
    if (data.length >= 6) {
      return {
        x: data.readInt16LE(0),
        y: data.readInt16LE(2),
        z: data.readInt16LE(4),
      };
    }
    return { raw: data.toString('hex') };
  }
}

class GetLightCommand extends Command {
  get key() { return { cluster: Cluster.Sensor, commandId: 0x04 }; }
  get name() { return 'Sensor::GetLight'; }

  decodeResponse(data) {
    if (data.length >= 4) {
      return {
        ambientLight: data.readUInt16LE(0),
        proximity: data.readUInt16LE(2),
      };
    }
    return { raw: data.toString('hex') };
  }
}

// ─── Command Registry ───────────────────────────────────────────────────────

export const commands = [
  new PingCommand(),
  new GetFwVersionCommand(),
  new GetCompileTimeCommand(),
  new GetDeviceModelCommand(),
  new GetSerialNumberCommand(),
  new GetTemperatureCommand(),
  new GetAccelCommand(),
  new GetLightCommand(),
];

/**
 * Find a command by cluster + commandId.
 */
export function findCommand(cluster, commandId) {
  return commands.find(c =>
    c.key.cluster === cluster && c.key.commandId === commandId
  );
}
