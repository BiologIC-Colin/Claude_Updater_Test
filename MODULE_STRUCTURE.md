# Zephyr Out-of-Tree Module Structure

This document explains the proper organization of the workspace as a Zephyr out-of-tree module.

## 📁 Directory Structure

```
workspace/                           # Out-of-tree module root
├── zephyr/
│   └── module.yml                   # ⭐ Zephyr module definition
├── CMakeLists.txt                   # ⭐ Top-level build integration
├── Kconfig                          # ⭐ Top-level configuration
│
├── drivers/                         # Hardware-level drivers
│   ├── CMakeLists.txt               # Drivers build integration
│   ├── Kconfig                      # Drivers menu configuration
│   └── can_update/                  # CAN update driver
│       ├── can_update.h
│       ├── can_update.c
│       ├── CMakeLists.txt
│       └── Kconfig
│
├── libs/                            # Protocol/application libraries
│   ├── CMakeLists.txt               # Libraries build integration
│   ├── Kconfig                      # Libraries menu configuration
│   ├── update_protocol/             # Firmware update protocol
│   │   ├── update_protocol.h
│   │   ├── update_protocol.c
│   │   ├── CMakeLists.txt
│   │   └── Kconfig
│   └── j1939_address_claim/         # J1939 Address Claim
│       ├── j1939_address_claim.h
│       ├── j1939_address_claim.c
│       ├── CMakeLists.txt
│       ├── Kconfig
│       └── README.md
│
├── boards/                          # Custom board definitions
│   └── arm/
│       └── stm32f7_custom/
│           ├── stm32f7_custom.dts
│           ├── stm32f7_custom_defconfig
│           ├── Kconfig.board
│           └── ...
│
├── apps/                            # Applications
│   └── can_bootloader_app/
│       ├── src/
│       ├── prj.conf
│       ├── CMakeLists.txt
│       └── ...
│
└── scripts/                         # West extensions
    └── west-commands.yml
```

## 🔑 Key Files Explained

### `workspace/zephyr/module.yml`

This is the **most important file** for Zephyr module integration. It tells Zephyr:
- Where to find the Kconfig file
- Where to find the CMakeLists.txt
- Board, DTS, and architecture root locations

```yaml
name: workspace
build:
  kconfig: Kconfig        # Kconfig at workspace/Kconfig
  cmake: .                # CMakeLists.txt at workspace/CMakeLists.txt
  settings:
    board_root: .         # Custom boards in workspace/boards/
    dts_root: .           # Device tree roots
    arch_root: .          # Architecture roots
    soc_root: .           # SoC roots
```

### `workspace/CMakeLists.txt`

Top-level build file that adds subdirectories:

```cmake
# Add drivers subdirectory
add_subdirectory(drivers)

# Add libraries subdirectory
add_subdirectory(libs)
```

### `workspace/Kconfig`

Top-level configuration that sources subsystem Kconfigs:

```kconfig
mainmenu "Custom Workspace Configuration"

rsource "drivers/Kconfig"
rsource "libs/Kconfig"

source "Kconfig.zephyr"
```

## 🏗️ Build System Hierarchy

```
Application CMakeLists.txt (apps/can_bootloader_app/CMakeLists.txt)
    ↓
    find_package(Zephyr) → Discovers modules via west.yml
    ↓
    Zephyr discovers workspace via module.yml
    ↓
    Includes workspace/CMakeLists.txt
    ↓
    ├─→ drivers/CMakeLists.txt
    │   └─→ add_subdirectory_ifdef(CONFIG_CAN_UPDATE can_update)
    │
    └─→ libs/CMakeLists.txt
        ├─→ add_subdirectory_ifdef(CONFIG_UPDATE_PROTOCOL update_protocol)
        └─→ add_subdirectory_ifdef(CONFIG_J1939_ADDRESS_CLAIM j1939_address_claim)
```

## 📋 Configuration System Flow

```
Application prj.conf
    ↓
    Sets CONFIG_J1939_ADDRESS_CLAIM=y
    ↓
    menuconfig loads workspace/Kconfig
    ↓
    rsource "libs/Kconfig"
    ↓
    ├─→ update_protocol/Kconfig
    └─→ j1939_address_claim/Kconfig
        └─→ Shows "J1939 Address Claim Support" option
```

## 🔄 Module Discovery Process

When you run `west build`:

1. **West finds modules**
   - Reads `west.yml`
   - Sees `self: path: workspace`
   - Looks for `workspace/zephyr/module.yml`

