# nano_mqtt

ESP32-C3 firmware project (Seeed XIAO ESP32C3) using the Arduino framework with MQTT, OTA updates, motor control, and battery monitoring.

## Build System

This project is built with PlatformIO.
PlatformIO uses SCons internally (not Make and not CMake).

The repository also contains a local Python virtual environment in `.venv`.
That environment is the recommended way to run PlatformIO in this workspace.

## Requirements

- Linux or macOS (Windows also works with adjusted commands)
- Python 3
- PlatformIO CLI installed in the workspace `.venv`

### Install PlatformIO CLI

Recommended for this workspace:

```bash
source .venv/bin/activate
pip install -U platformio intelhex
```

If you prefer a user-wide installation instead, use:

```bash
python3 -m pip install --user -U platformio
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

Quick check:

```bash
./.venv/bin/python -m platformio --version
```

## Build From Terminal

Go to the project directory:

```bash
cd /home/dominik/Repository/nano/nano_mqtt
```

Default build:

```bash
./.venv/bin/python -m platformio run
```

Build the OTA environment explicitly:

```bash
./.venv/bin/python -m platformio run -e ota
```

Build the USB environment explicitly:

```bash
./.venv/bin/python -m platformio run -e usb
```

## Useful Commands

### Upload firmware via USB serial

```bash
./.venv/bin/python -m platformio run -t upload -e usb
```

With explicit port:

```bash
./.venv/bin/python -m platformio run -t upload -e usb --upload-port /dev/ttyACM0
```

### Open serial monitor

```bash
./.venv/bin/python -m platformio device monitor -b 115200
```

Or with explicit port:

```bash
./.venv/bin/python -m platformio device monitor -b 115200 -p /dev/ttyACM0
```

### Clean build output

```bash
./.venv/bin/python -m platformio run -t clean
```

### Build and upload in one step

```bash
./.venv/bin/python -m platformio run -t upload -e usb
```

### List connected serial devices

```bash
./.venv/bin/python -m platformio device list
```

### List installed PlatformIO packages

```bash
./.venv/bin/python -m platformio pkg list
```

## Flash Over Network (Ethernet/Wi-Fi OTA)

If OTA is enabled in your firmware and the board is reachable by IP, you can upload over the network.

Current OTA target in [platformio.ini](platformio.ini) is the fixed IP `192.168.188.50`.

If you want to use mDNS instead, replace that value with a hostname such as `NanoESP32.local`.

Example upload command:

```bash
./.venv/bin/python -m platformio run -t upload -e ota
```

If you want to flash over USB instead, use:

```bash
./.venv/bin/python -m platformio run -t upload -e usb
```

Typical `platformio.ini` OTA settings:

```ini
[env:ota]
extends = env
board = seeed_xiao_esp32c3
upload_protocol = espota
upload_port = 192.168.188.50
; optional if OTA password is configured in firmware
upload_flags = --auth=YOUR_OTA_PASSWORD
```

`[env:usb]` uses the normal serial uploader (`esptool`) and does not need an OTA-reachable device.

Notes:

- The device and your computer must be on the same network.
- OTA upload usually requires that the device is already running OTA-capable firmware.
- If OTA upload fails, test basic network reachability first (ping/IP check).
- If you want `ota` to be the default environment, `default_envs = ota` is set in [platformio.ini](platformio.ini).
- If you have several ESP32s, use a unique IP or unique mDNS name for each one.
- The workspace contains ready-made VS Code tasks in [.vscode/tasks.json](.vscode/tasks.json) for Build, Upload OTA, and Upload USB.

## Environment

The active PlatformIO environments are defined in [platformio.ini](platformio.ini):

- `seeed_xiao_esp32c3` as the shared base environment
- `ota` for network upload (`default_envs = ota`)
- `usb` for serial upload
- `platformio.ini` is set up so both upload environments inherit the same board and library settings

## MQTT Topics

### Subscribed Topics (incoming commands)

| Topic | Payload | Effect |
| --- | --- | --- |
| `nano/esp32/engine` | `open`, `close`, `standby`, other -> `stop` | Controls motor command queue |
| `nano/esp32/sleepms` | positive integer (milliseconds) | Requests deep sleep for given duration |

### Published Topics (outgoing status/telemetry)

| Topic | Payload | Meaning |
| --- | --- | --- |
| `nano/esp32/engine/status` | `OK` / `FAIL` | Result of subscribing to `nano/esp32/engine` during reconnect |
| `nano/esp32/sleepms/status` | `OK` / `FAIL` | Result of subscribing to `nano/esp32/sleepms` during reconnect |
| `nano/esp32/status` | `online!`, `sleeping`, `offline` | Device state. `offline` is sent by MQTT Last Will if connection drops unexpectedly |
| `nano/esp32/alive_counter` | `0`..`9` (cycling) | Heartbeat counter every second |
| `nano/esp32/sleepms/wakeup_reason` | `NORMAL_START`, `ESP_SLEEP_WAKEUP_TIMER`, `ESP_SLEEP_WAKEUP_GPIO` | Wake-up reason after boot |
| `nano/esp32/reset_reason` | reset reason enum text (for example `ESP_RST_BROWNOUT`) | Reset cause from ESP-IDF at boot |
| `nano/esp32/boot_count` | incrementing integer string | Boot counter retained in RTC memory |
| `nano/esp32/engine/set` | `open`, `close`, `stop`, `standby` | Published motor state transitions |
| `nano/esp32/battery/monitor` | `activated` / `deactivated` | Battery monitor availability |
| `nano/esp32/battery/voltage` | float string (for example `3.92`) | Battery voltage in volts |
| `nano/esp32/battery/percent` | float string (for example `76.4`) | Battery state of charge in percent |
| `nano/esp32/battery/rate` | float string (for example `-0.35`) | Charge/discharge rate in percent per hour |
| `nano/esp32/battery/charging` | `charging` / `not_charging` | Derived from the MAX17048 charge rate and published when the charging state changes |

Retained message behavior:

- A retained MQTT publish stores the latest payload on the broker for that topic.
- New subscribers receive that last retained payload immediately after subscribe.
- In this firmware, `nano/esp32/status` is retained, so clients typically see `online!`, `sleeping`, or `offline` right away.

The `nano/esp32/battery/charging` topic is derived from the MAX17048 charge rate. It does not come from a dedicated charger pin, so it reflects the fuel gauge estimate rather than a physical charge-detect signal.
The `nano/esp32/status` topic is published as retained and also configured as MQTT Last Will, so the broker can switch it to `offline` if power is lost abruptly.

Note: `nano/esp32/engine` is the command topic. `nano/esp32/engine/set` is the state topic the firmware publishes after a motor transition.

## Common Issues

- `pio: command not found`:
	Use `./.venv/bin/python -m platformio ...` in this workspace, or install PlatformIO into `~/.local/bin`.
- `AttributeError: 'PlatformioCLI' object has no attribute 'resultcallback'`:
	You are using the old system PlatformIO package from `/usr/bin/pio`. Do not use that binary with Python 3.12.
- `ModuleNotFoundError: No module named 'intelhex'`:
	Install `intelhex` in the workspace environment with `pip install -U intelhex`.
- Serial upload fails because of port:
	Use `./.venv/bin/python -m platformio device list` to find the correct serial port and pass `--upload-port`.
- OTA upload fails:
	Verify the IP address or mDNS name, OTA password, firewall/network rules, and that OTA is running on the device.