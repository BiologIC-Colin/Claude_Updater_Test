# J1939 Address Claim Library

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![J1939](https://img.shields.io/badge/SAE-J1939--81-green.svg)](https://www.sae.org/standards/content/j1939/81/)
[![Zephyr](https://img.shields.io/badge/Zephyr-RTOS-purple.svg)](https://zephyrproject.org/)

This library implements **SAE J1939-81 Address Claim** functionality for automatic CAN bus address assignment in Zephyr RTOS applications.

## 📋 Features

- ✅ **Dynamic Address Assignment**: Automatically claims an address on the CAN bus
- ✅ **Contention Resolution**: Handles address conflicts using NAME priority
- ✅ **Arbitrary Addressing**: Can find alternative addresses if preferred address is taken
- ✅ **J1939 Compliant**: Follows SAE J1939-81 specification
- ✅ **Thread-safe**: Mutex-protected state management
- ✅ **Callback Support**: Notifies application of address claim state changes
- ✅ **Zero Dependencies**: Self-contained, only requires Zephyr CAN driver

## Overview

J1939 Address Claim allows devices to dynamically assign themselves a source address on the CAN bus. Each device has a unique 64-bit NAME that determines priority during address contention. Lower NAME values have higher priority.

### NAME Structure (64-bit)

```
Bit 63:    Arbitrary Address Capable (0=No, 1=Yes)
Bit 62-60: Industry Group (3 bits)
Bit 59-56: Vehicle System Instance (4 bits)
Bit 55-49: Vehicle System (7 bits)
Bit 48:    Reserved (1 bit)
Bit 47-40: Function (8 bits)
Bit 39-35: Function Instance (5 bits)
Bit 34-32: ECU Instance (3 bits)
Bit 31-21: Manufacturer Code (11 bits)
Bit 20-0:  Identity Number (21 bits)
```

### Address Claim Procedure

1. **Initialization**: Device starts with NULL address (0xFE)
2. **Claim Request**: Send Address Claimed message with preferred address
3. **Wait for Contention**: Wait 250ms for other devices to respond
4. **Resolve Conflicts**: If conflict, compare NAMEs:
   - Lower NAME = higher priority, keeps address
   - Higher NAME = lower priority, must find new address
5. **Success**: If no conflicts, address is claimed

## 🚀 Quick Start

Add to your `prj.conf`:
```conf
CONFIG_J1939_ADDRESS_CLAIM=y
```

Include in your code:
```c
#include "j1939_address_claim.h"
```

## 📖 Usage Guide

### 1. Include Header

```c
#include "j1939_address_claim.h"
```

### 2. Define Callback (Optional)

```c
static void address_claim_callback(uint8_t address, enum j1939_ac_state state, void *user_data)
{
	switch (state) {
	case J1939_AC_STATE_CLAIMED:
		printk("Address 0x%02X successfully claimed\n", address);
		break;
	case J1939_AC_STATE_CANNOT_CLAIM:
		printk("Failed to claim address\n");
		break;
	case J1939_AC_STATE_CONTENTION:
		printk("Address contention, finding new address\n");
		break;
	default:
		break;
	}
}
```

### 3. Configure and Initialize

```c
const struct device *can_dev = DEVICE_DT_GET(DT_NODELABEL(can1));

/* Build NAME */
j1939_name_t name = j1939_name_build(
	12345,      /* Identity number (unique serial number) */
	0,          /* Manufacturer code (0 = unassigned) */
	0,          /* ECU instance */
	0,          /* Function instance */
	130,        /* Function (130 = Display) */
	0,          /* Vehicle system */
	0,          /* Vehicle system instance */
	0,          /* Industry group (0 = Global) */
	true        /* Arbitrary address capable */
);

/* Configure address claim */
struct j1939_ac_config config = {
	.can_dev = can_dev,
	.name = name,
	.preferred_address = 0x80,  /* Try to claim 0x80 */
	.priority = 6,              /* Default J1939 priority */
	.arbitrary_capable = true,  /* Can use any address if 0x80 taken */
	.claim_timeout_ms = 250,    /* Standard J1939 timeout */
};

/* Initialize */
int ret = j1939_address_claim_init(&config, address_claim_callback, NULL);
if (ret) {
	printk("Failed to initialize address claim: %d\n", ret);
	return ret;
}
```

### 4. Start Address Claim

```c
/* Start claiming address */
ret = j1939_address_claim_start();
if (ret) {
	printk("Failed to start address claim: %d\n", ret);
	return ret;
}
```

### 5. Get Claimed Address

```c
uint8_t address = j1939_address_claim_get_address();
if (address != J1939_NULL_ADDRESS) {
	printk("Our address: 0x%02X\n", address);
} else {
	printk("No address claimed yet\n");
}

/* Check state */
enum j1939_ac_state state = j1939_address_claim_get_state();
if (state == J1939_AC_STATE_CLAIMED) {
	/* Address is ready to use */
}
```

### 6. Integration with can_update

```c
/* Wait for address claim to complete */
while (j1939_address_claim_get_state() != J1939_AC_STATE_CLAIMED) {
	k_sleep(K_MSEC(100));
}

uint8_t our_address = j1939_address_claim_get_address();

/* Now initialize CAN update driver with claimed address */
/* Modify can_update.c to use dynamic address instead of hardcoded J1939_SRC_ADDR */
```

## ⚙️ Configuration Options

Add to your `prj.conf`:

```conf
# Enable J1939 Address Claim
CONFIG_J1939_ADDRESS_CLAIM=y

# Optional: Configure defaults
CONFIG_J1939_AC_DEFAULT_PRIORITY=6
CONFIG_J1939_AC_CLAIM_TIMEOUT_MS=250
CONFIG_J1939_AC_ARBITRARY_CAPABLE=y
CONFIG_J1939_AC_DEFAULT_MANUFACTURER_CODE=0
```

## 💡 Complete Example

```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include "j1939_address_claim.h"

#define CAN_DEV DEVICE_DT_GET(DT_NODELABEL(can1))

static void address_claimed_cb(uint8_t address, enum j1939_ac_state state, void *user_data)
{
	if (state == J1939_AC_STATE_CLAIMED) {
		printk("Successfully claimed address 0x%02X\n", address);
	} else if (state == J1939_AC_STATE_CANNOT_CLAIM) {
		printk("ERROR: Cannot claim address\n");
	}
}

int main(void)
{
	int ret;

	/* Create unique NAME for this device */
	j1939_name_t name = j1939_name_build(
		12345,  /* Unique serial number */
		0,      /* Manufacturer code */
		0,      /* ECU instance */
		0,      /* Function instance */
		130,    /* Function code */
		0,      /* Vehicle system */
		0,      /* Vehicle system instance */
		0,      /* Industry group */
		true    /* Arbitrary address capable */
	);

	/* Configure address claim */
	struct j1939_ac_config config = {
		.can_dev = CAN_DEV,
		.name = name,
		.preferred_address = 0x80,
		.priority = 6,
		.arbitrary_capable = true,
		.claim_timeout_ms = 250,
	};

	/* Initialize */
	ret = j1939_address_claim_init(&config, address_claimed_cb, NULL);
	if (ret) {
		printk("Init failed: %d\n", ret);
		return ret;
	}

	/* Start address claim procedure */
	ret = j1939_address_claim_start();
	if (ret) {
		printk("Start failed: %d\n", ret);
		return ret;
	}

	/* Wait for address to be claimed */
	while (j1939_address_claim_get_state() != J1939_AC_STATE_CLAIMED) {
		k_sleep(K_MSEC(100));
	}

	uint8_t our_address = j1939_address_claim_get_address();
	printk("Ready! Our address: 0x%02X\n", our_address);

	/* Now use the claimed address for communication */
	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
```

## 🎯 NAME Priority Examples

Lower NAME values have **higher priority**:

```c
/* HIGH PRIORITY - More critical function, lower identity number */
j1939_name_t high_priority = j1939_name_build(
	1,      /* Low identity number */
	0,      /* Manufacturer */
	0, 0,
	127,    /* Engine controller - critical function */
	0, 0, 0, true
);

/* LOW PRIORITY - Less critical function, higher identity number */
j1939_name_t low_priority = j1939_name_build(
	99999,  /* High identity number */
	0,      /* Manufacturer */
	0, 0,
	130,    /* Display - less critical */
	0, 0, 0, true
);

/* high_priority < low_priority (numerically) = high_priority wins conflicts */
```

## 📚 Common J1939 Function Codes

| Code | Description |
|------|-------------|
| `0` | Engine |
| `3` | Transmission |
| `17` | Body Controller |
| `49` | Suspension Control |
| `127` | Engine Controller |
| `130` | Display |
| `140` | GPS |
| `233` | Diagnostic Tool |

## 🔧 Troubleshooting


### ❌ Address Always NULL
**Symptoms:** `j1939_address_claim_get_address()` returns `0xFE`

**Solutions:**
- ✓ Check CAN bus is properly initialized
- ✓ Verify CAN termination resistors (120Ω each end)
- ✓ Check for proper bitrate configuration
- ✓ Monitor CAN traffic with `candump`

### ⚠️ Cannot Claim Preferred Address
**Symptoms:** Device claims different address than preferred

**Solutions:**
- ✓ Another device may have higher priority NAME
- ✓ If `arbitrary_capable = false`, device can only use preferred address
- ✓ Check other devices' NAMEs on the bus

### 🔄 Contention Loop
**Symptoms:** Constant address changes or `J1939_AC_STATE_CONTENTION`

**Solutions:**
- ✓ Verify NAME is unique across all devices
- ✓ Check that addresses 0x00-0xFD are available
- ✓ May indicate bus saturation (all addresses taken)

### 🔗 Integration with Existing Code
To integrate with the existing `can_update.c`:

1. **Initialize address claim before** `can_update_init()`
2. **Wait for** `J1939_AC_STATE_CLAIMED`
3. **Modify** `can_update.c` to use dynamic address:
   ```c
   // Instead of: #define J1939_SRC_ADDR 0x80
   // Use: uint8_t J1939_SRC_ADDR = j1939_address_claim_get_address();
   ```

## 📚 API Reference

### Functions

| Function | Description |
|----------|-------------|
| `j1939_address_claim_init()` | Initialize address claim with configuration |
| `j1939_address_claim_start()` | Begin address claim procedure |
| `j1939_address_claim_stop()` | Stop and release claimed address |
| `j1939_address_claim_get_address()` | Get current claimed address |
| `j1939_address_claim_get_state()` | Get current state machine state |
| `j1939_address_claim_get_name()` | Get configured 64-bit NAME |

### Helper Functions

| Function | Description |
|----------|-------------|
| `j1939_name_build()` | Build NAME from individual fields |
| `j1939_name_compare()` | Compare two NAMEs for priority |

### States

| State | Description |
|-------|-------------|
| `J1939_AC_STATE_INIT` | Not initialized or stopped |
| `J1939_AC_STATE_WAIT_CLAIM` | Waiting to send claim |
| `J1939_AC_STATE_CLAIMING` | Claim sent, waiting for contention |
| `J1939_AC_STATE_CLAIMED` | ✅ Address successfully claimed |
| `J1939_AC_STATE_CANNOT_CLAIM` | ❌ Unable to claim any address |
| `J1939_AC_STATE_CONTENTION` | ⚠️ Address conflict detected |

## 🔗 References

- [SAE J1939-81: Network Management](https://www.sae.org/standards/content/j1939/81/)
- [SAE J1939-21: Data Link Layer](https://www.sae.org/standards/content/j1939/21/)
- [SAE J1939 Digital Annex](https://www.sae.org/standards/content/j1939da/)
- [Zephyr CAN API](https://docs.zephyrproject.org/latest/hardware/peripherals/can.html)

## 📄 License

```
SPDX-License-Identifier: Apache-2.0
```

Copyright (c) 2025

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

---

**Made with ❤️ for Zephyr RTOS**
