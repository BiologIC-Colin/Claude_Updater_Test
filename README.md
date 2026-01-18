# Zephyr Out-of-Tree CAN Bootloader Workspace

This is a complete out-of-tree Zephyr RTOS workspace template with MCUboot secure bootloader and CAN bus firmware update capability for STM32F7 processors.

## Project Structure

```
.
├── west.yml                          # West manifest for dependencies
├── workspace/
│   ├── boards/                       # Custom board definitions
│   │   └── arm/
│   │       └── stm32f7_custom/       # STM32F7 custom board
│   ├── drivers/                      # Custom drivers
│   │   └── can_update/               # CAN firmware update driver
│   ├── libs/                         # Custom libraries
│   │   └── update_protocol/          # Firmware update protocol
│   ├── apps/                         # Applications
│   │   └── can_bootloader_app/       # CAN bootloader demo app
│   └── scripts/                      # West command extensions
└── README.md                         # This file
```

## Features

- **Out-of-tree Zephyr workspace**: Clean separation from Zephyr SDK
- **MCUboot integration**: Secure bootloader with image verification
- **CAN bus updates**: Firmware updates over CAN interface
- **STM32F7 HAL**: Full hardware abstraction layer support
- **Custom board support**: Template for STM32F767 with CAN
- **Modular architecture**: Separate repos for boards, drivers, libs, and apps

## Prerequisites

1. Install Zephyr dependencies:
   ```bash
   # Ubuntu/Debian
   sudo apt install --no-install-recommends git cmake ninja-build gperf \
     ccache dfu-util device-tree-compiler wget \
     python3-dev python3-pip python3-setuptools python3-tk python3-wheel xz-utils file \
     make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1
   ```

2. Install west:
   ```bash
   pip3 install --user -U west
   ```

3. Install Zephyr SDK (version 0.16.0 or later):
   ```bash
   cd ~
   wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/zephyr-sdk-0.16.8_linux-x86_64.tar.xz
   tar xvf zephyr-sdk-0.16.8_linux-x86_64.tar.xz
   cd zephyr-sdk-0.16.8
   ./setup.sh
   ```

## Setup

1. Initialize the workspace:
   ```bash
   cd /home/colin/CLionProjects/Updater_Test
   west init -l .
   west update
   ```

2. Install Python dependencies:
   ```bash
   pip3 install --user -r zephyr/scripts/requirements.txt
   ```

3. Export Zephyr environment:
   ```bash
   export ZEPHYR_BASE=/home/colin/CLionProjects/Updater_Test/zephyr
   source zephyr/zephyr-env.sh
   ```

## Building

### Build the application with MCUboot:

```bash
cd workspace/apps/can_bootloader_app
west build -b stm32f7_custom -p
```

This will build both the MCUboot bootloader and the application image.

### Build outputs:
- `build/zephyr/zephyr.elf` - Application ELF
- `build/zephyr/zephyr.bin` - Application binary
- `build/zephyr/zephyr.signed.bin` - Signed application for MCUboot
- `build/mcuboot/zephyr/zephyr.bin` - MCUboot bootloader binary
- `build/zephyr/zephyr.hex` - Combined bootloader + application

## Flashing

### Flash bootloader and application:
```bash
west flash
```

### Flash only MCUboot:
```bash
west flash --domain mcuboot
```

### Flash only application:
```bash
west flash --domain app
```

## Firmware Update via CAN

### CAN Update Protocol

The firmware update protocol uses standard CAN frames (11-bit IDs):

- **Filter ID**: `0x100` (configurable via `CONFIG_CAN_UPDATE_FILTER_ID`)
- **Baud Rate**: 500 kbps (configurable in device tree)

#### Message Format

All messages use the first byte as message type:

1. **START** (0x01): Initialize update session
   ```
   Byte 0: 0x01 (START)
   Byte 1-4: Image size (little-endian, 32-bit)
   ```

2. **DATA** (0x02): Transfer firmware chunk
   ```
   Byte 0: 0x02 (DATA)
   Byte 1-2: Sequence number (little-endian, 16-bit)
   Byte 3-7: Data payload (up to 5 bytes per frame)
   ```

