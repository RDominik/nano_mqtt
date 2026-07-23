# nano_mqtt

ESP32-C3 firmware project (Seeed XIAO ESP32C3) using the Arduino framework with MQTT, OTA updates, motor control, and battery monitoring.

## Build System

This project is built with PlatformIO.
PlatformIO uses SCons internally (not Make and not CMake).

## Requirements

- Linux or macOS (Windows also works with adjusted commands)
- Python 3
- PlatformIO CLI (`pio`)

### Install PlatformIO CLI

Recommended (via pip):

```bash
python3 -m pip install --user -U platformio
```

If `pio` is still not found:

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

Quick check:

```bash
pio --version
```

## Build From Terminal

Go to the project directory:

```bash
cd /home/dominik/Repository/nano/nano_mqtt
```

Default build:

```bash
pio run
```

Build the configured environment explicitly:

```bash
pio run -e seeed_xiao_esp32c3
```

## Useful Commands

### Upload firmware via USB serial

```bash
pio run -t upload -e seeed_xiao_esp32c3
```

With explicit port:

```bash
pio run -t upload -e seeed_xiao_esp32c3 --upload-port /dev/ttyACM0
```

### Open serial monitor

```bash
pio device monitor -b 115200
```

Or with explicit port:

```bash
pio device monitor -b 115200 -p /dev/ttyACM0
```

### Clean build output

```bash
pio run -t clean
```

### Build and upload in one step

```bash
pio run -t upload -e seeed_xiao_esp32c3
```

### List connected serial devices

```bash
pio device list
```

### List installed PlatformIO packages

```bash
pio pkg list
```

## Flash Over Network (Ethernet/Wi-Fi OTA)

If OTA is enabled in your firmware and the board is reachable by IP, you can upload over the network.

Example (device reachable at `192.168.1.50` via Ethernet or Wi-Fi):

```bash
pio run -t upload -e seeed_xiao_esp32c3 --upload-port 192.168.1.50
```

Typical `platformio.ini` OTA settings:

```ini
[env:seeed_xiao_esp32c3]
upload_protocol = espota
upload_port = 192.168.1.50
; optional if OTA password is configured in firmware
upload_flags = --auth=YOUR_OTA_PASSWORD
```

Notes:

- The device and your computer must be on the same network.
- OTA upload usually requires that the device is already running OTA-capable firmware.
- If OTA upload fails, test basic network reachability first (ping/IP check).

## Environment

The active PlatformIO environment is defined in [platformio.ini](platformio.ini):

- `seeed_xiao_esp32c3`

## MQTT Topics

### Subscribed Topics (incoming commands)

| Topic | Payload | Effect |
| --- | --- | --- |
| `nano/esp32/engine` | `forward`, `backward`, `standby`, other -> `stop` | Controls motor command queue |
| `nano/esp32/sleepms` | positive integer (milliseconds) | Requests deep sleep for given duration |

### Published Topics (outgoing status/telemetry)

| Topic | Payload | Meaning |
| --- | --- | --- |
| `nano/esp32/engine/status` | `OK` / `FAIL` | Result of subscribing to `nano/esp32/engine` during reconnect |
| `nano/esp32/sleepms/status` | `OK` / `FAIL` | Result of subscribing to `nano/esp32/sleepms` during reconnect |
| `nano/esp32/status` | `online!`, `sleeping` | Device online heartbeat and pre-sleep state |
| `nano/esp32/alive_counter` | `0`..`9` (cycling) | Heartbeat counter every 10 seconds |
| `nano/esp32/sleepms/wakeup_reason` | `NORMAL_START`, `ESP_SLEEP_WAKEUP_TIMER`, `ESP_SLEEP_WAKEUP_GPIO` | Wake-up reason after boot |
| `nano/esp32/engine/set` | `forward`, `backward`, `stop`, `standby` | Published motor state transitions |
| `nano/esp32/battery/monitor` | `activated` / `deactivated` | Battery monitor availability |
| `nano/esp32/battery/voltage` | float string (for example `3.92`) | Battery voltage in volts |
| `nano/esp32/battery/percent` | float string (for example `76.4`) | Battery state of charge in percent |
| `nano/esp32/battery/rate` | float string (for example `-0.35`) | Charge/discharge rate in percent per hour |

## Common Issues

- `pio: command not found`:
	PlatformIO CLI is not installed, or `~/.local/bin` is not in PATH.
- Serial upload fails because of port:
	Use `pio device list` to find the correct serial port and pass `--upload-port`.
- OTA upload fails:
	Verify IP address, OTA password, firewall/network rules, and that OTA is running on the device.