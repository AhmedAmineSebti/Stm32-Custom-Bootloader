# STM32 Custom Bootloader

This repository contains a custom bootloader for STM32 microcontrollers.

## Overview

The project includes:

- Bootloader source code (C)
- Build system files (Makefile)
- Supporting scripts/configuration as needed

Everything needed to build and understand the bootloader is already in the repository; this README provides a high-level entry point and quick-start instructions.

## Getting started

### Prerequisites

You will typically need:

- **ARM GNU Toolchain** (`arm-none-eabi-gcc`, `arm-none-eabi-ld`, etc.)
- **make**
- A way to flash STM32 devices (e.g., **ST-LINK**, **OpenOCD**, or **STM32CubeProgrammer**)

### Build

From the repository root:

```bash
make
```

> If the project uses a different target (e.g., `make all`) or requires setting `MCU`, `BOARD`, or `TOOLCHAIN` variables, refer to the Makefile(s) in the repo.

### Flash

Flashing depends on your setup and tool (ST-LINK/OpenOCD/STM32CubeProgrammer). Refer to the existing project files and scripts in this repository for the correct memory map and flashing procedure.

## Repository structure

- `src/` (or similar): bootloader source code
- `inc/` (or similar): headers
- `Makefile`: build configuration

> Folder names may differ—browse the repo for the exact layout.

## Notes

- Ensure your **linker script** and **vector table** placement match the bootloader memory region.
- Verify **application start address** and the bootloader’s jump-to-application logic.

## License

If a license file is present in the repository, it applies. Otherwise, licensing is currently unspecified.
