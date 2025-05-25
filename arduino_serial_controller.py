#!/usr/bin/env python3
"""
Arduino Serial Controller for Unraid
Communicates with Arduino microcontroller via serial connection
Sends system status and notifications
"""

import serial
import time
import json
import logging
import signal
import sys
import os
import threading
from datetime import datetime
from pathlib import Path

class ArduinoSerialController:
    def __init__(self, config_file='/boot/config/plugins/arduino-serial-controller/settings.cfg'):
        """Initialize the Arduino Serial Controller"""
        self.config_file = config_file
        self.config = self.load_config()
        self.serial_connection = None
        self.running = False
        self.shutdown_initiated = False
        
        # Setup logging
        self.setup_logging()
        
        # Threading for monitoring different events
        self.monitor_threads = []
        
        # Signal handlers for graceful shutdown
        signal.signal(signal.SIGTERM, self.signal_handler)
        signal.signal(signal.SIGINT, self.signal_handler)
        
        self.logger.info("Arduino Serial Controller initialized")
    
    def setup_logging(self):
        """Setup logging configuration"""
        log_dir = Path('/var/log/arduino-serial-controller')
        log_dir.mkdir(exist_ok=True)
        
        logging.basicConfig(
            level=getattr(logging, self.config.get('log_level', 'INFO')),
            format='%(asctime)s - %(levelname)s - %(message)s',
            handlers=[
                logging.FileHandler(log_dir / 'arduino_controller.log'),
                logging.StreamHandler(sys.stdout)
            ]
        )
        self.logger = logging.getLogger(__name__)
    
    def load_config(self):
        """Load configuration from file"""
        default_config = {
            'serial_port': '/dev/ttyUSB0',
            'baud_rate': 9600,
            'update_interval': 30,  # seconds
            'timeout': 5,
            'log_level': 'INFO',
            'retry_attempts': 3,
            'retry_delay': 5
        }
        
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, 'r') as f:
                    user_config = {}
                    for line in f:
                        if '=' in line and not line.strip().startswith('#'):
                            key, value = line.strip().split('=', 1)
                            # Convert numeric values
                            try:
                                if '.' in value:
                                    value = float(value)
                                else:
                                    value = int(value)
                            except ValueError:
                                pass  # Keep as string
                            user_config[key] = value
                    default_config.update(user_config)
            except Exception as e:
                print(f"Error loading config: {e}")
        
        return default_config
    
    def connect_arduino(self):
        """Establish serial connection with Arduino"""
        for attempt in range(self.config['retry_attempts']):
            try:
                self.serial_connection = serial.Serial(
                    port=self.config['serial_port'],
                    baudrate=self.config['baud_rate'],
                    timeout=self.config['timeout']
                )
                self.logger.info(f"Connected to Arduino on {self.config['serial_port']}")
                time.sleep(2)  # Give Arduino time to initialize
                return True
            except Exception as e:
                self.logger.error(f"Connection attempt {attempt + 1} failed: {e}")
                if attempt < self.config['retry_attempts'] - 1:
                    time.sleep(self.config['retry_delay'])
        
        return False
    
    def send_message(self, message_type, data=None):
        """Send message to Arduino"""
        if not self.serial_connection or not self.serial_connection.is_open:
            return False
        
        try:
            message = {
                'type': message_type,
                'timestamp': datetime.now().isoformat(),
                'data': data or {}
            }
            
            json_message = json.dumps(message) + '\n'
            self.serial_connection.write(json_message.encode('utf-8'))
            self.serial_connection.flush()
            
            self.logger.debug(f"Sent message: {message_type}")
            return True
            
        except Exception as e:
            self.logger.error(f"Error sending message: {e}")
            return False
    
    def get_cpu_temperature(self):
        """Get CPU temperature from system"""
        try:
            # Try different methods to get CPU temperature
            temp_sources = [
                '/sys/class/thermal/thermal_zone0/temp',
                '/sys/class/thermal/thermal_zone1/temp',
                '/sys/class/hwmon/hwmon0/temp1_input',
                '/sys/class/hwmon/hwmon1/temp1_input'
            ]
            
            for source in temp_sources:
                if os.path.exists(source):
                    with open(source, 'r') as f:
                        temp = int(f.read().strip())
                        # Convert millidegrees to degrees if necessary
                        if temp > 1000:
                            temp = temp / 1000
                        return round(temp, 1)
            
            return None
        except Exception as e:
            self.logger.error(f"Error reading CPU temperature: {e}")
            return None
    
    def get_system_status(self):
        """Collect comprehensive system status"""
        status = {
            'cpu_temp': self.get_cpu_temperature(),
            'timestamp': datetime.now().isoformat(),
            'uptime': self.get_uptime(),
            'array_status': self.get_array_status()
        }
        
        # Extensible: Add more status items here
        status.update(self.get_extended_status())
        
        return status
    
    def get_uptime(self):
        """Get system uptime"""
        try:
            with open('/proc/uptime', 'r') as f:
                uptime_seconds = float(f.read().split()[0])
                return int(uptime_seconds)
        except:
            return 0
    
    def get_array_status(self):
        """Get Unraid array status"""
        try:
            # Check if array is started by looking for typical array indicators
            if os.path.exists('/proc/mdstat'):
                with open('/proc/mdstat', 'r') as f:
                    mdstat = f.read()
                    if 'active' in mdstat:
                        return 'started'
            return 'stopped'
        except:
            return 'unknown'
    
    def get_extended_status(self):
        """Extensible method for additional status collection"""
        # Override this method or extend it for additional functionality
        extended = {}
        
        # Example extensions (uncomment/modify as needed):
        # extended['memory_usage'] = self.get_memory_usage()
        # extended['disk_usage'] = self.get_disk_usage()
        # extended['network_status'] = self.get_network_status()
        
        return extended
    
    def monitor_array_status(self):
        """Monitor array status changes"""
        last_status = None
        
        while self.running:
            try:
                current_status = self.get_array_status()
                
                if last_status is not None and current_status != last_status:
                    self.logger.info(f"Array status changed: {last_status} -> {current_status}")
                    self.send_message('array_status_change', {
                        'previous_status': last_status,
                        'current_status': current_status
                    })
                
                last_status = current_status
                time.sleep(10)  # Check every 10 seconds
                
            except Exception as e:
                self.logger.error(f"Error monitoring array status: {e}")
                time.sleep(10)
    
    def periodic_status_update(self):
        """Send periodic status updates to Arduino"""
        while self.running:
            try:
                status = self.get_system_status()
                self.send_message('status_update', status)
                
                time.sleep(self.config['update_interval'])
                
            except Exception as e:
                self.logger.error(f"Error in periodic update: {e}")
                time.sleep(self.config['update_interval'])
    
    def signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        self.logger.info(f"Received signal {signum}, initiating shutdown...")
        self.shutdown()
    
    def shutdown(self):
        """Graceful shutdown"""
        if self.shutdown_initiated:
            return
            
        self.shutdown_initiated = True
        self.logger.info("Shutting down Arduino Serial Controller...")
        
        # Send shutdown notification to Arduino
        self.send_message('system_shutdown', {'reason': 'service_stop'})
        
        # Stop the main loop
        self.running = False
        
        # Wait for threads to finish
        for thread in self.monitor_threads:
            if thread.is_alive():
                thread.join(timeout=5)
        
        # Close serial connection
        if self.serial_connection and self.serial_connection.is_open:
            time.sleep(1)  # Give Arduino time to process shutdown message
            self.serial_connection.close()
            self.logger.info("Serial connection closed")
        
        self.logger.info("Arduino Serial Controller stopped")
    
    def run(self):
        """Main execution loop"""
        self.logger.info("Starting Arduino Serial Controller...")
        
        # Connect to Arduino
        if not self.connect_arduino():
            self.logger.error("Failed to connect to Arduino. Exiting.")
            return
        
        # Send startup notification
        self.send_message('system_startup', {'version': '2024.12.25'})
        
        self.running = True
        
        # Start monitoring threads
        status_thread = threading.Thread(target=self.periodic_status_update, daemon=True)
        array_thread = threading.Thread(target=self.monitor_array_status, daemon=True)
        
        self.monitor_threads = [status_thread, array_thread]
        
        status_thread.start()
        array_thread.start()
        
        self.logger.info("Arduino Serial Controller is running...")
        
        try:
            # Main loop - keep the service alive
            while self.running:
                time.sleep(1)
                
                # Reconnect if connection lost
                if not self.serial_connection.is_open:
                    self.logger.warning("Serial connection lost, attempting to reconnect...")
                    if not self.connect_arduino():
                        self.logger.error("Failed to reconnect, will retry...")
                        time.sleep(self.config['retry_delay'])
                        
        except KeyboardInterrupt:
            self.logger.info("Keyboard interrupt received")
        except Exception as e:
            self.logger.error(f"Unexpected error in main loop: {e}")
        finally:
            self.shutdown()

def main():
    """Main entry point"""
    controller = ArduinoSerialController()
    controller.run()

if __name__ == '__main__':
    main()