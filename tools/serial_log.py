"""Captures the board's serial log (ST-Link virtual COM port) for a while.

Usage:  python tools/serial_log.py [--port COM3] [--seconds 60] [--reset]
--reset pulses the board's reset through the ST-Link first (needs st-flash or
pio), otherwise it just listens.
"""
import argparse
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--out", help="also save the log to this file")
    a = ap.parse_args()

    out = open(a.out, "w", encoding="utf-8") if a.out else None
    with serial.Serial(a.port, a.baud, timeout=0.2) as s:
        end = time.time() + a.seconds
        buf = b""
        while time.time() < end:
            buf += s.read(4096)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").rstrip("\r")
                print(text, flush=True)
                if out:
                    out.write(text + "\n")
        if buf:
            print(buf.decode("utf-8", "replace"), flush=True)
    if out:
        out.close()


if __name__ == "__main__":
    sys.exit(main())
