# STM32 Custom Bootloader (STM32F446xx)

Custom UART bootloader for **STM32F446xx** (HAL / STM32CubeMX project).

## Boot mode selection

In `bootloader_STM32F446xx/Core/Src/main.c`, the bootloader decides between **Bootloader mode** and **User application** using the blue user button on **PC13**:

- **PC13 pressed (GPIO_PIN_RESET)** → enter bootloader UART command loop (`bootloader_uart_read_data()`)
- **PC13 not pressed** → jump to user application (`bootloader_jump_to_user_app()`)

## UARTs

Defined in `bootloader_STM32F446xx/Core/Src/main.c`:

- **Command UART (Host ↔ MCU):** `USART2` (`C_UART = &huart2`)
- **Debug UART (MCU → terminal):** `USART3` (`D_UART = &huart3`)

Debug printing is enabled with `BL_DEBUG_MSG_EN`.

## Bootloader protocol (frames)

### Host → MCU command frame

The MCU first reads **1 byte** (length), then reads `length` bytes containing:

```
+--------+--------------+--------------------+
| LEN    | CMD          | CRC32              |
| 1 byte | 1 byte       | 4 bytes (LSB first)|
+--------+--------------+--------------------+
```

Notes:

- `LEN` is the number of bytes that follow **excluding** itself.
- The bootloader verifies CRC32 before executing the command.

This matches the Python host implementation in `python/STM32_Programmer_V1.py` where `data_buf[0] = <len>-1`, and CRC is appended as 4 bytes.

### MCU → Host response

The bootloader replies with either:

- **ACK**: 2 bytes
  - `0xA5` (ACK)
  - `follow_len` (number of bytes that will follow as payload)
- **NACK**: 1 byte
  - `0x7F` (NACK)

After ACK, the bootloader sends `follow_len` bytes of payload depending on the command.

## Supported commands

Command codes are defined in `bootloader_STM32F446xx/Core/Inc/main.h`:

| Command | Code |
|---|---:|
| `BL_GET_VER` | `0x51` |
| `BL_GET_HELP` | `0x52` |
| `BL_GET_CID` | `0x53` |
| `BL_GET_RDP_STATUS` | `0x54` |
| `BL_GO_TO_ADDR` | `0x55` |
| `BL_FLASH_ERASE` | `0x56` |
| `BL_MEM_WRITE` | `0x57` |
| `BL_ENDIS_RW_PROTECT` | `0x58` |
| `BL_MEM_READ` | `0x59` |
| `BL_READ_SECTOR_STATUS` | `0x5A` |
| `BL_OTP_READ` | `0x5B` |

Bootloader version macro:

- `BL_VERSION = 0x10` (v1.0)

## Flash layout

In `bootloader_STM32F446xx/Core/Src/main.c`:

- `FLASH_SECTOR2_BASE_ADDRESS = 0x08008000`

This is typically used as the start address for the user application (sector 2 on STM32F446).

## Host application (Python)

A reference host tool is provided in:

- `python/STM32_Programmer_V1.py`

It constructs the command frames, computes CRC32, sends requests over a serial port, and decodes bootloader replies.

## Build

This is a STM32CubeIDE / STM32CubeMX generated project. You can:

- Open the `bootloader_STM32F446xx` project in **STM32CubeIDE** and build.
- Or build via Makefile if you have generated one for your environment.

## Documentation

You mentioned 2 PDFs describing:

- the bootloader commands
- frame formats sent/received by the host

If these PDFs are added to the repository (or if you tell me their file paths), I can link them here and extract the exact command payload formats for each command.
