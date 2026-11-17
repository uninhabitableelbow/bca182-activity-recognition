# Functional Verification

All device tests ran on the physical RT-Spark board (STM32F407ZGT6) joined to a phone hotspot
(2.4 GHz, WPA2). The cloud service ran on a Windows 10 PC on the same hotspot, and the app ran on an Android
phone. Board logs were captured from the ST-Link virtual COM port at 115200 baud with
`tools/serial_log.py`. Two of them are kept in [evidence/](evidence/). "Observed" means a person watched the
result during the test.

## Model (offline)

| ID | Test | Procedure | Expected | Result | Evidence |
|---|---|---|---|---|---|
| FT-01 | Accuracy on unseen days | Leave-one-day-out over the 12 days of the dataset | Correct on days the model never saw | Pass: 99.49% | [ml/results/results.md](../ml/results/results.md) |
| FT-02 | Any board orientation | Every test window rotated by a random 3D rotation | Accuracy stays high | Pass: 99.65% (models without rotation training: 58%) | same |
| FT-03 | Other wrist | Train on one wrist only, test on the other | Accuracy stays high | Pass: 98.8% and 99.0% (without rotation training: 74.7%) | same |

## Cloud service

| ID | Test | Procedure | Expected | Result | Evidence |
|---|---|---|---|---|---|
| FT-04 | Unit tests | `python -m unittest discover -s tests` | All pass | Pass: 16 tests | test output |
| FT-05 | Pipeline with recorded data | `tools/fake_device.py` replayed dataset walking and running, switching every 15 s | Predictions follow the activity | Pass: 38 of 39 correct; the miss was the first second after a switch | classifier log |
| FT-06 | Bad API input | `/api/history?limit=abc` | HTTP 400, service keeps running | Pass | curl |
| FT-07 | History paging | `/api/history?limit=2&before_id=100` | The 2 rows just older than id 100 | Pass: ids 99 and 98 | curl |

## Device firmware

| ID | Test | Procedure | Expected | Result | Evidence |
|---|---|---|---|---|---|
| FT-08 | Build | `pio run -e rt_spark` | No warnings | Pass: flash 10.4%, RAM 81.7% | build output |
| FT-09 | Unit tests | `pio test -e native` | All pass | Pass: 13 tests | test output |
| FT-10 | Initialise sensors | Reset the board flat on the desk | ICM20608 found; about 1 g on Z and 0 rad/s at rest | Pass: WHO_AM_I 0xAF, accel [-0.02, -0.07, 1.02] g, gyro below 0.003 rad/s | [first publish log](evidence/board_serial_first_publish.log) |
| FT-11 | Initialise Wi-Fi | Same reset | RW007 starts and reports its firmware and MAC | Pass: RW007_2.1.0, MAC FC:58:4A:A6:E5:68 | same |
| FT-12 | Join Wi-Fi | `Driver_WiFi0.Activate` with the hotspot's name and password | IP address from DHCP | Pass: 10.59.240.145, 6.1 s after start-up | same |
| FT-13 | Connect to the MQTT broker | coreMQTT CONNECT to the PC | Connected | Pass: 0.1 s after the IP address | same |
| FT-14 | One message per second | Watch the log and the classifier for 4 minutes | One message per second, sequence numbers without gaps | Pass: about 280-byte messages, a prediction every second | same; classifier log |
| FT-15 | Hotspot not visible | Hotspot on 5 GHz / off | Join fails, board keeps retrying and lists the networks it can see | Pass: 8 networks listed, retry every 3 s | serial log |
| FT-16 | Broker drops the connection silently | Switched the PC's network profile Public, then Private, which drops the connection without notice | Board notices and reconnects by itself | Pass: `MQTTKeepAliveTimeout`, reconnected in about 1 s, both times. Before the fix the task hung | [reconnect log](evidence/board_serial_reconnect_test.log) |

## End to end and mobile app

| ID | Test | Procedure | Expected | Result | Evidence |
|---|---|---|---|---|---|
| FT-17 | Board at rest | Board still on the desk | WALK on dashboard and app (the model has no "still" class) | Pass | [dashboard](images/dashboard-walk.png) |
| FT-18 | Board shaken | Shake the board by hand | RUN within about 1 s, back to WALK after | Pass: RUN in the next second; 70% WALK in the first second back | [app](images/app-run.png), [dashboard](images/dashboard-run-to-walk.png), [photo](images/demo-board-run.jpg) |
| FT-19 | Real-time prediction in the app | App open while the board sends | Card shows the current activity, live, updated every second | Pass: "Live from rt-spark-01", 100% | [app](images/app-run.png) |
| FT-20 | History in the app | App open | Newest rows appear at the top every 2 s with time and confidence | Pass | [app](images/app-run.png) |
| FT-21 | Device offline | Board stopped sending | Card turns grey with the time since the last data | Pass: "No new data for 1 min (device offline?)" | [app](images/app-offline.png) |
| FT-22 | Reconnects | Board's connection dropped (FT-16) | App goes back to live without being restarted | Pass: observed | observed |
| FT-23 | Android Lint | `gradlew lintDebug` | No errors | Pass: 0 errors, 3 deliberate warnings | [static-analysis.md](static-analysis.md) |

## Not tested

- **Walking and running while wearing the board.** The board takes its power over USB from the PC, so it was
  shaken by hand instead. Shaking is a strong, regular movement like running, but this does not show how
  well the model separates real walking from real running on this sensor.
- **Long runs.** The longest continuous session was under one hour, so memory leaks or slow faults that take
  many hours to appear would not have been seen.
