#!/usr/bin/env python3
"""
Arcana MQTT Monitor — real-time sensor data + encrypted command console.

Shows:
  - Sensor data (temperature, accel) in real-time
  - Encrypted command responses (AES-256-CCM + protobuf)

Interactive commands:
  ping     — Ping (returns tick count)
  fw       — Get firmware version
  temp     — Get temperature
  accel    — Get accelerometer
  model    — Get device model
  serial   — Get serial number
  quit     — Exit

Usage:
  python3 tools/mqtt_monitor.py
"""

import json, ssl, struct, sys, time, threading

sys.path.insert(0, '.')
from tools.mqtt_crypto_test import *

def main():
    crypto = CryptoEngine(PSK_HEX)

    CMDS = {
        'ping':   (0x00, 0x01),
        'fw':     (0x00, 0x02),
        'compile':(0x00, 0x03),
        'temp':   (0x01, 0x02),
        'accel':  (0x01, 0x03),
        'model':  (0x02, 0x01),
        'serial': (0x02, 0x02),
    }

    def on_connect(c, ud, fl, rc, p=None):
        c.subscribe('/arcana/#')
        print('[connected] Subscribed to /arcana/#')

    def on_message(c, ud, msg):
        ts = time.strftime('%H:%M:%S')

        if msg.topic == '/arcana/sensor':
            try:
                d = json.loads(msg.payload)
                print(f'  [{ts}] sensor  t={d["t"]}°C  ax={d["ax"]}  ay={d["ay"]}  az={d["az"]}')
            except:
                print(f'  [{ts}] sensor  {msg.payload[:80]}')
            return

        if msg.topic == '/arcana/rsp':
            raw = msg.payload
            try:
                raw = bytes.fromhex(raw.decode('ascii'))
            except:
                print(f'  [{ts}] rsp (not hex) {msg.payload[:50]}')
                return

            r = frame_decode(raw)
            if not r:
                print(f'  [{ts}] rsp deframe fail')
                return

            payload, flags, sid = r
            try:
                plain = crypto.decrypt(payload)
                rsp = decode_cmd_response(plain)
                status = {0:'OK', 1:'NotFound', 2:'InvalidParam',
                          3:'Busy', 4:'Error'}.get(rsp['status'], '?')
                out = {
                    'cluster': f'0x{rsp["cluster"]:02X}',
                    'command': f'0x{rsp["command"]:02X}',
                    'status': status,
                }
                if rsp['payload']:
                    out['hex'] = rsp['payload'].hex()
                    if len(rsp['payload']) == 4:
                        out['uint32'] = struct.unpack('<I', rsp['payload'])[0]
                    elif len(rsp['payload']) > 0:
                        try:
                            out['text'] = rsp['payload'].rstrip(b'\x00').decode('utf-8', errors='replace')
                        except:
                            pass
                print(f'  [{ts}] rsp     {json.dumps(out)}')
            except Exception as e:
                print(f'  [{ts}] rsp decrypt/decode fail: {e}')
            return

        # Other topics
        print(f'  [{ts}] {msg.topic}: {msg.payload[:60]}')

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                         client_id='arcana_monitor', transport='websockets')
    client.username_pw_set('arcana', 'arcana')
    client.tls_set(cert_reqs=ssl.CERT_REQUIRED)
    client.ws_set_options(path='/mqtt')
    client.on_connect = on_connect
    client.on_message = on_message

    print('Arcana MQTT Monitor')
    print(f'Broker: iot.somnics.cloud:443 (WSS)')
    print(f'Encryption: AES-256-CCM | Serialization: protobuf')
    print('─' * 60)

    client.connect('iot.somnics.cloud', 443, 60)
    client.loop_start()

    def send_cmd(name):
        if name not in CMDS:
            print(f'  Unknown command: {name}')
            print(f'  Available: {", ".join(CMDS.keys())}')
            return
        cluster, command = CMDS[name]
        pb = encode_cmd_request(cluster, command)
        enc = crypto.encrypt(pb)
        framed = frame_encode(enc)
        client.publish('/arcana/cmd', framed, qos=0)
        print(f'  >>> sent {name} (encrypted {len(framed)}B)')

    print('Type command name to send, or "quit" to exit:')
    print(f'  Available: {", ".join(CMDS.keys())}')
    print('─' * 60)

    try:
        while True:
            try:
                cmd = input().strip().lower()
            except EOFError:
                break
            if cmd == 'quit' or cmd == 'q':
                break
            if cmd == '':
                continue
            send_cmd(cmd)
    except KeyboardInterrupt:
        pass

    client.loop_stop()
    client.disconnect()
    print('\nDone.')

if __name__ == '__main__':
    main()
