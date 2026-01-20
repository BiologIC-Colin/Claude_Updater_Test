#!/usr/bin/env python3
"""
J1939 Firmware Update Sender for Raspberry Pi
Sends firmware updates over CAN bus using J1939 Transport Protocol

Requirements:
    pip3 install python-can

Usage:
    sudo python3 j1939_firmware_sender.py -i can0 -f firmware.bin -d 0x80
"""

import argparse
import time
import struct
import can
from pathlib import Path
from typing import Optional

# J1939 Protocol Definitions
J1939_TP_CM_RTS = 16    # Request to Send
J1939_TP_CM_CTS = 17    # Clear to Send
J1939_TP_CM_EOM = 19    # End of Message Acknowledgment
J1939_TP_CM_BAM = 32    # Broadcast Announce Message
J1939_TP_CM_ABORT = 255 # Connection Abort

J1939_PGN_TP_CM = 0xEC00  # Transport Protocol - Connection Management
J1939_PGN_TP_DT = 0xEB00  # Transport Protocol - Data Transfer
J1939_PGN_FIRMWARE_UPDATE = 0xEF00  # Custom PGN for firmware updates

# Default addresses
DEFAULT_SRC_ADDR = 0x00   # Host (Raspberry Pi) address
DEFAULT_DST_ADDR = 0x80   # Device address
DEFAULT_PRIORITY = 6


