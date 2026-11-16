"""Movement classifier: MQTT subscriber -> ML model -> database.

Subscribes to   bca182/activity/<device>/sensor
Message (JSON, one per second from the board):
    {"device": "rt-spark-01", "seq": 42,
     "samples": [[ax, ay, az, gx, gy, gz], ...]}   # 5 samples at 5 Hz, g and rad/s

For every device it keeps the most recent <window> samples. Once the buffer
is full, each new message produces one prediction, which is stored in SQLite
together with the raw readings.

Environment: MQTT_HOST (default 127.0.0.1), MQTT_PORT (1883), DB_PATH, MODEL_DIR.
"""
from __future__ import annotations

import json
import logging
import os
import sys
import time
from collections import deque
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import paho.mqtt.client as mqtt

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ml"))
from activity_model import ActivityClassifier  # noqa: E402

import db  # noqa: E402

TOPIC = "bca182/activity/+/sensor"
MAX_GAP_S = 3.0          # longer silence from a device resets its buffer
log = logging.getLogger("classifier")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def parse_message(payload: bytes):
    """Validates a sensor message. Returns (device, seq, samples) or raises ValueError."""
    try:
        msg = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        raise ValueError(f"not JSON: {e}") from None
    if not isinstance(msg, dict):
        raise ValueError("message must be a JSON object")
    device = msg.get("device")
    if not isinstance(device, str) or not device or len(device) > 64:
        raise ValueError("missing or invalid 'device'")
    samples = msg.get("samples")
    if not isinstance(samples, list) or not 1 <= len(samples) <= 50:
        raise ValueError("'samples' must be a list of 1-50 readings")
    arr = np.asarray(samples, dtype=np.float32)
    if arr.ndim != 2 or arr.shape[1] != 6 or not np.isfinite(arr).all():
        raise ValueError("each sample must be 6 finite numbers [ax,ay,az,gx,gy,gz]")
    seq = msg.get("seq")
    if seq is not None and not isinstance(seq, int):
        raise ValueError("'seq' must be an integer")
    return device, seq, arr


class ClassifierService:
    def __init__(self, classifier: ActivityClassifier, conn):
        self.clf = classifier
        self.conn = conn
        self.buffers: dict[str, deque] = {}
        self.last_seen: dict[str, float] = {}
        self.last_seq: dict[str, int] = {}

    def handle(self, payload: bytes, now: float | None = None):
        """Processes one MQTT payload. Returns the stored prediction dict or None."""
        now = time.monotonic() if now is None else now
        device, seq, samples = parse_message(payload)

        buf = self.buffers.setdefault(device, deque(maxlen=self.clf.window))
        gap = now - self.last_seen.get(device, now)
        prev_seq = self.last_seq.get(device)
        if gap > MAX_GAP_S or (seq is not None and prev_seq is not None and seq != prev_seq + 1):
            buf.clear()                      # don't mix readings across a break
        self.last_seen[device] = now
        if seq is not None:
            self.last_seq[device] = seq
        buf.extend(samples)

        ts = utc_now()
        db.insert_reading(self.conn, device, seq, ts, samples.round(4).tolist())
        result = None
        if len(buf) == self.clf.window:
            activity, conf = self.clf.predict(np.asarray(buf))
            db.insert_prediction(self.conn, device, seq, ts, activity, conf)
            result = {"device": device, "seq": seq, "created_at": ts,
                      "activity": activity, "confidence": conf}
        self.conn.commit()
        return result


def main():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(message)s")
    model_dir = os.environ.get("MODEL_DIR", ROOT / "ml" / "models")
    service = ClassifierService(ActivityClassifier(model_dir), db.connect())
    log.info("model %s, window %d samples", service.clf.meta["architecture"], service.clf.window)

    def on_connect(client, userdata, flags, reason_code, properties):
        log.info("connected to broker (%s), subscribing %s", reason_code, TOPIC)
        client.subscribe(TOPIC, qos=1)

    def on_message(client, userdata, msg):
        try:
            r = service.handle(msg.payload)
            if r:
                log.info("%s seq=%s -> %s (%.2f)", r["device"], r["seq"], r["activity"],
                         r["confidence"])
        except ValueError as e:
            log.warning("rejected message on %s: %s", msg.topic, e)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="activity-classifier")
    client.on_connect = on_connect
    client.on_message = on_message
    client.reconnect_delay_set(1, 30)
    client.connect(os.environ.get("MQTT_HOST", "127.0.0.1"),
                   int(os.environ.get("MQTT_PORT", "1883")), keepalive=30)
    client.loop_forever(retry_first_connection=True)


if __name__ == "__main__":
    main()
