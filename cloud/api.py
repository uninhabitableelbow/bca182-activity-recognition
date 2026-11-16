"""Web API for classified activity data (Flask).

Endpoints (all JSON, GET):
    /api/health                         service status
    /api/devices                        devices seen, with their last prediction time
    /api/latest?device=ID               most recent prediction
    /api/history?device=ID&limit=N&before_id=K
                                        predictions, newest first (paged by id)
    /api/summary?device=ID&minutes=M    walk/run counts and seconds in the last M minutes
    /                                   simple web dashboard

Environment: DB_PATH, API_HOST (default 0.0.0.0), API_PORT (default 5000).
"""
from __future__ import annotations

import os
from datetime import datetime, timedelta, timezone

from flask import Flask, jsonify, request, send_from_directory

import db

app = Flask(__name__, static_folder="static")
conn = db.connect()

# A device that sent nothing for this long is reported as not live.
LIVE_TIMEOUT_S = 5


def row(r):
    return dict(r) if r is not None else None


def device_arg():
    d = request.args.get("device")
    if d is None:
        first = conn.execute("SELECT device FROM predictions ORDER BY id DESC LIMIT 1").fetchone()
        return first["device"] if first else None
    return d


def int_arg(name, default, lo, hi):
    try:
        v = int(request.args.get(name, default))
    except ValueError:
        return None
    return max(lo, min(hi, v))


@app.after_request
def cors(resp):
    resp.headers["Access-Control-Allow-Origin"] = "*"
    return resp


@app.get("/api/health")
def health():
    n = conn.execute("SELECT COUNT(*) AS n FROM predictions").fetchone()["n"]
    return jsonify(status="ok", predictions=n, time=datetime.now(timezone.utc).isoformat())


@app.get("/api/devices")
def devices():
    rows = conn.execute("SELECT device, MAX(created_at) AS last_seen, COUNT(*) AS predictions "
                        "FROM predictions GROUP BY device ORDER BY last_seen DESC").fetchall()
    return jsonify([dict(r) for r in rows])


@app.get("/api/latest")
def latest():
    device = device_arg()
    r = conn.execute("SELECT * FROM predictions WHERE device = ? ORDER BY id DESC LIMIT 1",
                     (device,)).fetchone()
    if r is None:
        return jsonify(error="no predictions yet", device=device), 404
    out = row(r)
    age = (datetime.now(timezone.utc) - datetime.fromisoformat(out["created_at"])).total_seconds()
    out["age_seconds"] = round(age, 1)
    out["live"] = age <= LIVE_TIMEOUT_S
    return jsonify(out)


@app.get("/api/history")
def history():
    device = device_arg()
    limit = int_arg("limit", 50, 1, 500)
    before = int_arg("before_id", 2**62, 1, 2**62)
    if limit is None or before is None:
        return jsonify(error="limit and before_id must be integers"), 400
    rows = conn.execute("SELECT * FROM predictions WHERE device = ? AND id < ? "
                        "ORDER BY id DESC LIMIT ?", (device, before, limit)).fetchall()
    return jsonify(device=device, items=[dict(r) for r in rows])


@app.get("/api/summary")
def summary():
    device = device_arg()
    minutes = int_arg("minutes", 60, 1, 60 * 24 * 30)
    if minutes is None:
        return jsonify(error="minutes must be an integer"), 400
    since = (datetime.now(timezone.utc) - timedelta(minutes=minutes)).isoformat()
    rows = conn.execute("SELECT activity, COUNT(*) AS n FROM predictions "
                        "WHERE device = ? AND created_at >= ? GROUP BY activity",
                        (device, since)).fetchall()
    # One prediction per message and one message per second, so counts are seconds.
    counts = {"walk": 0, "run": 0, **{r["activity"]: r["n"] for r in rows}}
    return jsonify(device=device, minutes=minutes, seconds=counts)


@app.get("/")
def index():
    return send_from_directory(app.static_folder, "index.html")


if __name__ == "__main__":
    app.run(host=os.environ.get("API_HOST", "0.0.0.0"), port=int(os.environ.get("API_PORT", "5000")),
            threaded=True)