class J1939FirmwareSender:
    """J1939 Firmware Update Sender"""

    def __init__(self, interface: str, src_addr: int = DEFAULT_SRC_ADDR,
                 dst_addr: int = DEFAULT_DST_ADDR, priority: int = DEFAULT_PRIORITY,
                 bitrate: int = 250000):
        """
        Initialize J1939 Firmware Sender

        Args:
            interface: CAN interface name (e.g., 'can0')
            src_addr: Source address (host)
            dst_addr: Destination address (target device)
            priority: J1939 priority (0-7, lower is higher priority)
            bitrate: CAN bus bitrate (default 250kbps)
        """
        self.interface = interface
        self.src_addr = src_addr
        self.dst_addr = dst_addr
        self.priority = priority
        self.bitrate = bitrate
        self.bus: Optional[can.Bus] = None

    def build_can_id(self, pgn: int) -> int:
        """
        Build J1939 29-bit CAN ID

        Args:
            pgn: Parameter Group Number

        Returns:
            29-bit CAN ID with extended frame flag
        """
        can_id = 0x80000000  # Extended frame bit
        can_id |= (self.priority & 0x07) << 26
        can_id |= (pgn & 0x3FFFF) << 8
        can_id |= (self.src_addr & 0xFF)

        # For PDU1 format (PF < 240), include destination address
        pf = (pgn >> 8) & 0xFF
        if pf < 240:
            can_id &= ~(0xFF << 8)
            can_id |= (self.dst_addr & 0xFF) << 8

        return can_id

    def connect(self):
        """Connect to CAN bus"""
        try:
            self.bus = can.Bus(interface='socketcan',
                              channel=self.interface,
                              bitrate=self.bitrate,
                              can_filters=None,
                              receive_own_messages=False)
            print(f"✓ Connected to {self.interface} at {self.bitrate} bps")
        except Exception as e:
            raise RuntimeError(f"Failed to connect to CAN interface: {e}")

    def disconnect(self):
        """Disconnect from CAN bus"""
        if self.bus:
            self.bus.shutdown()
            self.bus = None
            print("✓ Disconnected from CAN bus")

    def send_rts(self, data_size: int, num_packets: int) -> bool:
        """
        Send J1939 RTS (Request to Send) message

        Args:
            data_size: Total message size in bytes
            num_packets: Total number of data packets

        Returns:
            True if CTS received, False otherwise
        """
        can_id = self.build_can_id(J1939_PGN_TP_CM)

        # Build RTS message
        data = bytearray([
            J1939_TP_CM_RTS,
            data_size & 0xFF,
            (data_size >> 8) & 0xFF,
            num_packets,
            0xFF,  # Max packets (255 = no limit)
            J1939_PGN_FIRMWARE_UPDATE & 0xFF,
            (J1939_PGN_FIRMWARE_UPDATE >> 8) & 0xFF,
            (J1939_PGN_FIRMWARE_UPDATE >> 16) & 0xFF
        ])

        msg = can.Message(arbitration_id=can_id,
                         is_extended_id=True,
                         data=data)

        self.bus.send(msg)
        print(f"→ Sent RTS: {data_size} bytes, {num_packets} packets")

        # Wait for CTS response
        timeout = 5.0
        start_time = time.time()

        while time.time() - start_time < timeout:
            recv_msg = self.bus.recv(timeout=0.1)
            if recv_msg and len(recv_msg.data) >= 1:
                if recv_msg.data[0] == J1939_TP_CM_CTS:
                    num_pkts = recv_msg.data[1]
                    next_pkt = recv_msg.data[2]
                    print(f"← Received CTS: {num_pkts} packets, next={next_pkt}")
                    return True
                elif recv_msg.data[0] == J1939_TP_CM_ABORT:
                    print("✗ Received ABORT from device")
                    return False

        print("✗ Timeout waiting for CTS")
        return False

    def send_data_packet(self, seq_num: int, data: bytes):
        """
        Send J1939 TP.DT (Data Transfer) packet

        Args:
            seq_num: Sequence number (1-255)
            data: Data payload (up to 7 bytes)
        """
        can_id = self.build_can_id(J1939_PGN_TP_DT)

        # Build TP.DT message: [seq_num, data...]
        payload = bytearray([seq_num]) + bytearray(data)

        # Pad to 8 bytes if needed
        while len(payload) < 8:
            payload.append(0xFF)

        msg = can.Message(arbitration_id=can_id,
                         is_extended_id=True,
                         data=payload)

        self.bus.send(msg)

    def wait_for_eom(self, timeout: float = 10.0) -> bool:
        """
        Wait for EOM (End of Message) acknowledgment

        Args:
            timeout: Timeout in seconds

        Returns:
            True if EOM received, False otherwise
        """
        start_time = time.time()

        while time.time() - start_time < timeout:
            recv_msg = self.bus.recv(timeout=0.1)
            if recv_msg and len(recv_msg.data) >= 1:
                if recv_msg.data[0] == J1939_TP_CM_EOM:
                    total_bytes = recv_msg.data[1] | (recv_msg.data[2] << 8)
                    total_pkts = recv_msg.data[3]
                    print(f"← Received EOM: {total_bytes} bytes, {total_pkts} packets")
                    return True
                elif recv_msg.data[0] == J1939_TP_CM_ABORT:
                    print("✗ Received ABORT from device")
                    return False

        print("✗ Timeout waiting for EOM")
        return False

    def send_firmware(self, firmware_path: Path, packet_delay: float = 0.005):
        """
        Send firmware file over J1939

        Args:
            firmware_path: Path to firmware binary file
            packet_delay: Delay between packets in seconds (default 5ms)

        Returns:
            True if successful, False otherwise
        """
        if not firmware_path.exists():
            print(f"✗ Firmware file not found: {firmware_path}")
            return False

        # Read firmware file
        firmware_data = firmware_path.read_bytes()
        firmware_size = len(firmware_data)

        # Calculate number of packets (7 bytes of data per packet)
        bytes_per_packet = 7
        num_packets = (firmware_size + bytes_per_packet - 1) // bytes_per_packet

        print(f"\n{'='*60}")
        print(f"Firmware Update")
        print(f"{'='*60}")
        print(f"File: {firmware_path}")
        print(f"Size: {firmware_size} bytes")
        print(f"Packets: {num_packets}")
        print(f"Source: 0x{self.src_addr:02X}")
        print(f"Destination: 0x{self.dst_addr:02X}")
        print(f"{'='*60}\n")

        # Send RTS and wait for CTS
        if not self.send_rts(firmware_size, num_packets):
            return False

        # Small delay before starting data transfer
        time.sleep(0.1)

        # Send data packets
        print("\nTransferring data...")
        seq_num = 1
        offset = 0

        start_time = time.time()
        last_progress = 0

        while offset < firmware_size:
            # Extract data chunk (up to 7 bytes)
            end = min(offset + bytes_per_packet, firmware_size)
            chunk = firmware_data[offset:end]

            # Send packet
            self.send_data_packet(seq_num, chunk)

            offset = end
            seq_num += 1

            # Progress reporting
            progress = (offset * 100) // firmware_size
            if progress >= last_progress + 10:
                elapsed = time.time() - start_time
                speed = offset / elapsed if elapsed > 0 else 0
                print(f"  {progress}% ({offset}/{firmware_size} bytes) "
                      f"- {speed/1024:.1f} KB/s")
                last_progress = progress

            # Delay between packets to avoid overwhelming receiver
            time.sleep(packet_delay)

        elapsed = time.time() - start_time
        avg_speed = firmware_size / elapsed if elapsed > 0 else 0

        print(f"\n✓ Data transfer complete")
        print(f"  Total time: {elapsed:.2f} seconds")
        print(f"  Average speed: {avg_speed/1024:.1f} KB/s")

        # Wait for EOM acknowledgment
        print("\nWaiting for device acknowledgment...")
        if self.wait_for_eom():
            print("\n" + "="*60)
            print("✓ FIRMWARE UPDATE SUCCESSFUL!")
            print("="*60)
            print("The device will reboot to apply the update.")
            return True
        else:
            print("\n✗ Firmware update failed!")
            return False


