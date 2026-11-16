# Model results

Dataset: 88588 samples, 952 continuous segments over 12 days, 5 Hz.
All accuracies are leave-one-day-out: each day is predicted by a model that never saw that day. "Rotated" applies a random 3D rotation to every test window.

## Stage 1: architectures (2 s window, no augmentation)

| Model | Uses time? | Parameters | Accuracy | Balanced acc. | Rotated acc. |
|---|---|---|---|---|---|
| logistic | no | 14 | 78.21% | 78.17% | 48.39% |
| cnn | yes | 370 | 99.95% | 99.95% | 57.61% |
| gru | yes | 402 | 99.93% | 99.93% | 58.36% |

## Stage 2: window length (no augmentation)

| Model | Window | Seconds | Accuracy | Rotated acc. |
|---|---|---|---|---|
| cnn | 5 | 1 | 99.93% | 58.97% |
| cnn | 10 | 2 | 99.95% | 57.61% |
| cnn | 20 | 4 | 99.98% | 56.15% |

## Stage 3: trained with random rotations

| Model | Window | Filters | Parameters | Accuracy | Rotated acc. |
|---|---|---|---|---|---|
| cnn | 5 | 8 | 370 | 98.61% | 98.71% |
| cnn | 5 | 16 | 1122 | 99.51% | 99.47% |
| cnn | 10 | 8 | 370 | 99.49% | 99.65% |
| cnn | 10 | 16 | 1122 | 99.61% | 99.66% |
| cnn | 20 | 8 | 370 | 99.77% | 99.81% |
| cnn | 20 | 16 | 1122 | 99.93% | 99.87% |

## Cross-wrist (train on one wrist, test on the other)

| Training | Wrist 0 -> 1 | Wrist 1 -> 0 |
|---|---|---|
| plain | 98.02% | 74.71% |
| augmented | 99.04% | 98.77% |

## Final model

cnn, window 10 samples (2 s), 8 filters, 370 parameters, trained on all data with rotation augmentation. Held-out-day accuracy 99.49%, rotated 99.65%.
