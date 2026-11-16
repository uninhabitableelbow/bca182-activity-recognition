"""Train and evaluate walk/run classifiers on the Kaggle run-or-walk dataset.

Usage:
    python ml/train.py --data D:/lab3-downloads/dataset/dataset.csv

What it does (follows the "Machine Learning Models" section of the lab PDF):
  1. Splits the recording into continuous segments and cuts sliding windows
     so the models can learn temporal patterns.
  2. Compares a time-insensitive baseline with two small temporal models
     (1D CNN, GRU) and several window lengths, starting simple.
  3. Tests generalisation three ways:
       - leave-one-day-out (never test on a day the model trained on)
       - cross-wrist (train on one wrist, test on the other)
       - random 3D rotation of the test data (the board is not held like a watch),
         then trains with random rotations and picks the most orientation-robust model
  4. Trains the chosen configuration on all data and saves it to ml/models/.
Results go to ml/results/results.json and ml/results/results.md.
"""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import numpy as np
import pandas as pd
import torch
from torch import nn

from activity_model import ARCHITECTURES, CHANNELS, SAMPLE_RATE_HZ, count_parameters

ROOT = Path(__file__).resolve().parent
SEED = 182
MAX_GAP_S = 0.5          # a larger gap between samples starts a new segment


# ---------------------------------------------------------------- data ---
def load_segments(csv_path: Path):
    """Returns a list of (samples[n,6], label, wrist, day) continuous segments."""
    d = pd.read_csv(csv_path)
    hms, ns = d.time.str.rsplit(":", n=1).str[0], d.time.str.rsplit(":", n=1).str[1]
    t = (pd.to_datetime(d.date + " " + hms, format="%Y-%m-%d %H:%M:%S")
         + pd.to_timedelta(ns.astype(np.int64), unit="ns"))
    dt = t.diff().dt.total_seconds().to_numpy()
    new = ((dt <= 0) | (dt > MAX_GAP_S) | np.isnan(dt)
           | (d.activity.diff().fillna(1) != 0).to_numpy()
           | (d.wrist.diff().fillna(1) != 0).to_numpy()
           | (d.date != d.date.shift()).to_numpy())
    seg_id = np.cumsum(new)
    x = d[CHANNELS].to_numpy(np.float32)
    segs = []
    for _, idx in pd.Series(np.arange(len(d))).groupby(seg_id):
        i = idx.to_numpy()
        segs.append((x[i], int(d.activity.iat[i[0]]), int(d.wrist.iat[i[0]]), d.date.iat[i[0]]))
    return segs


def make_windows(segs, window: int):
    xs, ys, wrists, days = [], [], [], []
    for s, y, w, day in segs:
        if len(s) < window:
            continue
        idx = np.arange(window)[None, :] + np.arange(len(s) - window + 1)[:, None]
        xs.append(s[idx])
        n = len(idx)
        ys.append(np.full(n, y)); wrists.append(np.full(n, w)); days.append(np.full(n, day))
    return (np.concatenate(xs), np.concatenate(ys), np.concatenate(wrists),
            np.concatenate(days))


def normaliser(x):
    """Rotation-invariant scaling: zero mean, one shared std per sensor."""
    mean = np.zeros(6, np.float32)
    std = np.empty(6, np.float32)
    std[:3] = x[..., :3].std()
    std[3:] = x[..., 3:].std()
    return mean, std


def random_rotations(n, rng):
    """n uniformly random 3x3 rotation matrices (QR of a Gaussian matrix)."""
    q, r = np.linalg.qr(rng.normal(size=(n, 3, 3)))
    q *= np.sign(np.diagonal(r, axis1=1, axis2=2))[:, None, :]
    det = np.linalg.det(q)
    q[det < 0, :, 0] *= -1
    return q.astype(np.float32)


