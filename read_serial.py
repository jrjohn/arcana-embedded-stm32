#!/usr/bin/env python3
"""Simple serial monitor for STM32 debug output."""

import serial
import sys
import time

SERIAL_PORT = '/dev/tty.usbserial-1120'
BAUD_RATE = 115200

def main():
    try:
        print(f'Opening {SERIAL_PORT} at {BAUD_RATE} baud...')
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
        print('Connected! Press Ctrl+C to exit.\n')
        
        while True:
            try:
                if ser.in_waiting > 0:
                    data = ser.read(ser.in_waiting)
                    text = data.decode('utf-8', errors='ignore')
                    print(text, end='', flush=True)
                time.sleep(0.01)
            except serial.SerialException:
                # Transient USB serial hiccup (e.g. board reset) — retry
                time.sleep(0.5)
                try:
                    ser.close()
                except Exception:
                    pass
                ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
                print('\n[reconnected]\n', flush=True)

    except KeyboardInterrupt:
        print('\n\nExiting...')
    except Exception as e:
        print(f'Error: {e}')
        sys.exit(1)
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()

if __name__ == '__main__':
    main()
