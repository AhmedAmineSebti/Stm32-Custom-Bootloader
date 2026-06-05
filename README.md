# STM32 Custom Bootloader (STM32F446xx)

Custom UART bootloader for STM32F446 with a Python host tool for command/flash operations.

## Repository contents

- `/tmp/workspace/AhmedAmineSebti/Stm32-Custom-Bootloader/bootloader_STM32F446xx`  
  STM32CubeIDE project containing the bootloader firmware (`Core/Src/main.c`, `Core/Inc/main.h`).
- `/tmp/workspace/AhmedAmineSebti/Stm32-Custom-Bootloader/python/STM32_Programmer_V1.py`  
  Python serial host utility to send bootloader commands.
- `/tmp/workspace/AhmedAmineSebti/Stm32-Custom-Bootloader/Bootloader Commands.pdf`  
  Command reference.
- `/tmp/workspace/AhmedAmineSebti/Stm32-Custom-Bootloader/commands format.pdf`  
  Packet format per command.

## Bootloader behavior

From firmware (`main.c`):

- If **PC13 button is pressed at reset**, MCU stays in bootloader mode and listens on UART.
- Otherwise, it jumps to the user application at **`0x08008000`** (`FLASH_SECTOR2_BASE_ADDRESS`).

### UART configuration

- Command UART: **USART2**, 115200, 8N1 (`C_UART`)
- Debug UART: **USART3**, 115200, 8N1 (`D_UART`)

## Protocol basics

### Common command frame

All host commands follow:

`[LEN][CMD][PAYLOAD...][CRC32]`

- `LEN` = number of bytes to follow (does not include itself)
- `CMD` = bootloader command code
- `CRC32` = 4 bytes (little-endian in host script)

### Bootloader response

- ACK: `[0xA5][LEN_TO_FOLLOW]` then response payload
- NACK: `[0x7F]`

## Commands

Command codes defined in firmware (`main.h`):

| Command | Code | Purpose | Current firmware status |
|---|---:|---|---|
| `BL_GET_VER` | `0x51` | Get bootloader version (`BL_VERSION = 0x10`) | Implemented |
| `BL_GET_HELP` | `0x52` | Get supported commands list | Implemented |
| `BL_GET_CID` | `0x53` | Get MCU chip ID | Implemented |
| `BL_GET_RDP_STATUS` | `0x54` | Get flash RDP level | Implemented |
| `BL_GO_TO_ADDR` | `0x55` | Jump to address | Implemented |
| `BL_FLASH_ERASE` | `0x56` | Sector/mass erase | Implemented |
| `BL_MEM_WRITE` | `0x57` | Write bytes to memory/flash | Implemented |
| `BL_ENDIS_RW_PROTECT` | `0x58` | Enable/disable R/W protection | Declared, handler empty |
| `BL_MEM_READ` | `0x59` | Read memory | Declared, handler empty |
| `BL_READ_SECTOR_STATUS` | `0x5A` | Read sector protection status | Declared, handler empty |
| `BL_OTP_READ` | `0x5B` | Read OTP | Declared, handler empty |

> Note: The Python script also defines `0x5C` and `0x5D`, but those are not implemented in firmware.

## Packet format summary (from `commands format.pdf`)

- **`BL_GET_VER` (`0x51`)**: total 6 bytes  
  `[LEN=5][CMD][CRC32]`
- **`BL_GET_HELP` (`0x52`)**: total 6 bytes  
  `[LEN=5][CMD][CRC32]`
- **`BL_GET_CID` (`0x53`)**: total 6 bytes  
  `[LEN=5][CMD][CRC32]`
- **`BL_GET_RDP_STATUS` (`0x54`)**: total 6 bytes  
  `[LEN=5][CMD][CRC32]`
- **`BL_GO_TO_ADDR` (`0x55`)**: total 10 bytes  
  `[LEN=9][CMD][ADDR(4)][CRC32]`
- **`BL_FLASH_ERASE` (`0x56`)**: total 8 bytes  
  `[LEN=7][CMD][SECTOR][NUM_SECTORS][CRC32]`
- **`BL_MEM_WRITE` (`0x57`)**: total `11 + X` bytes  
  `[LEN=10+X][CMD][BASE_ADDR(4)][PAYLOAD_LEN=X][PAYLOAD][CRC32]`

## Using the Python host tool

### Prerequisites

- Python 3
- `pyserial`

Install:

```bash
pip install pyserial
```

### Run

```bash
cd /tmp/workspace/AhmedAmineSebti/Stm32-Custom-Bootloader/python
python3 STM32_Programmer_V1.py
```

Then:
1. Enter serial port (example: `COM3` or `/dev/ttyUSB0`).
2. Select command from menu.
3. For `BL_MEM_WRITE`, ensure `user_app.bin` is in the same directory.

## Flash/app notes

- Bootloader jumps to application vector table in sector 2 (`0x08008000`).
- Address checks in firmware allow SRAM1, SRAM2, FLASH, and BKPSRAM ranges.
- Flash erase accepts sectors `0..7` or `0xFF` for mass erase.
