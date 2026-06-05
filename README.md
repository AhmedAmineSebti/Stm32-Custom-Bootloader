# STM32F446RE Custom Bootloader

A custom UART bootloader for the **STM32F446RE** (NUCLEO-F446RE) with a **Python host application** that communicates with it over the ST-Link virtual COM port (USART2). On reset, the MCU checks the onboard blue button (PC13): held down → bootloader mode; released → jumps to the user application at Flash Sector 2.

---

## Table of Contents

- [System Architecture](#system-architecture)
- [Hardware](#hardware)
- [Memory Map](#memory-map)
- [Communication Interface](#communication-interface)
- [ACK / NACK Protocol](#ack--nack-protocol)
- [CRC-32 Algorithm](#crc-32-algorithm)
- [Packet Structure](#packet-structure)
- [Supported Commands](#supported-commands)
  - [BL\_GET\_VER (0x51)](#bl_get_ver-0x51)
  - [BL\_GET\_HELP (0x52)](#bl_get_help-0x52)
  - [BL\_GET\_CID (0x53)](#bl_get_cid-0x53)
  - [BL\_GET\_RDP\_STATUS (0x54)](#bl_get_rdp_status-0x54)
  - [BL\_GO\_TO\_ADDR (0x55)](#bl_go_to_addr-0x55)
  - [BL\_FLASH\_ERASE (0x56)](#bl_flash_erase-0x56)
  - [BL\_MEM\_WRITE (0x57)](#bl_mem_write-0x57)
  - [Stub Commands](#stub-commands)
- [STM32 Firmware – Implementation Details](#stm32-firmware--implementation-details)
- [Python Host – Implementation Details](#python-host--implementation-details)
- [Project Structure](#project-structure)
- [Getting Started](#getting-started)
- [Debug Output](#debug-output)

---

## System Architecture

```
 +--------------------------------------------------+
 |               Host PC                            |
 |                                                  |
 |   STM32_Programmer_V1.py                          |
 |   ├── Interactive menu (1-8)                     |
 |   ├── Packet builder per command                 |
 |   ├── CRC-32 engine (matches STM32 HW CRC)       |
 |   └── ACK/NACK parser                            |
 |                  |                               |
 |            pyserial 115200 8N1                   |
 |                  |                               |
 |         USB (ST-Link virtual COM)                |
 +------------------+-------------------------------+
                    |
 +------------------+-------------------------------+
 |          NUCLEO-F446RE                           |
 |                                                  |
 |  USART2  PA2(TX) / PA3(RX)  <-- command channel |
 |  USART3  PB10/PB11           --> debug messages  |
 |  PC13    blue button         --> BL activation   |
 |  PA5     LD2 LED             --> flash activity  |
 |                                                  |
 |  +---------------+  +---------------------------+|
 |  |  Bootloader   |  |    User Application       ||
 |  | Sectors 0-1   |  |  Sector 2 @ 0x08008000   ||
 |  | 0x08000000    |  |                           ||
 |  +---------------+  +---------------------------+|
 +--------------------------------------------------+
```

---

## Hardware

| Item | Details |
|------|---------|
| MCU | STM32F446RE – ARM Cortex-M4 @ 84 MHz |
| Board | NUCLEO-F446RE |
| BL activation | PC13 – blue button, active LOW |
| Command UART | USART2 – PA2 (TX) / PA3 (RX) via ST-Link USB |
| Debug UART | USART3 – PB10 (TX) / PB11 (RX) at 115200 baud |
| Flash activity LED | PA5 – LD2 (green), ON during erase/write |
| Internal Flash | 512 KB |
| SRAM1 | 112 KB |
| SRAM2 | 16 KB |
| Backup SRAM | 4 KB |
| Host | Any PC with Python 3 and pyserial |

---

## Memory Map

| Region | Start | End | Size | Usage |
|--------|-------|-----|------|-------|
| Flash Sector 0 | `0x08000000` | `0x08003FFF` | 16 KB | Bootloader |
| Flash Sector 1 | `0x08004000` | `0x08007FFF` | 16 KB | Bootloader (overflow) |
| **Flash Sector 2** | **`0x08008000`** | `0x0800BFFF` | 16 KB | **User app start** |
| Flash Sectors 3–7 | `0x0800C000` | `0x0807FFFF` | ~464 KB | User app continuation |
| SRAM1 | `0x20000000` | `0x2001BFFF` | 112 KB | Stack / heap |
| SRAM2 | `0x2001C000` | `0x2001FFFF` | 16 KB | |
| Backup SRAM | `0x40024000` | `0x40024FFF` | 4 KB | |

The user application binary must be linked to start at **`0x08008000`**. The bootloader reads the initial MSP from this address and the reset handler from `0x08008004`.

---

## Communication Interface

| Parameter | Value |
|-----------|-------|
| Peripheral | USART2 |
| Baud rate | 115200 |
| Word length | 8 bits |
| Stop bits | 1 |
| Parity | None |
| Flow control | None |

The ST-Link bridge exposes USART2 as a virtual COM port over USB — no extra hardware needed.

---

## ACK / NACK Protocol

Every command gets a response before the payload is sent.

**ACK — CRC passed:**
```
Byte 0: 0xA5          (BL_ACK)
Byte 1: len_to_follow (number of payload bytes that follow)
```

**NACK — CRC failed:**
```
Byte 0: 0x7F          (BL_NACK)
```

The Python host reads these bytes first, checks for `0xA5`, then reads exactly `len_to_follow` more bytes.

---

## CRC-32 Algorithm

Both sides use a CRC-32 compatible with the STM32 hardware CRC peripheral. The Python host computes CRC and appends it **little-endian** as 4 bytes.

---

## Packet Structure

### Host → MCU

```
+------------------+---------------+------------------+---------------+
| Length to Follow |  Command Code |  Payload (0-N B) |    CRC-32     |
|    (1 byte)      |   (1 byte)    |                  |   (4 bytes)   |
+------------------+---------------+------------------+---------------+
        ^
        = (total packet bytes) - 1
```

### MCU → Host

```
Success:  [ 0xA5 | len_to_follow | payload... ]
Failure:  [ 0x7F ]
```

---

## Supported Commands

Command codes are defined in `bootloader_STM32F446xx/Core/Inc/main.h`.

### BL_GET_VER (0x51)

Returns the bootloader version (`BL_VERSION`). Current value: **`0x10`** (v1.0).

### BL_GET_HELP (0x52)

Returns all supported command codes.

### BL_GET_CID (0x53)

Reads the device ID.

### BL_GET_RDP_STATUS (0x54)

Reads Flash Read Protection level.

### BL_GO_TO_ADDR (0x55)

Validates then jumps to the given address.

### BL_FLASH_ERASE (0x56)

Erases one or more sectors. Use sector `0xFF` for mass erase.

### BL_MEM_WRITE (0x57)

Writes a chunk of data to memory (typically Flash at/after `0x08008000`).

### Stub Commands

These commands are defined and dispatched but may not be fully implemented depending on the handler code:

| Command | Code | Description |
|---------|------|-------------|
| `BL_ENDIS_RW_PROTECT` | `0x58` | Enable/disable per-sector R/W protection |
| `BL_MEM_READ` | `0x59` | Read from memory |
| `BL_READ_SECTOR_STATUS` | `0x5A` | Read sector protection status |
| `BL_OTP_READ` | `0x5B` | Read OTP |

---

## STM32 Firmware – Implementation Details

### Bootloader Activation

In `bootloader_STM32F446xx/Core/Src/main.c`:

- If PC13 is pressed (active LOW), the firmware enters the bootloader UART loop.
- Otherwise it jumps to the user application.

### UART Channels

| Macro | Peripheral | Role |
|-------|-----------|------|
| `C_UART` | USART2 | Command channel (host ↔ bootloader) |
| `D_UART` | USART3 | Debug `printmsg()` output |

Debug output is enabled by `#define BL_DEBUG_MSG_EN` in `main.c`.

---

## Python Host – Implementation Details

The host application is in:

- `python/STM32_Programmer_V1.py`

It builds the command frames, appends CRC, sends over serial using `pyserial`, and parses ACK/NACK + payload.

### Requirements

```bash
pip install pyserial
```

---

## Project Structure

```
.
├─ bootloader_STM32F446xx/          STM32CubeIDE project
│  ├─ Core/
│  │  ├─ Inc/                       main.h (command codes/macros)
│  │  └─ Src/                       main.c (bootloader logic)
│  └─ Drivers/                      HAL + CMSIS
├─ python/                          Python host application
│  └─ STM32_Programmer_V1.py
└─ README.md
```

---

## Getting Started

1. Flash the bootloader to `0x08000000`.
2. Hold the **blue button** (PC13), press RESET to enter bootloader mode.
3. Run the Python host script and use the menu to send commands.
4. To program a user app:
   - Erase sector 2 (or mass erase)
   - Write app binary to `0x08008000` using `BL_MEM_WRITE`
   - Jump to `0x08008000` with `BL_GO_TO_ADDR`

---

## Debug Output

Debug is sent over USART3 when `BL_DEBUG_MSG_EN` is enabled.

To disable debug output, comment out `#define BL_DEBUG_MSG_EN` in `bootloader_STM32F446xx/Core/Src/main.c`.
