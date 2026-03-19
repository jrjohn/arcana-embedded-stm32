#!/usr/bin/env python3
"""Read STM32 serial debug output via USB CDC."""
import serial, sys, glob

def find_port():
    return '/dev/tty.usbserial-1120'

port = find_port()
print(f"Connecting to {port} @ 115200...")
ser = serial.Serial(port, 115200, timeout=1)
ser.reset_input_buffer()
print("--- Listening (Ctrl+C to quit) ---")
try:
    while True:
        data = ser.read(ser.in_waiting or 1)
        if data:
            sys.stdout.write(data.decode('utf-8', errors='replace'))
            sys.stdout.flush()
except KeyboardInterrupt:
    print("\n--- Done ---")
finally:
    ser.close()
