"""Tests for the cloud service: message validation, buffering, storage and API.

Run:  python -m unittest discover -s tests -v
Uses a stub classifier and a temporary SQLite file, so no broker or trained
model is needed.
"""
import importlib
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "cloud"))
sys.path.insert(0, str(ROOT / "ml"))


class StubClassifier:
    """'run' when mean |gyro| is large, else 'walk'."""
    window = 10
    meta = {"architecture": "stub"}

    def predict(self, samples):
        assert samples.shape == (10, 6)
        return ("run", 0.9) if np.abs(samples[:, 3:]).mean() > 1 else ("walk", 0.8)


def msg(seq, gyro=0.1, device="dev1", n=5):
    return json.dumps({"device": device, "seq": seq,
                       "samples": [[0, -1, 0, gyro, gyro, gyro]] * n}).encode()


class CloudTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        os.environ["DB_PATH"] = str(Path(self.tmp.name) / "test.db")
        import db
        importlib.reload(db)
        import classifier_service
        importlib.reload(classifier_service)
        self.db = db
        self.cs = classifier_service
        self.conn = db.connect()
        self.service = classifier_service.ClassifierService(StubClassifier(), self.conn)

    def tearDown(self):
        self.conn.close()
        if "api" in sys.modules:
            sys.modules["api"].conn.close()
            del sys.modules["api"]
        self.tmp.cleanup()


class TestParseMessage(CloudTestCase):
    def test_valid(self):
        device, seq, arr = self.cs.parse_message(msg(3))
        self.assertEqual((device, seq, arr.shape), ("dev1", 3, (5, 6)))

    def test_rejects_bad_messages(self):
        bad = [b"not json", b"[1,2]", json.dumps({"samples": [[0] * 6]}).encode(),
               json.dumps({"device": "d", "samples": [[0] * 5]}).encode(),
               json.dumps({"device": "d", "samples": []}).encode(),
               json.dumps({"device": "d", "samples": [[0] * 6] * 51}).encode(),
               json.dumps({"device": "d", "seq": "1", "samples": [[0] * 6]}).encode(),
               b'{"device": "d", "samples": [[NaN,0,0,0,0,0]]}']
        for b in bad:
            with self.subTest(b=b), self.assertRaises(ValueError):
                self.cs.parse_message(b)


class TestBuffering(CloudTestCase):
    def test_first_prediction_after_window_fills(self):
        self.assertIsNone(self.service.handle(msg(0), now=0))      # 5 samples
        r = self.service.handle(msg(1), now=1)                     # 10 samples
        self.assertEqual(r["activity"], "walk")
        self.assertEqual(self.conn.execute("SELECT COUNT(*) FROM readings").fetchone()[0], 2)
        self.assertEqual(self.conn.execute("SELECT COUNT(*) FROM predictions").fetchone()[0], 1)

    def test_one_prediction_per_message_after_fill(self):
        for s in range(5):
            self.service.handle(msg(s, gyro=2), now=s)
        rows = self.conn.execute("SELECT activity FROM predictions").fetchall()
        self.assertEqual([r[0] for r in rows], ["run"] * 4)

    def test_sequence_gap_resets_buffer(self):
        self.service.handle(msg(0), now=0)
        self.service.handle(msg(1), now=1)
        self.assertIsNone(self.service.handle(msg(5), now=2))      # seq jumped

    def test_time_gap_resets_buffer(self):
        self.service.handle(msg(0), now=0)
        self.service.handle(msg(1), now=1)
        self.assertIsNone(self.service.handle(msg(2), now=10))     # 9 s silence

    def test_devices_are_independent(self):
        self.service.handle(msg(0, device="a"), now=0)
        self.assertIsNone(self.service.handle(msg(0, device="b"), now=0))
        self.assertIsNotNone(self.service.handle(msg(1, device="a"), now=1))


class TestApi(CloudTestCase):
    def setUp(self):
        super().setUp()
        for s in range(6):
            self.service.handle(msg(s, gyro=2 if s >= 3 else 0.1), now=s)
        import api
        self.client = api.app.test_client()

    def test_health(self):
        j = self.client.get("/api/health").get_json()
        self.assertEqual((j["status"], j["predictions"]), ("ok", 5))

    def test_latest(self):
        j = self.client.get("/api/latest?device=dev1").get_json()
        self.assertEqual((j["activity"], j["seq"]), ("run", 5))
        self.assertTrue(j["live"])

    def test_latest_defaults_to_most_recent_device(self):
        self.assertEqual(self.client.get("/api/latest").get_json()["device"], "dev1")

    def test_latest_unknown_device_is_404(self):
        self.assertEqual(self.client.get("/api/latest?device=nope").status_code, 404)

    def test_history_newest_first_and_paging(self):
        items = self.client.get("/api/history?device=dev1&limit=3").get_json()["items"]
        self.assertEqual([i["seq"] for i in items], [5, 4, 3])
        more = self.client.get(f"/api/history?device=dev1&limit=3&before_id={items[-1]['id']}")
        self.assertEqual([i["seq"] for i in more.get_json()["items"]], [2, 1])

    def test_history_bad_limit(self):
        self.assertEqual(self.client.get("/api/history?limit=abc").status_code, 400)

    def test_summary(self):
        j = self.client.get("/api/summary?device=dev1&minutes=5").get_json()
        self.assertEqual(j["seconds"]["walk"] + j["seconds"]["run"], 5)
        self.assertGreater(j["seconds"]["run"], 0)

    def test_devices(self):
        j = self.client.get("/api/devices").get_json()
        self.assertEqual([d["device"] for d in j], ["dev1"])

    def test_dashboard_served(self):
        r = self.client.get("/")
        self.assertIn(b"Activity Recognition", r.data)
        r.close()


if __name__ == "__main__":
    unittest.main()
