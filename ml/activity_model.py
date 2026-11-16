"""Shared model definitions and preprocessing for the activity classifier.

Used by ml/train.py (training) and cloud/classifier_service.py (inference),
so both sides window and normalise sensor data in exactly the same way.

Units (inferred from the Kaggle run-or-walk data, see docs):
    acceleration_x/y/z  in g      (gravity included)
    gyro_x/y/z          in rad/s
Sample rate: 5 Hz (one sample every 0.2 s).
"""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import torch
from torch import nn

CHANNELS = ["acceleration_x", "acceleration_y", "acceleration_z",
            "gyro_x", "gyro_y", "gyro_z"]
LABELS = {0: "walk", 1: "run"}
SAMPLE_RATE_HZ = 5


class LogisticSingleSample(nn.Module):
    """Time-insensitive baseline: classifies one 6-axis reading on its own."""

    def __init__(self, window: int):
        super().__init__()
        self.window = window
        self.linear = nn.Linear(len(CHANNELS), 2)

    def forward(self, x):            # x: (batch, channels, window)
        return self.linear(x[:, :, -1])


class TinyCNN(nn.Module):
    """Small 1D convolutional network over a window of readings.

    Convolutions along time learn short temporal patterns (the stride of a
    step or a running arm swing) across all six axes at once.
    """

    def __init__(self, window: int, hidden: int = 8):
        super().__init__()
        self.window = window
        self.features = nn.Sequential(
            nn.Conv1d(len(CHANNELS), hidden, kernel_size=3, padding=1),
            nn.ReLU(),
            nn.Conv1d(hidden, hidden, kernel_size=3, padding=1),
            nn.ReLU(),
            nn.AdaptiveAvgPool1d(1),
        )
        self.head = nn.Linear(hidden, 2)

    def forward(self, x):
        return self.head(self.features(x).squeeze(-1))


class TinyGRU(nn.Module):
    """Small recurrent network: reads the window one sample at a time."""

    def __init__(self, window: int, hidden: int = 8):
        super().__init__()
        self.window = window
        self.gru = nn.GRU(len(CHANNELS), hidden, batch_first=True)
        self.head = nn.Linear(hidden, 2)

    def forward(self, x):
        _, h = self.gru(x.transpose(1, 2))
        return self.head(h[-1])


ARCHITECTURES = {
    "logistic": LogisticSingleSample,
    "cnn": TinyCNN,
    "gru": TinyGRU,
}


def count_parameters(model: nn.Module) -> int:
    return sum(p.numel() for p in model.parameters())


class ActivityClassifier:
    """Loads a trained model and classifies windows of raw sensor readings."""

    def __init__(self, model_dir: str | Path):
        model_dir = Path(model_dir)
        meta = json.loads((model_dir / "model_meta.json").read_text())
        self.meta = meta
        self.window = meta["window"]
        self.mean = np.asarray(meta["mean"], dtype=np.float32)
        self.std = np.asarray(meta["std"], dtype=np.float32)
        self.model = ARCHITECTURES[meta["architecture"]](self.window, **meta.get("kwargs", {}))
        self.model.load_state_dict(torch.load(model_dir / "model.pt", map_location="cpu",
                                              weights_only=True))
        self.model.eval()

    def predict(self, samples: np.ndarray) -> tuple[str, float]:
        """samples: (window, 6) array in g and rad/s, oldest first.

        Returns (label, probability of that label).
        """
        samples = np.asarray(samples, dtype=np.float32)
        if samples.shape != (self.window, len(CHANNELS)):
            raise ValueError(f"expected ({self.window}, 6) samples, got {samples.shape}")
        x = ((samples - self.mean) / self.std).T[None, :, :]
        with torch.no_grad():
            p = torch.softmax(self.model(torch.from_numpy(x)), dim=1)[0].numpy()
        k = int(p.argmax())
        return LABELS[k], float(p[k])
