#!/usr/bin/env python3
"""Long-running MQTT monitor — verify sustained sensor stream + encrypted commands."""

import os, ssl, json, time, sys
import paho.mqtt.client as mqtt

BROKER = os.environ.get("MQTT_BROKER", "mqtt.example.com")
PORT = int(os.environ.get("MQTT_PORT", "443"))

count = 0
start_time = time.time()
drops = 0
last_seq = -1

def on_connect(c, ud, fl, rc, p=None):
    c.subscribe("/arcana/#")
    print(f"Connected to {BROKER}:{PORT} (WSS)")
    print(f"{'Time':>10} {'Topic':<18} {'Info'}")
    print("-" * 65)

def on_message(c, ud, msg):
    global count, drops
    count += 1
    ts = time.strftime("%H:%M:%S")
    elapsed = time.time() - start_time

    if msg.topic == "/arcana/sensor":
        try:
            d = json.loads(msg.payload)
            # Print every 10th to avoid spam
            if count % 10 == 1:
                t = d.get("t", "?")
                ax = d.get("ax", "?")
                ay = d.get("ay", "?")
                az = d.get("az", "?")
                print(f"{ts:>10} sensor            t={t}C ax={ax} ay={ay} az={az}")
        except:
            pass

    elif msg.topic == "/arcana/rsp":
        print(f"{ts:>10} rsp (encrypted)   {len(msg.payload)}B")

    elif msg.topic == "/arcana/cmd":
        print(f"{ts:>10} cmd               {len(msg.payload)}B")

    else:
        print(f"{ts:>10} {msg.topic:<18} {len(msg.payload)}B")

    # Stats every 30 messages
    if count % 30 == 0:
        rate = count / elapsed if elapsed > 0 else 0
        print(f"{'':>10} --- {count} msgs / {int(elapsed)}s / {rate:.1f} msg/s ---")

def on_disconnect(c, ud, fl, rc, p=None):
    print(f"[WARN] Disconnected rc={rc}, reconnecting...")

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                     client_id="arcana_longtest",
                     transport="websockets")
client.username_pw_set(os.environ.get("MQTT_USER", "user"), os.environ.get("MQTT_PASS", "pass"))
client.tls_set(cert_reqs=ssl.CERT_REQUIRED)
client.ws_set_options(path="/mqtt")
client.on_connect = on_connect
client.on_message = on_message
client.on_disconnect = on_disconnect
client.reconnect_delay_set(min_delay=1, max_delay=30)

print("Arcana MQTT Long-Run Test (Ctrl+C to stop)")
client.connect(BROKER, PORT, 60)
start_time = time.time()

try:
    client.loop_forever()
except KeyboardInterrupt:
    elapsed = time.time() - start_time
    rate = count / elapsed if elapsed > 0 else 0
    print(f"\n{'='*65}")
    print(f"Total: {count} msgs in {int(elapsed)}s ({rate:.1f} msg/s)")
    print(f"Drops: {drops}")
    client.loop_stop()
    client.disconnect()
