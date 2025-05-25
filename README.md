# Arduino Serial Controller for Unraid

A comprehensive Unraid plugin that communicates with Arduino microcontrollers via serial connection. The plugin sends system status information, notifications, and handles shutdown events.

## Features

- 🔌 **Serial Communication**: Direct communication with Arduino via USB/Serial
- 🌡️ **System Monitoring**: Sends CPU temperature and system time periodically
- 🔄 **Array Status**: Notifies Arduino when Unraid array starts/stops
- 🛑 **Shutdown Handling**: Graceful shutdown notifications to Arduino
- 🎛️ **Web Interface**: Easy configuration through Unraid's web UI
- 📦 **Standalone**: No Python installation required - uses compiled executable
- 🔧 **Extensible**: Easy to add new monitoring features

## Quick Start

### 1. Install the Plugin

1. Download the `.plg` file from the [latest release](https://github.com/phred7/unraid-arduino-serial-controller/releases)
2. In Unraid, go to **Plugins** → **Install Plugin**
3. Upload the `.plg` file or paste the GitHub URL
4. Click **Install**

### 2. Configure the Plugin

1. Go to **Settings** → **Arduino Serial Controller**
2. Configure your Arduino's serial port (usually `/dev/ttyUSB0` or `/dev/ttyACM0`)
3. Set the baud rate to match your Arduino sketch (default: 9600)
4. Adjust update interval as needed (default: 30 seconds)
5. Click **Save Configuration**

### 3. Arduino Setup

Your Arduino sketch should read JSON messages from serial. Here's a basic example:

```cpp
#include <ArduinoJson.h>

void setup() {
  Serial.begin(9600);
  Serial.println("Arduino Serial Controller Ready");
}

void loop() {
  if (Serial.available()) {
    String message = Serial.readStringUntil('\n');
    
    // Parse JSON message
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, message);
    
    String messageType = doc["type"];
    String timestamp = doc["timestamp"];
    
    if (messageType == "status_update") {
      float cpuTemp = doc["data"]["cpu_temp"];
      int uptime = doc["data"]["uptime"];
      String arrayStatus = doc["data"]["array_status"];
      
      // Handle status update
      Serial.print("CPU Temp: ");
      Serial.print(cpuTemp);
      Serial.print("°C, Array: ");
      Serial.println(arrayStatus);
      
    } else if (messageType == "system_shutdown") {
      Serial.println("System shutting down!");
      // Handle shutdown (save data, turn off LEDs, etc.)
      
    } else if (messageType == "array_status_change") {
      String currentStatus = doc["data"]["current_status"];
      Serial.print("Array status changed to: ");
      Serial.println(currentStatus);
      
    } else if (messageType == "system_startup") {
      Serial.println("System started up!");
    }
  }
}
```

## Message Format

The plugin sends JSON messages with this structure:

```json
{
  "type": "message_type",
  "timestamp": "2024-12-25T10:30:00.123456",
  "data": {
    // Message-specific data
  }
}
```

### Message Types

| Type | Description | Data Fields |
|------|-------------|-------------|
| `system_startup` | Sent when plugin starts | `version` |
| `status_update` | Periodic system status | `cpu_temp`, `timestamp`, `uptime`, `array_status` |
| `system_shutdown` | System shutting down | `reason` |
| `array_status_change` | Array started/stopped | `previous_status`, `current_status` |

## Development

### Building from Source

1. **Install UV** (if not already installed):
   ```bash
   curl -LsSf https://astral.sh/uv/install.sh | sh
   ```

2. **Clone the repository**:
   ```bash
   git clone https://github.com/phred7/unraid-arduino-serial-controller.git
   cd unraid-arduino-serial-controller
   ```

3. **Build the executable**:
   ```bash
   chmod +x build.sh
   ./build.sh
   ```

4. **Test locally**:
   ```bash
   ./dist/arduino-serial-controller
   ```

### Project Structure

```
unraid-arduino-serial-controller/
├── arduino-serial-controller.plg     # Unraid plugin definition
├── arduino_serial_controller.py      # Main Python application
├── rc.arduino-serial-controller      # Service control script
├── ArduinoSerialController.page      # Web interface
├── pyproject.toml                    # Python project configuration
├── build.sh                          # Build script
├── .github/workflows/                # GitHub Actions
└── README.md                         # This file
```

### Extending Functionality

The plugin is designed to be easily extensible. To add new monitoring features:

1. **Modify `get_extended_status()` method** in `arduino_serial_controller.py`:
   ```python
   def get_extended_status(self):
       extended = {}
       extended['memory_usage'] = self.get_memory_usage()
       extended['disk_usage'] = self.get_disk_usage()
       extended['custom_metric'] = self.get_custom_metric()
       return extended
   ```

2. **Add your custom methods**:
   ```python
   def get_memory_usage(self):
       # Your implementation here
       return memory_percentage
   ```

3. **Rebuild and deploy**:
   ```bash
   ./build.sh
   ```

## Configuration Options

| Setting | Default | Description |
|---------|---------|-------------|
| `serial_port` | `/dev/ttyUSB0` | Arduino serial port |
| `baud_rate` | `9600` | Serial communication speed |
| `update_interval` | `30` | Status update frequency (seconds) |
| `timeout` | `5` | Serial communication timeout |
| `log_level` | `INFO` | Logging verbosity |
| `retry_attempts` | `3` | Connection retry attempts |
| `retry_delay` | `5` | Delay between retries (seconds) |

## Troubleshooting

### Common Issues

1. **"No such file or directory" error**:
   - Check if your Arduino is connected
   - Verify the serial port path (`ls /dev/tty*`)
   - Try different ports: `/dev/ttyACM0`, `/dev/ttyUSB1`, etc.

2. **Permission denied**:
   - Add your user to the dialout group: `usermod -a -G dialout root`
   - Restart the plugin after making changes

3. **Plugin won't start**:
   - Check logs: `/var/log/arduino-serial-controller/arduino_controller.log`
   - Verify executable permissions: `ls -la /usr/local/bin/arduino-serial-controller`

4. **Arduino not receiving messages**:
   - Verify baud rate matches between plugin and Arduino
   - Check Arduino's serial monitor for incoming data
   - Enable DEBUG logging for detailed message output

### Logs

View logs through the web interface or directly:

```bash
tail -f /var/log/arduino-serial-controller/arduino_controller.log
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

This project is open source. Feel free to modify and distribute according to your needs.

## Support

- 📋 **Issues**: [GitHub Issues](https://github.com/phred7/unraid-arduino-serial-controller/issues)
- 💬 **Discussions**: [GitHub Discussions](https://github.com/phred7/unraid-arduino-serial-controller/discussions)
- 📖 **Wiki**: [Project Wiki](https://github.com/phred7/unraid-arduino-serial-controller/wiki)

---

**Made with ❤️ for the Unraid community**