# J1939 Firmware Update Implementation

This project implements firmware updates over CAN bus using the J1939 transport protocol.

## Overview

The firmware update system consists of two parts:
1. **Embedded receiver** (Zephyr RTOS) - Receives and applies firmware updates
2. **Python sender** (Raspberry Pi) - Sends firmware updates over CAN bus

## J1939 Protocol Details

### Transport Protocol

The implementation uses J1939 Transport Protocol (TP) for multi-packet message transfer:

- **TP.CM** (Connection Management) - PGN 0xEC00
  - RTS (Request to Send) - Initiates transfer
  - CTS (Clear to Send) - Receiver ready for packets
  - EOM (End of Message) - Transfer complete acknowledgment
  - ABORT - Cancel transfer

- **TP.DT** (Data Transfer) - PGN 0xEB00
  - Carries actual firmware data
  - 7 bytes of data per packet
  - Sequence numbered (1-255)

### CAN ID Format

J1939 uses 29-bit extended CAN IDs with the following structure:

```
Bit 28-26: Priority (0-7, lower = higher priority)
Bit 25-8:  Parameter Group Number (PGN)
Bit 7-0:   Source Address
```

For PDU1 format (PF < 240), bits 15-8 contain the destination address.

### Message Flow

```
Sender (RPi)                    Receiver (Device)
     |                                |
     |-------- RTS ------------------>|
     |  (size, num_packets)           |
     |                                |
     |<------- CTS -------------------|
     |  (ready to receive)            |
     |                                |
     |-------- TP.DT #1 ------------->|
     |-------- TP.DT #2 ------------->|
     |-------- TP.DT #3 ------------->|
     |           ...                  |
     |-------- TP.DT #N ------------->|
     |                                |
     |<------- EOM -------------------|
     |  (transfer complete)           |
     |                                |
```

## Configuration

### Device Addresses

- **Host (Raspberry Pi)**: 0x00 (configurable via `-s` option)
- **Target Device**: 0x80 (configurable via `-d` option)

These can be changed in the source code:
- C code: `can_update.c` - `J1939_SRC_ADDR` and `J1939_DST_ADDR`
- Python: Command line arguments or defaults in script

### CAN Bus Settings

- **Default Bitrate**: 250 kbps
- **Protocol**: J1939 with extended 29-bit IDs
- **Transport**: SocketCAN on Raspberry Pi

## Raspberry Pi Setup

### 1. Hardware Setup

Connect a CAN transceiver to the Raspberry Pi:

**MCP2515 SPI CAN Module:**
```
MCP2515    Raspberry Pi
------     ------------
VCC    ->  3.3V (Pin 1)
GND    ->  GND (Pin 6)
SCK    ->  GPIO11/SCLK (Pin 23)
SI     ->  GPIO10/MOSI (Pin 19)
SO     ->  GPIO9/MISO (Pin 21)
CS     ->  GPIO8/CE0 (Pin 24)
INT    ->  GPIO25 (Pin 22)
```

### 2. Enable SPI

Edit `/boot/config.txt`:
```bash
sudo nano /boot/config.txt
```

Add or uncomment:
```
dtparam=spi=on
dtoverlay=mcp2515-can0,oscillator=8000000,interrupt=25
dtoverlay=spi-bcm2835-overlay
```

Reboot:
```bash
sudo reboot
```

### 3. Install Dependencies

```bash
# Install can-utils
sudo apt-get update
sudo apt-get install can-utils

# Install Python CAN library
pip3 install python-can
```

### 4. Configure CAN Interface

The script automatically configures the interface, or manually:

```bash
sudo ip link set can0 type can bitrate 250000
sudo ip link set can0 up
```

Verify:
```bash
ip -details link show can0
```

## Usage

### Basic Usage

```bash
sudo python3 j1939_firmware_sender.py -i can0 -f firmware.bin -d 0x80
```

### Advanced Options

```bash
# Custom bitrate (500 kbps)
sudo python3 j1939_firmware_sender.py -i can0 -f firmware.bin -d 0x80 -b 500000

# Custom addresses (source=0x10, dest=0x90)
sudo python3 j1939_firmware_sender.py -i can0 -f firmware.bin -s 0x10 -d 0x90

# Adjust packet delay (10ms between packets)
sudo python3 j1939_firmware_sender.py -i can0 -f firmware.bin -d 0x80 -D 0.01

# Just setup CAN interface
sudo python3 j1939_firmware_sender.py -i can0 --setup-only
```