def setup_can_interface(interface: str, bitrate: int = 250000):
    """
    Setup CAN interface on Raspberry Pi

    Args:
        interface: Interface name (e.g., 'can0')
        bitrate: Bitrate in bps
    """
    import subprocess

    print(f"Setting up {interface}...")

    try:
        # Bring down interface if already up
        subprocess.run(['sudo', 'ip', 'link', 'set', interface, 'down'],
                      stderr=subprocess.DEVNULL)

        # Configure interface
        subprocess.run(['sudo', 'ip', 'link', 'set', interface, 'type', 'can',
                       'bitrate', str(bitrate)], check=True)

        # Bring up interface
        subprocess.run(['sudo', 'ip', 'link', 'set', interface, 'up'],
                      check=True)

        print(f"✓ {interface} configured at {bitrate} bps")
        return True

    except subprocess.CalledProcessError as e:
        print(f"✗ Failed to setup {interface}: {e}")
        print("\nNote: This script requires root privileges to configure CAN interface.")
        print("Try running with: sudo python3 j1939_firmware_sender.py ...")
        return False


def main():
    parser = argparse.ArgumentParser(
        description='Send firmware updates over CAN using J1939 protocol',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Send firmware to device at address 0x80 on can0
  sudo python3 j1939_firmware_sender.py -i can0 -f firmware.bin -d 0x80

  # Use custom bitrate and packet delay
  sudo python3 j1939_firmware_sender.py -i can0 -f firmware.bin -d 0x80 -b 500000 -D 0.01

  # Just setup the CAN interface without sending
  sudo python3 j1939_firmware_sender.py -i can0 --setup-only
        """)

    parser.add_argument('-i', '--interface', default='can0',
                       help='CAN interface name (default: can0)')
    parser.add_argument('-f', '--firmware', type=Path,
                       help='Firmware binary file to send')
    parser.add_argument('-d', '--dest-addr', type=lambda x: int(x, 0), default=DEFAULT_DST_ADDR,
                       help=f'Destination address (default: 0x{DEFAULT_DST_ADDR:02X})')
    parser.add_argument('-s', '--src-addr', type=lambda x: int(x, 0), default=DEFAULT_SRC_ADDR,
                       help=f'Source address (default: 0x{DEFAULT_SRC_ADDR:02X})')
    parser.add_argument('-p', '--priority', type=int, default=DEFAULT_PRIORITY,
                       help=f'J1939 priority 0-7 (default: {DEFAULT_PRIORITY})')
    parser.add_argument('-b', '--bitrate', type=int, default=250000,
                       help='CAN bitrate in bps (default: 250000)')
    parser.add_argument('-D', '--delay', type=float, default=0.005,
                       help='Delay between packets in seconds (default: 0.005)')
    parser.add_argument('--setup-only', action='store_true',
                       help='Only setup CAN interface, do not send firmware')
    parser.add_argument('--no-setup', action='store_true',
                       help='Skip CAN interface setup (assume already configured)')

    args = parser.parse_args()

    # Setup CAN interface if needed
    if not args.no_setup:
        if not setup_can_interface(args.interface, args.bitrate):
            return 1

    if args.setup_only:
        print("\n✓ CAN interface setup complete")
        return 0

    # Validate firmware file
    if not args.firmware:
        parser.error("Firmware file (-f/--firmware) is required unless --setup-only is used")

    # Create sender and send firmware
    sender = J1939FirmwareSender(
        interface=args.interface,
        src_addr=args.src_addr,
        dst_addr=args.dest_addr,
        priority=args.priority,
        bitrate=args.bitrate
    )

    try:
        sender.connect()
        success = sender.send_firmware(args.firmware, packet_delay=args.delay)
        return 0 if success else 1

    except KeyboardInterrupt:
        print("\n\n✗ Interrupted by user")
        return 1

    except Exception as e:
        print(f"\n✗ Error: {e}")
        import traceback
        traceback.print_exc()
        return 1

    finally:
        sender.disconnect()


if __name__ == '__main__':
    exit(main())
