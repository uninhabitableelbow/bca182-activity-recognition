"""SQLite storage shared by the classifier service and the web API."""
from __future__ import annotations

import json
import os
import sqlite3
from pathlib import Path

DB_PATH = Path(os.environ.get("DB_PATH", Path(__file__).resolve().parent / "data" / "activity.db"))

SCHEMA = """
CREATE TABLE IF NOT EXISTS readings (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    device      TEXT    NOT NULL,
    seq         INTEGER,
    received_at TEXT    NOT NULL,            -- UTC ISO-8601
    samples     TEXT    NOT NULL             -- JSON [[ax,ay,az,gx,gy,gz], ...]
);
CREATE TABLE IF NOT EXISTS predictions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    device      TEXT    NOT NULL,
    seq         INTEGER,
    created_at  TEXT    NOT NULL,            -- UTC ISO-8601
    activity    TEXT    NOT NULL,            -- 'walk' or 'run'
    confidence  REAL    NOT NULL             -- probability of that activity, 0..1
);
CREATE INDEX IF NOT EXISTS idx_pred_device_time ON predictions(device, created_at);
CREATE INDEX IF NOT EXISTS idx_read_device_time ON readings(device, received_at);
"""


def connect(path: Path | str = DB_PATH) -> sqlite3.Connection:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(path, timeout=10, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")      # API reads while the classifier writes
    conn.executescript(SCHEMA)
    return conn


def insert_reading(conn, device: str, seq, received_at: str, samples) -> None:
    conn.execute("INSERT INTO readings(device, seq, received_at, samples) VALUES (?,?,?,?)",
                 (device, seq, received_at, json.dumps(samples)))


def insert_prediction(conn, device: str, seq, created_at: str, activity: str,
                      confidence: float) -> None:
    conn.execute("INSERT INTO predictions(device, seq, created_at, activity, confidence) "
                 "VALUES (?,?,?,?,?)", (device, seq, created_at, activity, round(confidence, 4)))