3. **END** (0x03): Complete update session
   ```
   Byte 0: 0x03 (END)
   Byte 1-4: CRC32 (optional)
   ```

4. **ABORT** (0x04): Cancel update
   ```
   Byte 0: 0x04 (ABORT)
   ```

### Update Process

1. Device boots into MCUboot
2. If valid image, boots into application (slot 0)
3. Application initializes CAN and listens for updates
4. Host sends START message with image size
5. Host sends DATA messages sequentially
6. Host sends END message
7. Application validates and marks image for update
8. Device reboots into MCUboot
9. MCUboot swaps images (slot 1 → slot 0)
10. Device boots new firmware

### Status LED Indicators

- **Slow blink (1 Hz)**: Idle, waiting for updates
- **Fast blink (10 Hz)**: Update in progress
- **Solid on**: Update successful, awaiting reboot
- **Very fast blink (20 Hz)**: Update error

## Configuration

### Board Configuration

Edit `workspace/boards/arm/stm32f7_custom/stm32f7_custom.dts` to customize:
- CAN pins and baud rate
- Flash partitions
- GPIO assignments
- Peripherals

### MCUboot Configuration

Edit `workspace/apps/can_bootloader_app/child_image/mcuboot/prj.conf`:
- Image signature type (RSA, ECDSA)
- Swap mode (scratch, move, etc.)
- Bootloader features

### CAN Update Configuration

Edit `workspace/drivers/can_update/Kconfig`:
- `CONFIG_CAN_UPDATE_FILTER_ID`: CAN filter ID
- `CONFIG_CAN_UPDATE_CHUNK_SIZE`: Max chunk size (8-64 bytes)
- `CONFIG_CAN_UPDATE_TIMEOUT_MS`: Operation timeout

## Memory Layout (STM32F767)

```
0x08000000 ├─────────────────┐
           │   MCUboot        │ 128 KB
0x08020000 ├─────────────────┤
           │   Slot 0 (App)   │ 448 KB
0x08090000 ├─────────────────┤
           │   Slot 1 (Update)│ 448 KB
0x08100000 ├─────────────────┤
           │   Storage        │ 896 KB
0x08200000 └─────────────────┘
```

## Custom Boards

To add a new custom board:

1. Create board directory: `workspace/boards/arm/<board_name>/`
2. Add required files:
   - `Kconfig.board` - Board Kconfig
   - `Kconfig.defconfig` - Default configuration
   - `<board_name>.dts` - Device tree
   - `<board_name>_defconfig` - Default defconfig
   - `<board_name>.yaml` - Board metadata
   - `board.cmake` - Flash/debug runner config

## Custom Drivers

To add a new driver:

1. Create driver directory: `workspace/drivers/<driver_name>/`
2. Add `Kconfig`, `CMakeLists.txt`, sources
3. Update `workspace/drivers/Kconfig` and `CMakeLists.txt`

## Custom Libraries

To add a new library:

1. Create library directory: `workspace/libs/<lib_name>/`
2. Add `Kconfig`, `CMakeLists.txt`, sources
3. Update `workspace/libs/Kconfig` and `CMakeLists.txt`

## Security Considerations

1. **Image signing**: MCUboot validates images using RSA-2048 signatures
2. **Secure boot chain**: MCUboot → App verification
3. **Flash protection**: Read/write protection on bootloader partition
4. **CAN security**: Consider adding authentication to CAN protocol
5. **Rollback protection**: MCUboot prevents downgrade attacks

## Debugging

### Enable verbose logging:
```bash
west build -b stm32f7_custom -- -DCONFIG_LOG_DEFAULT_LEVEL=4
```

### View logs:
```bash
# Using serial console
screen /dev/ttyUSB0 115200

# Or minicom
minicom -D /dev/ttyUSB0 -b 115200
```

### Debug with GDB:
```bash
west debug
```

## References

- [Zephyr Documentation](https://docs.zephyrproject.org/)
- [MCUboot Documentation](https://docs.mcuboot.com/)
- [STM32F7 Reference Manual](https://www.st.com/resource/en/reference_manual/dm00124865.pdf)
- [Zephyr CAN API](https://docs.zephyrproject.org/latest/hardware/peripherals/can.html)

## License

SPDX-License-Identifier: Apache-2.0