def rotate(x, rng):
    """Applies one random rotation per window to both accel and gyro axes."""
    r = random_rotations(len(x), rng)
    out = x.copy()
    out[..., :3] = np.einsum("nij,ntj->nti", r, x[..., :3])
    out[..., 3:] = np.einsum("nij,ntj->nti", r, x[..., 3:])
    return out


# ------------------------------------------------------------ training ---
def train_model(arch, window, x, y, *, augment=False, epochs=5, seed=SEED, kwargs=None):
    torch.manual_seed(seed)
    rng = np.random.default_rng(seed)
    kwargs = kwargs or {}
    mean, std = normaliser(x)
    model = ARCHITECTURES[arch](window, **kwargs)
    opt = torch.optim.Adam(model.parameters(), lr=3e-3)
    loss_fn = nn.CrossEntropyLoss()
    yt = torch.from_numpy(y).long()
    for _ in range(epochs):
        xe = rotate(x, rng) if augment else x
        xe = torch.from_numpy(((xe - mean) / std).transpose(0, 2, 1).copy())
        perm = torch.randperm(len(xe))
        model.train()
        for i in range(0, len(xe), 256):
            b = perm[i:i + 256]
            opt.zero_grad()
            loss_fn(model(xe[b]), yt[b]).backward()
            opt.step()
    model.eval()
    return model, mean, std


def predict(model, mean, std, x):
    with torch.no_grad():
        xt = torch.from_numpy(((x - mean) / std).transpose(0, 2, 1).copy())
        return torch.cat([model(xt[i:i + 4096]).argmax(1) for i in range(0, len(xt), 4096)]).numpy()


def scores(y, p):
    tp = int(((p == 1) & (y == 1)).sum()); tn = int(((p == 0) & (y == 0)).sum())
    fp = int(((p == 1) & (y == 0)).sum()); fn = int(((p == 0) & (y == 1)).sum())
    acc = (tp + tn) / len(y)
    rec_run = tp / max(tp + fn, 1); rec_walk = tn / max(tn + fp, 1)
    prec_run = tp / max(tp + fp, 1)
    f1 = 2 * prec_run * rec_run / max(prec_run + rec_run, 1e-9)
    return {"accuracy": round(acc, 4), "balanced_accuracy": round((rec_run + rec_walk) / 2, 4),
            "f1_run": round(f1, 4), "confusion": {"tn": tn, "fp": fp, "fn": fn, "tp": tp}}


def leave_one_day_out(arch, window, segs, *, augment=False, kwargs=None, epochs=5):
    """Trains once per held-out day; tests each model on the original and on
    randomly rotated copies of that day. Returns (plain_scores, rotated_scores)."""
    x, y, _, days = make_windows(segs, window)
    rng = np.random.default_rng(SEED + 1)
    preds, preds_rot = np.empty_like(y), np.empty_like(y)
    for day in np.unique(days):
        tr, te = days != day, days == day
        model, mean, std = train_model(arch, window, x[tr], y[tr], augment=augment,
                                       kwargs=kwargs, epochs=epochs)
        preds[te] = predict(model, mean, std, x[te])
        preds_rot[te] = predict(model, mean, std, rotate(x[te], rng))
    return scores(y, preds), scores(y, preds_rot)


def cross_wrist(arch, window, segs, *, augment=False, kwargs=None, epochs=5):
    x, y, wr, _ = make_windows(segs, window)
    out = {}
    for train_w in (0, 1):
        model, mean, std = train_model(arch, window, x[wr == train_w], y[wr == train_w],
                                       augment=augment, kwargs=kwargs, epochs=epochs)
        out[f"train_wrist{train_w}_test_wrist{1 - train_w}"] = scores(
            y[wr != train_w], predict(model, mean, std, x[wr != train_w]))
    return out


