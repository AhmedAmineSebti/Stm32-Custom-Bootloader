# STM32F4 Custom Bootloader

A fully custom bootloader for the **STM32F446RE** microcontroller, supporting firmware updates over UART with two host interfaces: a **Python desktop application** and a **wireless ESP32 web interface** with SD card support.

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Repository Structure](#repository-structure)
- [STM32 Bootloader](#stm32-bootloader)
  - [Supported Commands](#supported-commands)
  - [Communication Protocol](#communication-protocol)
  - [CRC Verification](#crc-verification)
  - [Memory Map](#memory-map)
- [Python Host Application](#python-host-application)
  - [Requirements](#requirements)
  - [Usage](#usage)
- [ESP32 Wireless Host](#esp32-wireless-host)
  - [Features](#features)
  - [Hardware Requirements](#hardware-requirements)
  - [Wiring](#wiring)
  - [Configuration](#configuration)
  - [Web Interface](#web-interface)
  - [Flashing Workflow](#flashing-workflow)
- [How to Flash a New Application](#how-to-flash-a-new-application)
- [Troubleshooting](#troubleshooting)

---

## Overview

This project implements a custom UART bootloader on the STM32F446RE from scratch, without relying on the built-in ST bootloader. It allows a host machine to:

- Query bootloader version, chip ID, and RDP status
- Jump to any valid memory address
- Erase specific flash sectors or perform a full mass erase
- Write a compiled `.bin` firmware image to flash memory

Two host interfaces are provided depending on the use case:

| Interface | Connection | Use Case |
|---|---|---|
| Python script | USB-to-UART (direct COM port) | Development, debugging |
| ESP32 web UI | WiFi (browser-based) | Wireless OTA-style updates via SD card |

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                   Host Side                         │
│                                                     │
│  ┌──────────────────┐     ┌──────────────────────┐  │
│  │  Python Script   │     │  ESP32 + Web UI      │  │
│  │  (COM port)      │     │  (WiFi + SD card)    │  │
│  └────────┬─────────┘     └──────────┬───────────┘  │
│           │ UART 115200              │ UART 115200   │
└───────────┼──────────────────────────┼───────────────┘
            │                          │
            └──────────┬───────────────┘
                       │
            ┌──────────▼───────────┐
            │   STM32F446RE        │
            │   Custom Bootloader  │
            │   @ 0x08000000       │
            ├──────────────────────┤
            │   User Application   │
            │   @ 0x08008000       │
            └──────────────────────┘
```

---

## Repository Structure

```
stm32-bootloader/
│
├── stm32/                        # STM32 bootloader firmware (STM32CubeIDE)
│   ├── Core/
│   │   ├── Src/
│   │   │   ├── main.c
│   │   │   ├── bootloader.c      # Command handlers
│   │   │   └── ...
│   │   └── Inc/
│   │       └── bootloader.h
│   └── ...
│
├── python/                       # Python host application
│   └── STM32_Programmer_V1.py
│
├── esp32/                        # ESP32 wireless host (PlatformIO)
│   ├── src/
│   │   └── bootloader_host.cpp
│   └── platformio.ini
│
└── README.md
```

---

## STM32 Bootloader

The bootloader resides in flash sector 0 starting at `0x08000000`. On power-up it checks a condition (GPIO pin or flag) to decide whether to stay in bootloader mode or jump to the user application at `0x08008000`.

### Supported Commands

| Code | Command | Description |
|---|---|---|
| `0x51` | `BL_GET_VER` | Returns bootloader version byte |
| `0x52` | `BL_GET_HELP` | Returns list of all supported command codes |
| `0x53` | `BL_GET_CID` | Returns 2-byte chip ID from DBGMCU |
| `0x54` | `BL_GET_RDP_STATUS` | Returns flash read protection level |
| `0x55` | `BL_GO_TO_ADDR` | Jumps execution to a given 32-bit address |
| `0x56` | `BL_FLASH_ERASE` | Erases one or more sectors, or mass-erases all |
| `0x57` | `BL_MEM_WRITE` | Writes a payload of up to 128 bytes to flash |

### Communication Protocol

Every command follows the same frame structure:

```
Host → STM32:
┌──────────────┬─────────┬─────────────────┬──────────────┐
│ len_to_follow│ CMD code│ Payload (varies)│  CRC32 (4B)  │
│    1 byte    │  1 byte │   0–N bytes     │   4 bytes    │
└──────────────┴─────────┴─────────────────┴──────────────┘

STM32 → Host (ACK):
┌────────┬──────────────┬────────────────────────┐
│  0xA5  │ len_to_follow│  Response bytes         │
│ 1 byte │   1 byte     │  (command-specific)     │
└────────┴──────────────┴────────────────────────┘

STM32 → Host (NACK):
┌────────┐
│  0x7F  │
└────────┘
```

`len_to_follow` in the command frame is `total_packet_length - 1` (excludes itself, includes everything else).

**BL_MEM_WRITE frame in detail:**

```
[len_to_follow | 0x57 | addr_b0 | addr_b1 | addr_b2 | addr_b3 | payload_len | payload (N bytes) | CRC (4 bytes)]
     [0]           [1]     [2]       [3]       [4]       [5]         [6]         [7 .. 6+N]         [7+N .. 10+N]
```

### CRC Verification

A standard CRC-32 with polynomial `0x04C11DB7` is computed over all bytes from `len_to_follow` up to (but not including) the trailing 4 CRC bytes. The STM32 computes the same CRC and sends NACK (`0x7F`) if they do not match.

**Important:** The CRC must be calculated **after** all payload bytes are placed in the buffer — calculating it before filling the payload is a common bug that results in consistent NACK responses.

### Memory Map

```
0x08000000  ┌─────────────────────┐
            │  Bootloader         │  Sector 0 (16 KB)
0x08004000  ├─────────────────────┤
            │  (reserved)         │  Sector 1 (16 KB)
0x08008000  ├─────────────────────┤
            │  User Application   │  Sectors 2–7
            │                     │
0x08080000  └─────────────────────┘
```

---

## Python Host Application

A terminal-based host that communicates with the STM32 bootloader directly over a serial (COM/tty) port.

### Requirements

```
Python 3.x
pyserial
```

Install with:
```bash
pip install pyserial
```

### Usage

```bash
python STM32_Programmer_V1.py
```

You will be prompted to enter the serial port name (e.g. `COM3` on Windows, `/dev/ttyUSB0` on Linux), then a menu appears:

```
 +==========================================+
 |               Menu                       |
 |         STM32F4 BootLoader v1            |
 +==========================================+

   BL_GET_VER                            --> 1
   BL_GET_HLP                            --> 2
   BL_GET_CID                            --> 3
   BL_GET_RDP_STATUS                     --> 4
   BL_GO_TO_ADDR                         --> 5
   BL_FLASH_MASS_ERASE                   --> 6
   BL_FLASH_ERASE                        --> 7
   BL_MEM_WRITE                          --> 8
   MENU_EXIT                             --> 0
```

To flash a new application, place the compiled `.bin` file named `user_app.bin` in the same directory as the Python script, then run commands `7` (erase) followed by `8` (write).

> **Note:** The serial port timeout is set to 10 seconds to allow for slow flash write operations. Do not reduce this value.

---

## ESP32 Wireless Host

An ESP32 acts as a wireless bridge between a browser-based interface and the STM32 bootloader over UART. The firmware binary is stored on a **FAT32 SD card** inserted into the ESP32's SPI SD module.

### Features

- Browser-based control panel accessible from any device on the same WiFi network
- Live terminal output streamed to the browser via **Server-Sent Events (SSE)** — no page refresh needed
- SD card support for reading `user_app.bin` and streaming it in 128-byte chunks
- Busy-state locking prevents concurrent commands
- All commands still echoed to the PlatformIO serial monitor in parallel

### Hardware Requirements

- ESP32 development board (tested on ESP32-WROOM-32 / ESP32 DevKit)
- SPI SD card module
- Micro SD card (FAT32 formatted)
- Jumper wires

### Wiring

**ESP32 → STM32 (UART):**

| ESP32 Pin | STM32 Pin |
|---|---|
| GPIO 17 (TX2) | USART3 RX |
| GPIO 16 (RX2) | USART3 TX |
| GND | GND |

**ESP32 → SD Card Module (SPI):**

| SD Module | ESP32 Pin |
|---|---|
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| SCK | GPIO 18 |
| CS | GPIO 5 |
| VCC | 3.3V |
| GND | GND |

### Configuration

Open `esp32/src/bootloader_host.cpp` and set your WiFi credentials:

```cpp
#define WIFI_SSID   "your_network_name"
#define WIFI_PASS   "your_password"
```

`platformio.ini`:
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
monitor_filters = send_on_enter
```

After flashing, open the serial monitor — the ESP32 will print its assigned IP address:
```
Connected! IP address: 192.168.1.105
Open the above IP in your browser.
```

### Web Interface

Open the IP address in any browser on the same network. The interface has four panels:

| Panel | Commands |
|---|---|
| Read Info | Get Version, Get Help, Get Chip ID, Get RDP Status |
| Jump | BL_GO_TO_ADDR with hex address input |
| Flash Erase | Mass erase button + sector erase with sector number and count inputs |
| Flash Write | BL_MEM_WRITE with base address input — reads `user_app.bin` from SD card |

The terminal panel at the bottom streams all responses live as they arrive from the STM32.

### Flashing Workflow

1. Compile your STM32 user application and export the `.bin` file
2. Copy it to the SD card root as `user_app.bin`
3. Insert the SD card into the ESP32 SD module
4. Open the web interface in a browser
5. Use **Flash Erase → Sector Erase**: sector `2`, count `6` (for a 384 KB app at `0x08008000`)
6. Use **Flash Write → Write user_app.bin**: base address `08008000`
7. Watch the terminal for progress and completion confirmation
8. Reset the STM32 — it will boot into the new application

---

## How to Flash a New Application

Regardless of which host interface you use, the sequence is always:

```
1. Hold STM32 in bootloader mode (hold button / set BOOT0 high)
2. Power on / reset STM32
3. Erase target flash sectors
4. Write new firmware
5. Release bootloader mode
6. Reset STM32 → runs new application
```

**Sector reference for STM32F446RE:**

| Sector | Start Address | Size |
|---|---|---|
| 0 | 0x08000000 | 16 KB |
| 1 | 0x08004000 | 16 KB |
| 2 | 0x08008000 | 16 KB |
| 3 | 0x0800C000 | 16 KB |
| 4 | 0x08010000 | 64 KB |
| 5 | 0x08020000 | 128 KB |
| 6 | 0x08040000 | 128 KB |
| 7 | 0x08060000 | 128 KB |

---

## Troubleshooting

**CRC always fails (NACK on every command)**
CRC must be computed after all payload bytes are written to the buffer. Verify the order: fill payload → compute CRC → append CRC bytes.

**Timeout after first MEM_WRITE chunk**
The STM32 sends the ACK immediately but the write status byte comes after the flash operation completes. Use a serial timeout of at least 10 seconds. Verify that `bootloader_uart_write_data(&write_status, 1)` is called after `execute_mem_write()` in the STM32 handler for valid addresses — not just for invalid ones.

**256 bytes written then crash**
This is the missing status byte bug described above. The leftover status byte from chunk 1 corrupts the ACK read of chunk 2. Fix: ensure the STM32 always sends the status byte for valid addresses.

**SD card mount fails**
Ensure the card is formatted as FAT32 (not exFAT). Some SD cards above 32 GB default to exFAT — reformat them with a tool like SD Card Formatter.

**ESP32 web interface not reachable**
Check the serial monitor for the IP address printed on boot. Make sure your phone or laptop is on the same WiFi network as the ESP32.

**PlatformIO serial monitor sends each character immediately**
Add `monitor_filters = send_on_enter` to `platformio.ini` so input is buffered until you press Enter.