2. **Zephyr integrates module**
   - Reads `module.yml`
   - Adds `workspace/Kconfig` to configuration system
   - Includes `workspace/CMakeLists.txt` in build

3. **Subdirectories are processed**
   - `drivers/` and `libs/` CMakeLists.txt are included
   - Individual components are conditionally compiled based on CONFIG options

4. **Headers become available**
   - `#include "j1939_address_claim.h"` works
   - `#include "can_update.h"` works

## 📦 Drivers vs Libraries

### Drivers (`workspace/drivers/`)

**Purpose**: Direct hardware interaction

**Characteristics**:
- Low-level device control
- Hardware register access
- Direct peripheral management
- Platform-specific code

**Example**: `can_update`
- Manages flash hardware
- Controls CAN peripheral
- Handles interrupts

### Libraries (`workspace/libs/`)

**Purpose**: Protocol/algorithm implementation

**Characteristics**:
- Hardware-agnostic logic
- Protocol implementation
- Uses drivers for hardware access
- Portable across platforms

**Example**: `j1939_address_claim`
- Implements J1939-81 protocol
- Uses CAN driver (doesn't touch hardware directly)
- Could work with any CAN driver

## 🎯 Best Practices

### ✅ DO

- ✅ Keep `zephyr/module.yml` at module root
- ✅ Use hierarchical CMakeLists.txt structure
- ✅ Use `add_subdirectory_ifdef()` for conditional compilation
- ✅ Put hardware drivers in `drivers/`
- ✅ Put protocol libraries in `libs/`
- ✅ Document each library with README.md
- ✅ Use proper SPDX license identifiers

### ❌ DON'T

- ❌ Don't mix protocol logic in drivers
- ❌ Don't put drivers in libs/
- ❌ Don't forget module.yml (module won't be discovered!)
- ❌ Don't use absolute paths in CMakeLists.txt
- ❌ Don't hardcode configurations (use Kconfig)

## 🔧 Adding New Components

### Adding a New Driver

1. Create directory: `workspace/drivers/my_driver/`
2. Add files:
   - `my_driver.h`
   - `my_driver.c`
   - `CMakeLists.txt`:
     ```cmake
     zephyr_library()
     zephyr_library_sources(my_driver.c)
     zephyr_library_include_directories(.)
     ```
   - `Kconfig`:
     ```kconfig
     config MY_DRIVER
         bool "My Driver Support"
         depends on GPIO  # Add dependencies
     ```
3. Update `workspace/drivers/CMakeLists.txt`:
   ```cmake
   add_subdirectory_ifdef(CONFIG_MY_DRIVER my_driver)
   ```
4. Update `workspace/drivers/Kconfig`:
   ```kconfig
   rsource "my_driver/Kconfig"
   ```

### Adding a New Library

1. Create directory: `workspace/libs/my_protocol/`
2. Add files (same structure as driver)
3. Update `workspace/libs/CMakeLists.txt`
4. Update `workspace/libs/Kconfig`

## 🚀 Usage

Enable components in your application's `prj.conf`:

```conf
# Enable CAN driver
CONFIG_CAN=y

# Enable custom CAN update driver
CONFIG_CAN_UPDATE=y

# Enable J1939 Address Claim library
CONFIG_J1939_ADDRESS_CLAIM=y
CONFIG_J1939_AC_ARBITRARY_CAPABLE=y
```

Then use in code:

```c
#include "can_update.h"
#include "j1939_address_claim.h"

// Components are automatically linked via Zephyr build system
```

## 🔍 Verification

Verify module is discovered:

```bash
cd workspace/apps/can_bootloader_app
west build -b stm32f7_custom -t menuconfig
```

You should see:
- "Custom Workspace Configuration" menu
- "Custom Drivers" → "CAN Update Support"
- "Custom Libraries" → "J1939 Address Claim Support"

## 📚 References

- [Zephyr Modules Documentation](https://docs.zephyrproject.org/latest/develop/modules.html)
- [West Manifest Documentation](https://docs.zephyrproject.org/latest/develop/west/manifest.html)
- [Zephyr Build System](https://docs.zephyrproject.org/latest/build/cmake/index.html)
- [Kconfig Documentation](https://docs.zephyrproject.org/latest/build/kconfig/index.html)

---

**Last Updated**: 2025-01-21
**Zephyr Version**: Compatible with Zephyr 3.x+
