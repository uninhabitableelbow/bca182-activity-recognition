# Requirements Traceability Matrix

Requirements are taken from *Laboratory Activity No. 3: Activity Recognition* (the page is given for each).
Functional tests (FT) are in [functional-verification.md](functional-verification.md). "Adapted" means the
lab sheet's exact item could not be used, and the reason is in the README's
[differences table](../README.md#differences-from-the-lab-sheet).

## Cloud service and machine learning (Task 1)

| ID | Requirement (page) | Implementation | Verification |
|---|---|---|---|
| CL-01 | MQTT broker receiving the device's messages (p. 3) | Mosquitto 2.1, [cloud/mosquitto.conf](../cloud/mosquitto.conf) | FT-13, FT-14 |
| CL-02 | Machine-learning classifier that classifies the sensor data into an activity (p. 3) | [cloud/classifier_service.py](../cloud/classifier_service.py) with [ml/activity_model.py](../ml/activity_model.py) | FT-05, FT-18 |
| CL-03 | Database storing the processed activity data; MariaDB, PostgreSQL or SQLite (pp. 2-3) | SQLite in [cloud/db.py](../cloud/db.py): `readings` and `predictions` tables | FT-04, FT-05 |
| CL-04 | Web API giving access to the stored data, and a web page (p. 3) | Flask, [cloud/api.py](../cloud/api.py): latest, history, summary, devices; dashboard [cloud/static/index.html](../cloud/static/index.html) | FT-04, FT-06, FT-07, FT-17 |
| CL-05 | Run the services on a cloud VM, or locally (Docker) (p. 3) | **Adapted:** the same services run natively on a PC on the same network | FT-14 |
| ML-01 | TensorFlow or PyTorch (p. 2) | PyTorch, [ml/train.py](../ml/train.py) | FT-01 |
| ML-02 | Kaggle run-or-walk dataset, in the right units (pp. 3-4) | Acceleration in g, rotation rate in rad/s, 5 Hz; the firmware sends the same units ([imu_convert.c](../firmware/App/Src/imu_convert.c)) | FT-10; unit tests |
| ML-03 | Use all axes, not the original author's model (p. 4) | All 6 channels (3 acceleration, 3 rotation rate) together, in a 1D CNN of my own design | [results.md](../ml/results/results.md) stage 1 |
| ML-04 | Generalise across rigorous testing scenarios (p. 4) | Leave-one-day-out, random 3D rotations, other wrist; trained with rotation augmentation | FT-01, FT-02, FT-03 |
| ML-05 | Use temporal information (p. 4) | 2 s window of 10 readings; CNN beats the single-reading logistic model (99.95% vs 78.2%) | results.md stages 1-2 |
| ML-06 | Keep it lightweight; start simple and add complexity (p. 4) | Logistic (14 params) -> CNN / GRU (370 / 402) -> chose the 370-parameter CNN; trains in seconds on a CPU | results.md |
| ML-07 | Probabilistic interpretation (p. 4) | Softmax output stored as the prediction's confidence | FT-18 (70% case) |

## Device firmware (Task 2)

| ID | Requirement (page) | Implementation | Verification |
|---|---|---|---|
| FW-01 | STM32CubeMX configuration, PlatformIO (pp. 2, 4) | [bca182-lab3-firmware.ioc](../firmware/bca182-lab3-firmware.ioc), [platformio.ini](../firmware/platformio.ini) | FT-08 |
| FW-02 | Use the on-board accelerometer and gyroscope (LSM6DSL) (p. 4) | **Adapted:** the board's ICM20608, [icm20608.c](../firmware/App/Src/icm20608.c) | FT-10 |
| FW-03 | Wi-Fi module on SPI with its global interrupt enabled (p. 4) | **Adapted:** RW007 on SPI2 with SPI2 and EXTI (INT pin) interrupts, [rw007.c](../firmware/App/Src/rw007.c) | FT-11 |
| FW-04 | CMSIS-Driver WiFi API (`Driver_WiFi0`) (pp. 5-7) | [Driver_WiFi_RW007.c](../firmware/App/Src/Driver_WiFi_RW007.c) implements `ARM_DRIVER_WIFI`, including sockets | FT-11, FT-12 |
| FW-05 | AWS coreMQTT (p. 5) | coreMQTT 5.0.2 over `Driver_WiFi0` sockets, [mqtt_link.c](../firmware/App/Src/mqtt_link.c) | FT-13 |
| FW-06 | `MQTT_DO_NOT_USE_CUSTOM_CONFIG` (p. 6) | Build flag in [platformio.ini](../firmware/platformio.ini) | FT-08 |
| FW-07 | Sequence: initialise sensors, initialise Wi-Fi, connect to the broker (p. 6) | [app.c](../firmware/App/Src/app.c): SensorTask initialises the IMU, then NetworkTask starts Wi-Fi and MQTT | FT-10 to FT-13 |
| FW-08 | Wi-Fi sample code, with error checking added (pp. 6-7) | The sheet's `Initialize`, `PowerControl`, `wifiConfig`, `Activate` code in `app_network_task()`, with return codes checked and retries | FT-12, FT-15 |
| FW-09 | A sensor reading every second, sent as an MQTT message (p. 6) | **Adapted:** one message per second holding that second's 5 readings (the model needs 5 Hz) | FT-14 |
| FW-10 | Message in the format expected by the cloud service (p. 7) | [sensor_message.c](../firmware/App/Src/sensor_message.c), JSON as parsed by `parse_message()` | Unit tests; FT-14 |

## Mobile app (Task 3)

| ID | Requirement (page) | Implementation | Verification |
|---|---|---|---|
| AP-01 | Android app (Java or Kotlin) (p. 7) | Java, [android/](../android/) | FT-19 |
| AP-02 | HTTP with Volley, networking off the UI thread (p. 7) | `JsonObjectRequest` on one `RequestQueue` in [MainActivity.java](../android/app/src/main/java/edu/msuiit/bca182/activity/MainActivity.java) | FT-19 |
| AP-03 | Display the real-time predictions (p. 7) | Live card polling `/api/latest` every second, coloured by activity, grey when the device is offline | FT-19, FT-21 |
| AP-04 | Display the history of activity records in the database (p. 7) | RecyclerView polling `/api/history` every 2 s, older pages loaded when scrolling; 60-minute summary bar | FT-20 |
| AP-05 | Smooth interaction, bug-free code (p. 7) | Only one request of each kind in flight, polling stops in the background, row-range updates without animations; Android Lint 0 errors | FT-22, FT-23 |
| AP-06 | Decompose functionality into classes and methods (p. 7) | `MainActivity` (screen and polling), `HistoryAdapter` (list), `Prediction` (data and formatting) | Inspection |
