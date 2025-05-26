# Complete Setup Guide - Arduino Serial Controller for Unraid

This guide will walk you through creating your first Unraid plugin from scratch using the Arduino Serial Controller as an example.

## Overview

You'll be creating a plugin that:
- Runs as a standalone executable (no Python installation needed)
- Communicates with Arduino via serial
- Sends system status periodically
- Handles shutdown and array events
- Has a web configuration interface
- Automatically starts on boot

## Prerequisites

### Development Environment
- Linux, macOS, or Windows with WSL2
- Git installed
- UV package manager (we'll install this)
- GitHub account

### Hardware
- Unraid server
- Arduino (Uno, Nano, ESP32, etc.)
- USB cable for Arduino connection

## Step 1: Set Up Your Development Environment

### 1.1 Create GitHub Repository

1. Go to [GitHub](https://github.com) and create a new repository
2. Name it: `unraid-arduino-serial-controller`
3. Make it public
4. Initialize with README

### 1.2 Clone Your Repository

```bash
git clone https://github.com/phredd7/unraid-arduino-serial-controller.git
cd unraid-arduino-serial-controller
```

### 1.3 Install UV Package Manager

```bash
# Install UV
curl -LsSf https://astral.sh/uv/install.sh | sh
source $HOME/.cargo/env

# Verify installation
uv --version
```

## Step 2: Create the Plugin Files

### 2.1 Create the Main Plugin File

Create `arduino-serial-controller.plg` with the plugin definition (use the artifact above).

### 2.2 Create the Python Application

Create `arduino_serial_controller.py` with the main application logic (use the artifact above).

### 2.3 Create the Service Script

Create `rc.arduino-serial-controller` with the service control script (use the artifact above).

### 2.4 Create the Web Interface

Create `ArduinoSerialControllerSettings.page` with the web interface (use the artifact above).

### 2.5 Create Build Configuration

Create `pyproject.toml` and `build.sh` (use the artifacts above).

### 2.6 Create GitHub Actions

Create `.github/workflows/build-and-release.yml` (use the artifact above).

## Step 3: Build and Test Locally

### 3.1 Build the Executable

```bash
# Make build script executable
chmod +x build.sh

# Run the build
./build.sh
```

This will:
- Install UV if needed
- Create a virtual environment
- Install dependencies
- Build a standalone executable using PyInstaller
- Create `dist/arduino-serial-controller`

### 3.2 Test the Executable

```bash
# Test the built executable
./dist/arduino-serial-controller --help

# Check file info
file dist/arduino-serial-controller
ls -la dist/arduino-serial-controller
```

## Step 4: Set Up Arduino

### 4.1 Install Arduino IDE and Libraries

1. Download [Arduino IDE](https://www.arduino.cc/en/software)
2. Install the `ArduinoJson` library:
   - Open Arduino IDE
   - Go to **Tools** → **Manage Libraries**
   - Search for "ArduinoJson"
   - Install the latest version

### 4.2 Upload the Example Sketch

1. Copy the `arduino_example.ino` code (from artifact above)
2. Open in Arduino IDE
3. Select your Arduino board and port
4. Upload the sketch

### 4.3 Test Arduino Communication

1. Open Serial Monitor (115200 baud)
2. You should see: "Arduino Serial Controller Ready"
3. Try typing `info` or `test` to test commands

## Step 5: Create GitHub Release

### 5.1 Commit Your Code

```bash
# Add all files
git add .

# Commit
git commit -m "Initial release of Arduino Serial Controller plugin"

# Push to GitHub
git push origin main
```

### 5.2 Create a Release Tag

```bash
# Create and push a tag
git tag v2024.12.25
git push origin v2024.12.25
```

### 5.3 GitHub Actions Will Build Automatically

The GitHub Actions workflow will:
1. Build the executable
2. Create a release
3. Upload the plugin files
4. Make them available for download

## Step 6: Install on Unraid

### 6.1 Method 1: Direct Plugin Installation

1. Go to **Plugins** → **Install Plugin** in Unraid
2. Paste the URL: `https://github.com/phredd7/unraid-arduino-serial-controller/releases/latest/download/arduino-serial-controller.plg`
3. Click **Install**

### 6.2 Method 2: Manual Installation

1. Download the `.plg` file from your GitHub release
2. Go to **Plugins** → **Install Plugin**
3. Upload the file
4. Click **Install**

## Step 7: Configure the Plugin

### 7.1 Find Your Arduino Port

SSH into your Unraid server and check for connected devices:

```bash
# List serial devices
ls /dev/tty*

# Common Arduino ports:
# /dev/ttyUSB0  - USB-to-serial adapter
# /dev/ttyACM0  - Arduino Uno/Nano native USB
```

### 7.2 Configure Settings

1. In Unraid, go to **Settings** → **Arduino Serial Controller**
2. Set the correct serial port
3. Verify baud rate matches Arduino (9600 default)
4. Click **Save Configuration**

### 7.3 Monitor Operation

1. Check the service status in the web interface
2. View logs to ensure communication is working
3. Watch Arduino Serial Monitor for incoming messages

## Step 8: Verify Everything Works

### 8.1 Test Status Updates

You should see periodic messages in Arduino Serial Monitor like:
```
[2024-12-25T10:30:00] Received: status_update
CPU: 45.2°C, Uptime: 5h, Array: started
```

### 8.2 Test Array Events

1. Stop your Unraid array
2. Arduino should receive: `array_status_change`
3. Start the array again
4. Arduino should receive another `array_status_change`

### 8.3 Test Shutdown

1. Initiate a server shutdown
2. Arduino should receive: `system_shutdown`
3. LEDs should indicate shutdown status

## Step 9: Customize and Extend

### 9.1 Add Custom Monitoring

Modify the `get_extended_status()` method to add:
- Memory usage
- Disk space
- Network statistics
- Custom sensors

### 9.2 Enhance Arduino Behavior

Add features like:
- LCD display for status
- Temperature alerts
- Remote control capabilities
- Data logging to SD card

### 9.3 Rebuild and Deploy

After making changes:
```bash
./build.sh
git add .
git commit -m "Added new features"
git tag v2024.12.26
git push origin v2024.12.26
```

## Troubleshooting

### Common Issues

**Plugin won't install:**
- Check Unraid version compatibility (minimum 6.8.0)
- Verify GitHub release files are accessible
- Check plugin syntax in `.plg` file

**Can't connect to Arduino:**
- Verify correct serial port
- Check USB cable and connections
- Ensure Arduino is not being used by another program
- Try different baud rates

**Service won't start:**
- Check logs: `/var/log/arduino-serial-controller/arduino_controller.log`
- Verify executable permissions
- Test executable manually

**No messages received:**
- Verify JSON parsing in Arduino code
- Check serial monitor for error messages
- Enable DEBUG logging in plugin

### Debug Commands

```bash
# Check service status
/etc/rc.d/rc.arduino-serial-controller status

# View logs
tail -f /var/log/arduino-serial-controller/arduino_controller.log

# Test executable manually
/usr/local/bin/arduino-serial-controller

# Check serial devices
dmesg | grep tty
```

## Next Steps

Now that you have a working plugin:

1. **Contribute**: Submit improvements back to the community
2. **Learn**: Study other Unraid plugins for inspiration
3. **Extend**: Add more Arduino projects and integrations
4. **Share**: Help others create their own plugins

## Resources

- [Unraid Plugin Development](https://wiki.unraid.net/Plugin_Development)
- [UV Documentation](https://docs.astral.sh/uv/)
- [PyInstaller Documentation](https://pyinstaller.readthedocs.io/)
- [ArduinoJson Library](https://arduinojson.org/)
- [Community Applications](https://forums.unraid.net/forum/38-community-applications/)

## Support

If you run into issues:
1. Check the logs first
2. Search existing GitHub issues
3. Create a new issue with detailed information
4. Ask on Unraid forums

Congratulations! You've successfully created your first Unraid plugin! 🎉