### Monitoring CAN Traffic

```bash
# View all CAN messages
candump can0

# Filter for J1939 TP messages
candump can0,0:0 -x

# Decode J1939 messages
candump can0 | j1939acd
```

## Embedded Device Setup

### Prerequisites

- Zephyr RTOS
- MCUboot bootloader
- CAN controller (e.g., STM32 with CAN peripheral)

### Configuration

The embedded device listens for J1939 messages on:
- TP.CM PGN (0xEC00) for connection management
- TP.DT PGN (0xEB00) for data transfer

Device address is set in `can_update.c`:
```c
#define J1939_SRC_ADDR 0x80  /* Device address */
#define J1939_DST_ADDR 0x00  /* Host address */
```

### Build and Flash

```bash
cd workspace/apps/can_bootloader_app
west build -b your_board
west flash
```

## Protocol Specifications

### RTS Message (Request to Send)

| Byte | Description |
|------|-------------|
| 0    | Control Byte (16 = RTS) |
| 1    | Message Size (LSB) |
| 2    | Message Size (MSB) |
| 3    | Total Packets |
| 4    | Max Packets (0xFF = no limit) |
| 5    | PGN (LSB) |
| 6    | PGN (Mid) |
| 7    | PGN (MSB) |

### CTS Message (Clear to Send)

| Byte | Description |
|------|-------------|
| 0    | Control Byte (17 = CTS) |
| 1    | Number of Packets |
| 2    | Next Packet Number |
| 3    | Reserved (0xFF) |
| 4    | Reserved (0xFF) |
| 5    | PGN (LSB) |
| 6    | PGN (Mid) |
| 7    | PGN (MSB) |

### TP.DT Message (Data Transfer)

| Byte | Description |
|------|-------------|
| 0    | Sequence Number (1-255) |
| 1-7  | Data (7 bytes max) |

### EOM Message (End of Message)

| Byte | Description |
|------|-------------|
| 0    | Control Byte (19 = EOM) |
| 1    | Total Bytes (LSB) |
| 2    | Total Bytes (MSB) |
| 3    | Total Packets |
| 4    | Reserved (0xFF) |
| 5    | PGN (LSB) |
| 6    | PGN (Mid) |
| 7    | PGN (MSB) |

## Troubleshooting

### CAN Interface Not Found

```bash
# Check if interface exists
ip link show can0

# Check kernel modules
lsmod | grep can
lsmod | grep mcp251x

# Load modules manually if needed
sudo modprobe can
sudo modprobe can-raw
sudo modprobe mcp251x
```

### Permission Denied

The script requires root privileges to configure CAN interface:
```bash
sudo python3 j1939_firmware_sender.py ...
```

### No Response from Device

1. Verify CAN bus bitrate matches on both sides
2. Check CAN bus termination resistors (120Ω on each end)
3. Verify device address configuration
4. Monitor bus with `candump can0` to see if RTS is sent
5. Check device logs for error messages

### Transfer Errors

- **Sequence errors**: Increase packet delay with `-D` option
- **Timeout**: Check CAN bus health and termination
- **Flash errors**: Verify device has sufficient flash space

## Performance

Typical transfer speeds:
- **250 kbps**: ~15-20 KB/s effective throughput
- **500 kbps**: ~30-40 KB/s effective throughput
- **1 Mbps**: ~50-70 KB/s effective throughput

Factors affecting speed:
- CAN bus bitrate
- Packet delay setting
- Processing time on receiver
- Bus load from other devices

## Security Considerations

This implementation does not include:
- Firmware authentication/signing
- Encryption
- Rollback protection

For production use, consider adding:
- MCUboot image signing with cryptographic keys
- Secure boot chain
- Firmware version checking
- CRC/checksum validation

## References

- [SAE J1939 Standard](https://www.sae.org/standards/content/j1939/)
- [J1939 Transport Protocol](https://www.sae.org/standards/content/j1939/21/)
- [SocketCAN Documentation](https://www.kernel.org/doc/html/latest/networking/can.html)
- [MCUboot Documentation](https://docs.mcuboot.com/)
- [Zephyr RTOS](https://docs.zephyrproject.org/)

## License

SPDX-License-Identifier: Apache-2.0
