#!/usr/bin/env python3
"""
Simple Smart Mode Tester
Tests the core smart mode detection functionality:
- Device behavior when app is connected vs disconnected
- LED status indicators
- Button behavior in different modes
"""

import asyncio
import logging
import time
from bleak import BleakClient, BleakScanner

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(message)s')
logger = logging.getLogger(__name__)

# Device configuration
DEVICE_NAME = "XIAO-REC"
AUDIO_SERVICE_UUID = "12345678-1234-1234-1234-123456789abc"
AUDIO_CONTROL_UUID = "12345678-1234-1234-1234-123456789abe"

class SmartModeTester:
    def __init__(self):
        self.client = None
        self.device = None
        
    async def find_device(self):
        """Find the XIAO-REC device"""
        logger.info("🔍 Scanning for XIAO-REC device...")
        devices = await BleakScanner.discover(timeout=5)
        
        for device in devices:
            if device.name == DEVICE_NAME:
                logger.info(f"✅ Found device: {device.address}")
                self.device = device
                return True
                
        logger.error("❌ Device not found")
        return False
        
    async def test_connected_mode(self):
        """Test behavior when app is connected"""
        logger.info("\n" + "="*50)
        logger.info("📱 TESTING: LIVE STREAMING MODE (App Connected)")
        logger.info("="*50)
        
        try:
            self.client = BleakClient(self.device.address)
            await self.client.connect()
            logger.info("✅ Connected to device")
            
            # Subscribe to audio stream to establish connection
            try:
                await self.client.start_notify(AUDIO_CONTROL_UUID, lambda c, d: None)
                logger.info("📡 Subscribed to audio service")
            except:
                pass  # Service might not be available yet
            
            logger.info("\n🧪 TEST INSTRUCTIONS:")
            logger.info("1. 🔘 Press the D0 button on your device")
            logger.info("2. 👀 Observe the LED behavior:")
            logger.info("   ✅ EXPECTED: Solid LED (live streaming mode)")
            logger.info("   ❌ WRONG: Flashing LED (would be offline mode)")
            logger.info("3. 🔘 Press D0 again to stop")
            
            # Keep connection alive for testing
            logger.info("\n⏳ Keeping connection alive for 60 seconds...")
            logger.info("   (Press Ctrl+C to end early)")
            
            for i in range(60):
                await asyncio.sleep(1)
                if i % 10 == 0:
                    logger.info(f"📱 Still connected... ({60-i}s remaining)")
                    
        except KeyboardInterrupt:
            logger.info("\n⚡ Test interrupted by user")
        except Exception as e:
            logger.error(f"❌ Connection error: {e}")
        finally:
            if self.client and self.client.is_connected:
                await self.client.disconnect()
                logger.info("🔌 Disconnected from device")
                
    async def test_disconnected_mode(self):
        """Test behavior when no app is connected"""
        logger.info("\n" + "="*50)
        logger.info("💾 TESTING: OFFLINE RECORDING MODE (No App)")
        logger.info("="*50)
        
        logger.info("🧪 TEST INSTRUCTIONS:")
        logger.info("1. 🔘 Press the D0 button on your device")
        logger.info("2. 👀 Observe the LED behavior:")
        logger.info("   ✅ EXPECTED: Flashing red LED (offline recording)")
        logger.info("   ❌ WRONG: Solid LED (would be streaming mode)")
        logger.info("3. 🔘 Press D0 again to stop recording")
        logger.info("4. 💾 Audio should be saved to SD card")
        
        logger.info("\n⏳ Test duration: 30 seconds")
        logger.info("   (No app connection - device should detect this)")
        
        for i in range(30):
            await asyncio.sleep(1)
            if i % 10 == 0:
                logger.info(f"📵 No app connected... ({30-i}s remaining)")
                
    async def run_full_test(self):
        """Run complete smart mode test"""
        if not await self.find_device():
            return
            
        logger.info("\n🎯 SMART VOICE RECORDER - MODE DETECTION TEST")
        logger.info("This test verifies the device correctly detects:")
        logger.info("• Live streaming when app is connected")
        logger.info("• Offline recording when no app is connected")
        
        # Test 1: Connected mode (Live streaming)
        await self.test_connected_mode()
        
        # Wait between tests
        logger.info("\n⏸️ Waiting 5 seconds between tests...")
        await asyncio.sleep(5)
        
        # Test 2: Disconnected mode (Offline recording)
        await self.test_disconnected_mode()
        
        logger.info("\n✅ SMART MODE TEST COMPLETE!")
        logger.info("📋 EXPECTED RESULTS SUMMARY:")
        logger.info("   📱 With app connected: Solid LED = Live streaming")
        logger.info("   💾 Without app: Flashing LED = Offline recording")
        logger.info("   🧠 Device automatically detects which mode to use")

async def main():
    tester = SmartModeTester()
    await tester.run_full_test()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("\n👋 Test ended by user")
