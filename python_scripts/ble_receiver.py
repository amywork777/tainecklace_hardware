#!/usr/bin/env python3
"""
BLE Audio Receiver for XIAO Voice Logger

This script connects to the XIAO-REC device via BLE and downloads audio files.

Requirements:
    pip install bleak

Usage:
    python ble_receiver.py
    python ble_receiver.py --scan-only
    python ble_receiver.py --device-address XX:XX:XX:XX:XX:XX
"""

import asyncio
import struct
import argparse
import time
from pathlib import Path
from typing import Optional

try:
    from bleak import BleakScanner, BleakClient
    from bleak.backends.device import BLEDevice
except ImportError:
    print("ERROR: bleak library not installed")
    print("Install with: pip install bleak")
    exit(1)

# BLE UUIDs from the device
UUID_SVC_FILE   = "a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0001"
UUID_TX_DATA    = "a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0002"  # notify, up to 244B
UUID_RX_CREDITS = "a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0003"  # write w/o resp, 1B
UUID_FILE_INFO  = "a3f9b7f0-52d1-4c7a-8f1c-7a1b9b2f0004"  # read: [u32 size][name...]

class XiaoAudioReceiver:
    def __init__(self):
        self.client: Optional[BleakClient] = None
        self.file_size = 0
        self.file_name = ""
        self.received_data = bytearray()
        self.expected_seq = 0
        self.total_packets = 0
        self.credits_sent = 0
        self.start_time = 0
        self.file_transfer_complete = False
        
        # Packet reordering buffer for out-of-order packets
        self.packet_buffer = {}  # seq -> payload
        self.max_buffer_size = 100  # Maximum packets to buffer
        self.last_progress_update = 0
        
    def crc16_ccitt(self, data: bytes) -> int:
        """Calculate CRC16-CCITT checksum (same as device)"""
        crc = 0xFFFF
        for byte in data:
            crc ^= byte << 8
            for _ in range(8):
                if crc & 0x8000:
                    crc = ((crc << 1) ^ 0x1021) & 0xFFFF
                else:
                    crc = (crc << 1) & 0xFFFF
        return crc
    
    async def scan_for_device(self, timeout=10) -> Optional[BLEDevice]:
        """Scan for XIAO-REC device"""
        print(f"Scanning for BLE devices (timeout: {timeout}s)...")
        
        devices = await BleakScanner.discover(timeout=timeout)
        
        print(f"Found {len(devices)} BLE devices:")
        xiao_device = None
        
        for device in devices:
            # Print all devices for debugging
            print(f"  - {device.name or 'Unknown'} ({device.address})")
            
            # Check various possible names
            if device.name:
                if "XIAO" in device.name.upper() or "REC" in device.name.upper():
                    print(f"  ✓ Potential XIAO device found: {device.name}")
                    xiao_device = device
        
        # Also check by service UUID
        for device in devices:
            # Get device details
            try:
                # Check if device advertises our service UUID
                if hasattr(device, 'metadata') and 'uuids' in device.metadata:
                    if UUID_SVC_FILE.lower() in [uuid.lower() for uuid in device.metadata['uuids']]:
                        print(f"  ✓ Found device with matching service UUID: {device.address}")
                        xiao_device = device
            except:
                pass
                
        if xiao_device:
            print(f"\nSelected device: {xiao_device.name or 'Unknown'} ({xiao_device.address})")
            return xiao_device
        else:
            print("\nXIAO-REC device not found. Make sure:")
            print("  1. Device is powered on")
            print("  2. You pressed 'c' to start BLE advertising")
            print("  3. Bluetooth is enabled on your computer")
            return None
    
    async def connect_to_device(self, device: BLEDevice) -> bool:
        """Connect to the BLE device"""
        try:
            print(f"Connecting to {device.address}...")
            self.client = BleakClient(device.address)
            await self.client.connect()
            
            if self.client.is_connected:
                print("✓ Connected successfully")
                return True
            else:
                print("✗ Connection failed")
                return False
                
        except Exception as e:
            print(f"Connection error: {e}")
            return False
    
    async def read_file_info(self) -> bool:
        """Read file information from device"""
        try:
            print("Reading file information...")
            file_info_data = await self.client.read_gatt_char(UUID_FILE_INFO)
            
            if len(file_info_data) < 4:
                print("Invalid file info data")
                return False
                
            # Parse: [u32 size][name (null-terminated)]
            self.file_size = struct.unpack('<I', file_info_data[:4])[0]
            name_bytes = file_info_data[4:]
            
            # Find null terminator
            null_pos = name_bytes.find(b'\x00')
            if null_pos >= 0:
                self.file_name = name_bytes[:null_pos].decode('utf-8')
            else:
                self.file_name = name_bytes.decode('utf-8').rstrip('\x00')
            
            print(f"File: {self.file_name}")
            print(f"Size: {self.file_size:,} bytes ({self.file_size/1024:.1f} KB)")
            
            return True
            
        except Exception as e:
            print(f"Error reading file info: {e}")
            return False
    
    async def send_credits(self, credits: int):
        """Send credits to device for flow control"""
        try:
            await self.client.write_gatt_char(UUID_RX_CREDITS, bytes([credits]), response=False)
            self.credits_sent += credits
        except Exception as e:
            print(f"Error sending credits: {e}")
    
    def process_buffered_packets(self):
        """Process packets in order from the reorder buffer"""
        while self.expected_seq in self.packet_buffer:
            payload = self.packet_buffer.pop(self.expected_seq)
            self.received_data.extend(payload)
            self.expected_seq += 1
            
        # Clean up old packets if buffer gets too large
        if len(self.packet_buffer) > self.max_buffer_size:
            # Remove oldest packets (lowest sequence numbers)
            old_seqs = sorted(self.packet_buffer.keys())[:len(self.packet_buffer) - self.max_buffer_size//2]
            for seq in old_seqs:
                del self.packet_buffer[seq]
                print(f"\n⚠ Dropped old packet {seq} (buffer overflow)")
                
    def should_update_progress(self) -> bool:
        """Throttle progress updates for better performance"""
        current_time = time.time()
        if current_time - self.last_progress_update > 0.1:  # Update every 100ms max
            self.last_progress_update = current_time
            return True
        return False
    
    async def notification_handler(self, sender, data: bytes):
        """Handle incoming data packets from device with proper reordering"""
        if len(data) < 8:
            print(f"Packet too short: {len(data)} bytes")
            return
            
        # Parse packet: [seq32|len16|crc16|payload<=236]
        seq = struct.unpack('<I', data[0:4])[0]
        length = struct.unpack('<H', data[4:6])[0]
        crc_received = struct.unpack('<H', data[6:8])[0]
        payload = data[8:8+length]
        
        # Check for EOF packet (length = 0, crc = 0)
        if length == 0 and crc_received == 0:
            print(f"\n✓ EOF packet received (seq {seq}) - transfer complete!")
            self.file_transfer_complete = True
            return
        
        # Validate packet length
        if len(payload) != length:
            print(f"\nPayload length mismatch: expected {length}, got {len(payload)}")
            return
            
        # Verify CRC (only for data packets, not EOF)
        crc_calculated = self.crc16_ccitt(payload)
        if crc_received != crc_calculated:
            print(f"\nCRC error on packet {seq}: expected {crc_calculated:04x}, got {crc_received:04x}")
            # Still process packet but note the error
            
        self.total_packets += 1
        
        # Handle packet ordering
        if seq < self.expected_seq:
            # Duplicate or very old packet, ignore
            return
        elif seq == self.expected_seq:
            # Perfect! This is the next expected packet
            self.received_data.extend(payload)
            self.expected_seq += 1
            
            # Process any buffered packets that are now in order
            self.process_buffered_packets()
        else:
            # Out of order packet - buffer it for later
            if seq not in self.packet_buffer:  # Avoid duplicate buffering
                self.packet_buffer[seq] = payload
                
                # If gap is too large, we might have missed packets - process what we can
                if seq - self.expected_seq > 50:
                    print(f"\n⚠ Large gap detected: expected {self.expected_seq}, got {seq}")
                    # Find the next contiguous sequence we can process
                    available_seqs = sorted([s for s in self.packet_buffer.keys() if s >= self.expected_seq])
                    if available_seqs:
                        # Skip to the next available sequence to avoid waiting forever
                        next_seq = available_seqs[0]
                        print(f"  Skipping to packet {next_seq} (gap of {next_seq - self.expected_seq})")
                        self.expected_seq = next_seq
                        self.process_buffered_packets()
        
        # Throttled progress update for better performance
        if self.should_update_progress():
            progress = (len(self.received_data) / self.file_size) * 100 if self.file_size > 0 else 0
            elapsed = time.time() - self.start_time
            speed = len(self.received_data) / elapsed if elapsed > 0 else 0
            buffered = len(self.packet_buffer)
            
            print(f"\rPacket {seq}: {len(self.received_data):,}/{self.file_size:,} bytes "
                  f"({progress:.1f}%) - {speed/1024:.1f} KB/s [{buffered} buffered]", end='', flush=True)
        
        # Optimized credit system for higher throughput
        # Send credits more aggressively for high-speed transmission
        if self.total_packets % 2 == 0:  # Every 2 packets instead of 3
            await self.send_credits(2)  # Send 2 credits at a time for faster flow
    
    async def download_file(self) -> bool:
        """Download the audio file from device"""
        try:
            # Reset state
            self.received_data = bytearray()
            self.expected_seq = 0
            self.total_packets = 0
            self.credits_sent = 0
            self.start_time = time.time()
            self.file_transfer_complete = False
            self.packet_buffer.clear()
            self.last_progress_update = 0
            
            # Subscribe to notifications
            await self.client.start_notify(UUID_TX_DATA, self.notification_handler)
            print("\n✓ Subscribed to data notifications")
            
            # Send initial credits (aggressive for high-speed transmission)
            await self.send_credits(64)  # Max credits to start fast
            print("✓ Sent initial credits (64 for high-speed mode)")
            
            # Wait for download to complete
            print("Downloading...")
            last_received = 0
            stall_count = 0
            
            while len(self.received_data) < self.file_size and not self.file_transfer_complete:
                await asyncio.sleep(0.5)
                
                # Check if EOF packet received
                if self.file_transfer_complete:
                    break
                
                # Check for stalled transfer
                if len(self.received_data) == last_received:
                    stall_count += 1
                    if stall_count > 20:  # 10 seconds of no progress (longer for high-speed)
                        # Check if we're at 99%+ complete (fallback for old protocol)
                        progress = (len(self.received_data) / self.file_size) * 100 if self.file_size > 0 else 0
                        if progress >= 99.0:
                            print(f"\n✓ Transfer nearly complete at {progress:.1f}% - accepting as done")
                            break
                        
                        print(f"\n⚠ Transfer stalled at {len(self.received_data)} bytes ({progress:.1f}%)")
                        print(f"   Buffer contains {len(self.packet_buffer)} out-of-order packets")
                        
                        # For high-speed mode, send more credits and be more aggressive
                        await self.send_credits(32)
                        stall_count = 0
                        
                        # If we have buffered packets, try to process them
                        if self.packet_buffer:
                            print("   Attempting to process buffered packets...")
                            # Find the lowest sequence number we can start from
                            min_buffered_seq = min(self.packet_buffer.keys())
                            if min_buffered_seq - self.expected_seq <= 10:  # Small gap, skip ahead
                                print(f"   Skipping gap: {self.expected_seq} -> {min_buffered_seq}")
                                self.expected_seq = min_buffered_seq
                                self.process_buffered_packets()
                else:
                    stall_count = 0
                    last_received = len(self.received_data)
                
                # Timeout check (60 seconds without any data)
                elapsed = time.time() - self.start_time
                if elapsed > 60 and len(self.received_data) == 0:
                    print("\nTimeout: No data received")
                    return False
            
            # Stop notifications
            await self.client.stop_notify(UUID_TX_DATA)
            
            # Final statistics
            elapsed = time.time() - self.start_time
            avg_speed = len(self.received_data) / elapsed if elapsed > 0 else 0
            
            print(f"\n✓ Download complete: {len(self.received_data):,} bytes in {self.total_packets} packets")
            print(f"  Average speed: {avg_speed/1024:.1f} KB/s")
            print(f"  Total time: {elapsed:.1f} seconds")
            if self.packet_buffer:
                print(f"  Warning: {len(self.packet_buffer)} packets still in reorder buffer")
                
            return True
            
        except Exception as e:
            print(f"\nDownload error: {e}")
            return False
    
    async def save_file(self, output_dir: str = ".") -> bool:
        """Save received data to file"""
        try:
            output_path = Path(output_dir) / self.file_name
            
            # Avoid overwriting existing files
            counter = 1
            original_path = output_path
            while output_path.exists():
                name_parts = original_path.stem, counter, original_path.suffix
                output_path = original_path.parent / f"{name_parts[0]}_{name_parts[1]}{name_parts[2]}"
                counter += 1
            
            with open(output_path, 'wb') as f:
                f.write(self.received_data)
            
            print(f"✓ File saved: {output_path} ({len(self.received_data):,} bytes)")
            return True
            
        except Exception as e:
            print(f"Save error: {e}")
            return False
    
    async def disconnect(self):
        """Disconnect from device"""
        if self.client and self.client.is_connected:
            await self.client.disconnect()
            print("✓ Disconnected")

async def main():
    parser = argparse.ArgumentParser(description='BLE Audio Receiver for XIAO Voice Logger')
    parser.add_argument('--scan-only', action='store_true', help='Only scan for devices')
    parser.add_argument('--device-address', help='Connect to specific device address')
    parser.add_argument('--timeout', type=int, default=10, help='Scan timeout (default: 10s)')
    parser.add_argument('--output-dir', default='.', help='Output directory (default: current)')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    receiver = XiaoAudioReceiver()
    
    try:
        # Scan for device
        if args.device_address:
            # Create device object from address
            device = None
            devices = await BleakScanner.discover(timeout=args.timeout)
            for d in devices:
                if d.address.lower() == args.device_address.lower():
                    device = d
                    break
            if not device:
                print(f"Device {args.device_address} not found")
                return
        else:
            device = await receiver.scan_for_device(args.timeout)
            if not device:
                return
        
        if args.scan_only:
            print("Scan complete")
            return
        
        # Connect and download
        if await receiver.connect_to_device(device):
            if await receiver.read_file_info():
                if receiver.file_size > 0:
                    if await receiver.download_file():
                        await receiver.save_file(args.output_dir)
                    else:
                        print("Download failed")
                else:
                    print("No file data available")
            else:
                print("Could not read file info")
        else:
            print("Connection failed")
            
    except KeyboardInterrupt:
        print("\nInterrupted by user")
    except Exception as e:
        print(f"Unexpected error: {e}")
    finally:
        await receiver.disconnect()

if __name__ == '__main__':
    asyncio.run(main())