# NOTA — Network Over-The-Air

TCP-based firmware upload for Arduino microcontrollers. Upload new firmware over WiFi or Ethernet without physical access to the device.

**Supported platforms:** ESP8266, ESP32, STM32 (with Ethernet)

## Why NOTA?

Standard OTA (like ArduinoOTA / espota) uses UDP for discovery and requires the host machine to open a listening port for the device to push data back. This breaks in locked-down industrial networks with tight firewall rules.

NOTA uses a **single outbound TCP connection** from the host to the device — no inbound ports, no UDP broadcast, no mDNS. If you can ping the device, you can update it.

## Install

### PlatformIO (recommended)

```ini
lib_deps = NOTA
```

Or from GitHub:

```ini
lib_deps = https://github.com/Jozo132/NOTA.git
```

### Arduino IDE

Download or clone this repository into your Arduino `libraries/` folder.

## Quick Start

### Firmware Side

```cpp
#include <NOTA.h>

NOTAClass NOTA;

void setup() {
    // Connect to WiFi / Ethernet first ...

    NOTA.setHostname("my-device");
    NOTA.setPort(8266);
    NOTA.setPassword("secret");       // optional

    NOTA.onStart([]() {
        Serial.println("OTA update starting...");
    });
    NOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress * 100) / total);
    });
    NOTA.onEnd([]() {
        Serial.println("\nOTA update complete!");
    });
    NOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error: %d\n", error);
    });

    NOTA.begin();
}

void loop() {
    NOTA.handle();
}
```

### Upload Side

Three ways to upload, from simplest to most flexible:

#### 1. PlatformIO CLI target (`pio run -t nota`)

Configure your `platformio.ini` and run with one command:

```ini
[env:myboard]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = NOTA

; NOTA upload settings
custom_nota_ip   = 192.168.1.100
custom_nota_port = 8266
custom_nota_auth = secret
```

```bash
pio run -t nota
```

This builds the firmware and uploads in one step.

#### 2. PlatformIO GUI target (`pio run -t nota-gui`)

Opens a graphical interface with firmware path pre-filled. IP, port, and password are remembered between sessions.

```bash
pio run -t nota-gui
```

> Requires Python with Tkinter and Node.js installed.

#### 3. Direct Node.js CLI

```bash
node tools/nota.js -f firmware.bin -i 192.168.1.100 -p 8266 -a secret
```

## PlatformIO Configuration

All options go in your `platformio.ini` under the environment section:

| Option | Required | Default | Description |
|---|---|---|---|
| `custom_nota_ip` | Yes (for CLI) | — | Target device IP address |
| `custom_nota_port` | No | `8266` | Target device TCP port |
| `custom_nota_auth` | No | — | OTA password (if device requires auth) |
| `custom_nota_name` | No | Project folder name | Expected device name (safety check) |
| `custom_nota_board` | No | PIO board ID | Expected board ID (safety check) |
| `custom_nota_force` | No | `false` | Skip name/board/version safety checks |

### Upload Button Override

To make the standard PlatformIO **Upload** button use NOTA:

```ini
[env:myboard]
upload_protocol = custom
custom_nota_ip   = 192.168.1.100
custom_nota_auth = secret
```

Now `pio run -t upload` (or the Upload button in VS Code) uses NOTA automatically.

## Upload Protocol

NOTA uses a simple TCP protocol:

1. Host connects to device on the configured port
2. Host sends: `<command> <size> <md5>\n`
3. Device responds with `AUTH <nonce> <metadata>` or `OK <metadata>`
4. If auth required: MD5 challenge-response handshake
5. Host streams firmware in 2048-byte chunks
6. Device verifies MD5, writes to flash, reboots

The metadata includes NOTA version, device name, platform, board, and firmware version — allowing the uploader to verify it's talking to the right device before sending any data.

## Safety Checks

The uploader verifies before uploading:

- **NOTA version compatibility** — refuses incompatible protocol versions
- **Device name** — confirms the target device matches expectations
- **Board ID** — confirms the hardware type matches

All checks can be bypassed with `custom_nota_force = true` or `--force` on the CLI.

## Requirements

- **Firmware:** Arduino framework (ESP8266, ESP32, or STM32)
- **Upload (CLI):** [Node.js](https://nodejs.org/) (any recent version)
- **Upload (GUI):** Python 3 with Tkinter (usually included with Python)

## API Reference

### `NOTAClass`

| Method | Description |
|---|---|
| `setPort(uint16_t port)` | Set the OTA listening port (default: 8266) |
| `setHostname(const char* name)` | Set device hostname for identification |
| `setPlatform(const char* platform)` | Set platform name (e.g. "ESP32") |
| `setBoard(const char* board)` | Set board name (e.g. "NodeMCU") |
| `setVersion(const char* version)` | Set firmware version string |
| `setPassword(const char* password)` | Set OTA password (plain text) |
| `setPasswordHash(const char* hash)` | Set OTA password (MD5 hash) |
| `setRebootOnSuccess(bool reboot)` | Auto-reboot after update (default: true) |
| `begin()` | Start the OTA listener |
| `reconnect()` | Reconnect listener after network reset |
| `handle()` | Process OTA requests (call in `loop()`) |
| `onRequest(callback)` | Called when OTA connection begins |
| `onStart(callback)` | Called when update transfer starts |
| `onEnd(callback)` | Called when update completes |
| `onError(callback)` | Called on error (receives `ota_error_t`) |
| `onProgress(callback)` | Called during transfer (current, total) |
| `getCommand()` | Returns update type: `U_FLASH` or `U_FS` |

## Project Structure

```
NOTA/
├── src/
│   ├── NOTA.h                  # Main library header
│   ├── MD5.h / MD5.cpp         # MD5 hashing for authentication
│   └── utility/
│       ├── internal_flash.h    # STM32 flash write helpers
│       ├── stm32_flash_boot.h  # STM32 flash page copy (header)
│       └── stm32_flash_boot.c  # STM32 flash page copy (impl)
├── tools/
│   ├── nota.js                 # Node.js CLI uploader
│   ├── nota-gui.py             # Python/Tkinter GUI uploader
│   └── src/
│       └── tools.py            # GUI settings persistence
├── extra_script.py             # PlatformIO build integration
├── library.json                # PlatformIO package manifest
├── library.properties          # Arduino IDE package manifest
└── keywords.txt                # Arduino IDE syntax highlighting
```

## License

MIT

## Author

Joze Vovk ([@Jozo132](https://github.com/Jozo132))
