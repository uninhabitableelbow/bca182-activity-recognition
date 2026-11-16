"""Pretends to be the RT-Spark board: publishes real dataset readings over MQTT.

Sends one message per second with 5 samples (5 Hz), in the same JSON format
the firmware uses, so the cloud service can be tested end to end without
hardware. It alternates between a walking and a running stretch of the
dataset so the prediction visibly changes.

Usage:
    python tools/fake_device.py --data D:/lab3-downloads/dataset/dataset.csv \
        [--host 127.0.0.1] [--seconds 60] [--switch 15] [--fast]
"""
from __future__ import annotations

import argparse
import json
import time

import pandas as pd
import paho.mqtt.client as mqtt

CHANNELS = ["acceleration_x", "acceleration_y", "acceleration_z", "gyro_x", "gyro_y", "gyro_z"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--device", default="fake-device")
    ap.add_argument("--seconds", type=int, default=60)
    ap.add_argument("--switch", type=int, default=15, help="seconds per activity before switching")
    ap.add_argument("--fast", action="store_true", help="don't wait 1 s between messages")
    a = ap.parse_args()

    d = pd.read_csv(a.data)
    walk = d[d.activity == 0][CHANNELS].to_numpy().round(4)
    run = d[d.activity == 1][CHANNELS].to_numpy().round(4)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=a.device)
    client.connect(a.host, a.port, keepalive=30)
    client.loop_start()
    topic = f"bca182/activity/{a.device}/sensor"
    pos = {"walk": 0, "run": 0}
    for seq in range(a.seconds):
        label = "walk" if (seq // a.switch) % 2 == 0 else "run"
        src = walk if label == "walk" else run
        i = pos[label]
        pos[label] += 5
        msg = {"device": a.device, "seq": seq, "samples": src[i:i + 5].tolist()}
        client.publish(topic, json.dumps(msg), qos=1).wait_for_publish()
        print(f"seq={seq:3d} sent {label}")
        if not a.fast:
            time.sleep(1)
    client.loop_stop()
    client.disconnect()


if __name__ == "__main__":
    main()
