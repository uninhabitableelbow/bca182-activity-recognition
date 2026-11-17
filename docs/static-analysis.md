# Static Analysis

## Firmware: Cppcheck

Command (from `firmware/`):

```
pio check -e rt_spark --skip-packages --src-filters="+<App/Src/>" --severity=low
```

Only my code in `App/Src/` is checked. CubeMX-generated files, the HAL, FreeRTOS, lwIP and coreMQTT are
third-party code.

| Run | High | Medium | Low |
|---|---|---|---|
| First run | 0 | 0 | 10 |
| After fixes | 0 | 0 | 9 |

Fixed:

| Finding | Fix |
|---|---|
| `rw007.c`: variable `result` is assigned a value that is never used | Declared `result` inside the retry loop |

Kept on purpose:

| Finding | Why it stays |
|---|---|
| `rw007.c`: 7 struct members never used (`reserve1`, `reserve2`, `slave_rx_buf`, `datarate`, `hidden`) | The structs are the byte layout the RW007 module sends and expects over SPI. Removing a member would shift every field after it |
| `app_log.c`: parameter `ptr` of `_write` could be `const` | `_write` must match the C library's declaration `int _write(int, char *, int)` |
| `wifi_netif.c`: parameter `p` of `low_level_output` could be `const` | It is an lwIP callback with a fixed type (`netif_linkoutput_fn`) |

The firmware also builds with `-Wall` and no warnings, and the native unit tests build with
`-Wall -Wextra -Werror`.

## Android app: Android Lint

Command (from `android/`): `gradlew lintDebug`

| Run | Errors | Warnings |
|---|---|---|
| First run | 0 | 22 |
| After fixes | 0 | 3 |

Fixed:

| Finding | Fix |
|---|---|
| `DefaultLocale`: `toUpperCase()` without a locale | `toUpperCase(Locale.ROOT)`. With a Turkish locale, "walk" would otherwise not become "WALK" |
| `HardcodedText`, `SetTextI18n` (17): text written in layouts and Java | All text moved to `res/values/strings.xml`, with placeholders for values |
| `Autofill` (2): settings fields | `importantForAutofill="no"`, because a server address is not personal data to autofill |

Kept on purpose:

| Finding | Why it stays |
|---|---|
| `InsecureBaseConfiguration`: plain HTTP allowed | The web API runs on a PC on the local network without a TLS certificate. With a cloud VM, adding HTTPS would remove the need |
| `GradleDependency`: RecyclerView 1.4.0 available | 1.4.0 needs compile SDK 35; 1.3.2 works with the SDK 34 used here |
| `DataExtractionRules`: `allowBackup` is deprecated | `allowBackup="false"` still keeps the app's settings out of cloud backups on the Android versions that read it |

## Cloud service (Python)

The cloud code is covered by 16 unit tests (`tests/test_cloud.py`), including invalid messages (wrong
types, NaN, too many samples), sequence gaps, the silence timeout and invalid API parameters.