# ---------------------------------------------------------------- main ---
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, type=Path)
    ap.add_argument("--out", default=ROOT / "models", type=Path)
    args = ap.parse_args()

    t0 = time.time()
    segs = load_segments(args.data)
    lengths = np.array([len(s[0]) for s in segs])
    print(f"{len(segs)} segments, median {np.median(lengths):.0f} samples", flush=True)

    results = {"dataset": {"rows": int(lengths.sum()), "segments": len(segs),
                           "days": len({s[3] for s in segs}),
                           "sample_rate_hz": SAMPLE_RATE_HZ},
               "stage1_architectures": [], "stage2_windows": [], "stage3_robustness": []}

    # Stage 1: start simple, add complexity (2 s window = 10 samples).
    for arch in ("logistic", "cnn", "gru"):
        w = 10
        params = count_parameters(ARCHITECTURES[arch](w))
        plain, rot = leave_one_day_out(arch, w, segs)
        results["stage1_architectures"].append({"architecture": arch, "window": w,
                                                "parameters": params, **plain,
                                                "rotated": rot})
        print(f"stage1 {arch:8s} params={params:4d} LODO acc={plain['accuracy']:.4f} "
              f"rotated={rot['accuracy']:.4f}", flush=True)

    # Stage 2: window length for the smaller of the two temporal models.
    best_arch = max((r for r in results["stage1_architectures"] if r["architecture"] != "logistic"),
                    key=lambda r: (round(r["balanced_accuracy"], 3), -r["parameters"]))["architecture"]
    for w in (5, 10, 20):
        plain, rot = leave_one_day_out(best_arch, w, segs)
        results["stage2_windows"].append({"architecture": best_arch, "window": w,
                                          "window_seconds": w / SAMPLE_RATE_HZ, **plain,
                                          "rotated": rot})
        print(f"stage2 {best_arch} window={w:2d} LODO acc={plain['accuracy']:.4f} "
              f"rotated={rot['accuracy']:.4f}", flush=True)

    # Stage 3: robustness to device orientation. The board is not worn like the
    # watch that recorded the dataset, so train with random 3D rotations and
    # choose by accuracy on rotated held-out days.
    for w in (5, 10, 20):
        for hidden in (8, 16):
            kw = {"hidden": hidden}
            plain, rot = leave_one_day_out(best_arch, w, segs, augment=True, kwargs=kw, epochs=8)
            results["stage3_robustness"].append({
                "architecture": best_arch, "window": w, "hidden": hidden,
                "parameters": count_parameters(ARCHITECTURES[best_arch](w, **kw)),
                "augmented": True, **plain, "rotated": rot})
            print(f"stage3 {best_arch} window={w:2d} hidden={hidden:2d} "
                  f"LODO acc={plain['accuracy']:.4f} rotated={rot['accuracy']:.4f}", flush=True)
    # Best rotated balanced accuracy; within 0.5 points prefer fewer parameters,
    # then the shorter window (faster response).
    top = max(r["rotated"]["balanced_accuracy"] for r in results["stage3_robustness"])
    best = min((r for r in results["stage3_robustness"]
                if r["rotated"]["balanced_accuracy"] >= top - 0.005),
               key=lambda r: (r["parameters"], r["window"]))
    best_w, kw = best["window"], {"hidden": best["hidden"]}
    results["cross_wrist"] = {
        "plain": cross_wrist(best_arch, best_w, segs, kwargs=kw, epochs=8),
        "augmented": cross_wrist(best_arch, best_w, segs, augment=True, kwargs=kw, epochs=8)}
    print("cross-wrist " + json.dumps({k: {kk: vv["accuracy"] for kk, vv in v.items()}
                                       for k, v in results["cross_wrist"].items()}), flush=True)

    # Final model on all data.
    x, y, _, _ = make_windows(segs, best_w)
    model, mean, std = train_model(best_arch, best_w, x, y, augment=True, kwargs=kw, epochs=8)
    args.out.mkdir(parents=True, exist_ok=True)
    torch.save(model.state_dict(), args.out / "model.pt")
    meta = {"architecture": best_arch, "window": best_w, "kwargs": kw,
            "rotation_augmentation": True, "parameters": count_parameters(model),
            "mean": mean.tolist(), "std": std.tolist(), "channels": CHANNELS,
            "units": {"acceleration": "g", "gyro": "rad/s"},
            "sample_rate_hz": SAMPLE_RATE_HZ, "labels": {"0": "walk", "1": "run"},
            "train_accuracy": scores(y, predict(model, mean, std, x))["accuracy"],
            "heldout_day_accuracy": best["accuracy"],
            "heldout_day_rotated_accuracy": best["rotated"]["accuracy"]}
    (args.out / "model_meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    results["final_model"] = meta
    results["training_seconds"] = round(time.time() - t0, 1)

    res_dir = ROOT / "results"
    res_dir.mkdir(exist_ok=True)
    (res_dir / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    (res_dir / "results.md").write_text(to_markdown(results), encoding="utf-8")
    print(f"final: {best_arch} window={best_w} hidden={kw['hidden']} "
          f"params={meta['parameters']} ({results['training_seconds']} s)", flush=True)


def to_markdown(r):
    pct = lambda v: f"{100 * v:.2f}%"
    lines = ["# Model results", "",
             f"Dataset: {r['dataset']['rows']} samples, {r['dataset']['segments']} continuous "
             f"segments over {r['dataset']['days']} days, {r['dataset']['sample_rate_hz']} Hz.",
             "All accuracies are leave-one-day-out: each day is predicted by a model that never "
             "saw that day. \"Rotated\" applies a random 3D rotation to every test window.", "",
             "## Stage 1: architectures (2 s window, no augmentation)", "",
             "| Model | Uses time? | Parameters | Accuracy | Balanced acc. | Rotated acc. |",
             "|---|---|---|---|---|---|"]
    for a in r["stage1_architectures"]:
        lines.append(f"| {a['architecture']} | {'no' if a['architecture'] == 'logistic' else 'yes'} "
                     f"| {a['parameters']} | {pct(a['accuracy'])} | {pct(a['balanced_accuracy'])} "
                     f"| {pct(a['rotated']['accuracy'])} |")
    lines += ["", "## Stage 2: window length (no augmentation)", "",
              "| Model | Window | Seconds | Accuracy | Rotated acc. |", "|---|---|---|---|---|"]
    for a in r["stage2_windows"]:
        lines.append(f"| {a['architecture']} | {a['window']} | {a['window_seconds']:.0f} "
                     f"| {pct(a['accuracy'])} | {pct(a['rotated']['accuracy'])} |")
    lines += ["", "## Stage 3: trained with random rotations", "",
              "| Model | Window | Filters | Parameters | Accuracy | Rotated acc. |",
              "|---|---|---|---|---|---|"]
    for a in r["stage3_robustness"]:
        lines.append(f"| {a['architecture']} | {a['window']} | {a['hidden']} | {a['parameters']} "
                     f"| {pct(a['accuracy'])} | {pct(a['rotated']['accuracy'])} |")
    cw = r["cross_wrist"]
    lines += ["", "## Cross-wrist (train on one wrist, test on the other)", "",
              "| Training | Wrist 0 -> 1 | Wrist 1 -> 0 |", "|---|---|---|"]
    for k, v in cw.items():
        lines.append(f"| {k} | {pct(v['train_wrist0_test_wrist1']['accuracy'])} "
                     f"| {pct(v['train_wrist1_test_wrist0']['accuracy'])} |")
    f = r["final_model"]
    lines += ["", "## Final model", "",
              f"{f['architecture']}, window {f['window']} samples "
              f"({f['window'] / f['sample_rate_hz']:.0f} s), {f['kwargs']['hidden']} filters, "
              f"{f['parameters']} parameters, trained on all data with rotation augmentation. "
              f"Held-out-day accuracy {pct(f['heldout_day_accuracy'])}, rotated "
              f"{pct(f['heldout_day_rotated_accuracy'])}.", ""]
    return "\n".join(lines)


if __name__ == "__main__":
    main()
