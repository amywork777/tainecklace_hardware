#!/usr/bin/env python3
"""
Setup script for iOS App Testing Suite
Installs required packages and sets up the testing environment
"""

import subprocess
import sys
import os

def install_package(package):
    """Install a Python package using pip"""
    try:
        subprocess.check_call([sys.executable, "-m", "pip", "install", package])
        print(f"✅ Installed {package}")
        return True
    except subprocess.CalledProcessError:
        print(f"❌ Failed to install {package}")
        return False

def check_package(package_name, import_name=None):
    """Check if a package is installed"""
    if import_name is None:
        import_name = package_name
    try:
        __import__(import_name)
        print(f"✅ {package_name} is already installed")
        return True
    except ImportError:
        print(f"❌ {package_name} is not installed")
        return False

def main():
    print("🔧 Setting up iOS App Testing Suite for Smart Voice Recorder")
    print("=" * 60)
    
    # Required packages
    packages = [
        ("bleak", "bleak"),                    # BLE communication
        ("numpy", "numpy"),                    # Audio processing
        ("sounddevice", "sounddevice"),        # Audio playback
        ("speechrecognition", "speech_recognition"),  # Transcription
        ("pyaudio", "pyaudio"),               # Audio input (for transcription)
        ("wave", "wave"),                     # WAV file handling
    ]
    
    # Check which packages need installation
    to_install = []
    for package, import_name in packages:
        if not check_package(package, import_name):
            to_install.append(package)
    
    if not to_install:
        print("\n🎉 All required packages are already installed!")
    else:
        print(f"\n📦 Installing {len(to_install)} missing packages...")
        failed = []
        
        for package in to_install:
            if not install_package(package):
                failed.append(package)
        
        if failed:
            print(f"\n❌ Failed to install: {', '.join(failed)}")
            print("💡 Try installing manually with:")
            for pkg in failed:
                print(f"   pip install {pkg}")
        else:
            print("\n✅ All packages installed successfully!")
    
    # Create testing scripts summary
    print("\n📋 Available Testing Scripts:")
    print("-" * 40)
    
    scripts = [
        ("test_smart_modes.py", "Test smart mode detection (connected vs offline)"),
        ("ios_app_simulator.py", "Full iOS app simulator with transcription"),
        ("live_audio_receiver_robust.py", "Basic live streaming test"),
        ("ble_receiver.py", "File transfer and device discovery"),
    ]
    
    for script, description in scripts:
        if os.path.exists(script):
            print(f"✅ {script:<25} - {description}")
        else:
            print(f"❌ {script:<25} - {description} (missing)")
    
    print("\n🚀 Quick Start Guide:")
    print("-" * 20)
    print("1. Test smart mode detection:")
    print("   python test_smart_modes.py")
    print()
    print("2. Full iOS app simulation:")
    print("   python ios_app_simulator.py")
    print()
    print("3. Basic streaming test:")
    print("   python live_audio_receiver_robust.py")
    
    print("\n🎯 What Each Test Does:")
    print("-" * 25)
    print("📱 Smart Mode Test:")
    print("   • Connects/disconnects to test mode detection")
    print("   • Verifies LED behavior (solid vs flashing)")
    print("   • Tests button behavior in both modes")
    print()
    print("🤖 iOS App Simulator:")
    print("   • Full app functionality simulation")
    print("   • Real-time audio streaming")
    print("   • Live transcription (using Google Speech API)")
    print("   • File management simulation")
    print("   • Interactive testing interface")
    print()
    print("🎵 Basic Streaming:")
    print("   • Simple audio streaming test")
    print("   • Audio playback and recording")
    print("   • Packet loss detection")
    
    print("\n✨ Setup complete! Ready for iOS app testing.")

if __name__ == "__main__":
    main()
