#!/usr/bin/env python3
"""
Arduino Serial Controller for Unraid
Communicates with Arduino microcontroller via serial connection
Sends system status and notifications
"""

import json
import logging
import os
import re
import signal
import subprocess
import sys
import threading
import time
import glob
from dataclasses import dataclass, field, fields, asdict
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, Optional, List, Union
import serial


# Constants
DEFAULT_CONFIG_PATH = '/boot/config/plugins/arduino-serial-controller/settings.cfg'
DEFAULT_LOG_DIR = Path('/var/log/arduino-serial-controller')
ARDUINO_INIT_DELAY = 2.0
ARRAY_CHECK_INTERVAL = 10
MAIN_LOOP_INTERVAL = 1
VERSION = '2025.05.26'

# Temperature sensor paths in order of preference
TEMP_SENSOR_PATHS = [
    '/sys/class/thermal/thermal_zone0/temp',
    '/sys/class/thermal/thermal_zone1/temp',
    '/sys/class/hwmon/hwmon0/temp1_input',
    '/sys/class/hwmon/hwmon1/temp1_input'
]

# Disk health priority (worst to best)
HEALTH_PRIORITY = {
    'FAILED': 0,
    'FAILING_NOW': 1,
    'PRE-FAIL': 2,
    'OLD_AGE': 3,
    'PASSED': 4,
    'OK': 5,
    'UNKNOWN': 6
}


@dataclass
class ArduinoMessage:
    """Optimized message structure for Arduino consumption"""
    # System info (short field names for Arduino efficiency)
    ts: str  # timestamp
    up: int  # uptime in seconds
    ct: Optional[float]  # cpu temperature
    as_: str  # array status (as is Python keyword)
    
    # Disk info
    d_temp: Optional[float]  # highest disk temperature
    d_cap: int  # total disk capacity in GB
    d_health: str  # worst disk health
    d_count: int  # number of disks
    
    # NVMe info
    n_temp: Optional[float]  # highest nvme temperature
    n_cap: int  # total nvme capacity in GB
    n_health: str  # worst nvme health
    n_count: int  # number of nvme devices
    
    # UPS info
    ups_online: bool  # UPS online status
    ups_batt: Optional[int]  # battery percentage
    ups_load: Optional[int]  # load percentage
    ups_runtime: Optional[int]  # estimated runtime in minutes
    ups_status: str  # UPS status string
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary, handling the 'as_' field name issue"""
        data = asdict(self)
        # Rename as_ to as for JSON since Arduino doesn't care about Python keywords
        if 'as_' in data:
            data['as'] = data.pop('as_')
        return data
    
    @classmethod
    def get_schema_info(cls) -> Dict[str, str]:
        """Get schema information for documentation/debugging"""
        return {
            'ts': 'timestamp (ISO format)',
            'up': 'uptime (seconds)',
            'ct': 'cpu_temperature (°C, nullable)',
            'as': 'array_status (started/stopped/unknown)',
            'd_temp': 'max_disk_temperature (°C, nullable)',
            'd_cap': 'total_disk_capacity (GB)',
            'd_health': 'worst_disk_health (PASSED/FAILED/etc)',
            'd_count': 'disk_count',
            'n_temp': 'max_nvme_temperature (°C, nullable)', 
            'n_cap': 'total_nvme_capacity (GB)',
            'n_health': 'worst_nvme_health (PASSED/FAILED/etc)',
            'n_count': 'nvme_count',
            'ups_online': 'ups_online_status (boolean)',
            'ups_batt': 'ups_battery_percentage (nullable)',
            'ups_load': 'ups_load_percentage (nullable)',
            'ups_runtime': 'ups_runtime_minutes (nullable)',
            'ups_status': 'ups_status_string'
        }


@dataclass
class DiskInfo:
    """Information about a storage device"""
    device: str
    capacity_gb: int
    temperature: Optional[float]
    health: str
    is_nvme: bool


@dataclass
class ArduinoControllerConfig:
    """Configuration dataclass for Arduino Serial Controller"""
    serial_port: str = '/dev/ttyUSB0'
    baud_rate: int = 9600
    update_interval: int = 30  # seconds
    timeout: int = 5
    log_level: str = 'INFO'
    retry_attempts: int = 3
    retry_delay: int = 5
    ups_name: str = 'ups'  # NUT UPS name
    enable_disk_monitoring: bool = True
    enable_ups_monitoring: bool = True
    
    def __post_init__(self) -> None:
        """Validate configuration after initialization"""
        self._validate()
    
    def _validate(self) -> None:
        """Validate configuration values"""
        if self.baud_rate <= 0:
            raise ValueError("Baud rate must be positive")
        
        if self.update_interval <= 0:
            raise ValueError("Update interval must be positive")
        
        if self.timeout <= 0:
            raise ValueError("Timeout must be positive")
        
        if self.retry_attempts < 0:
            raise ValueError("Retry attempts cannot be negative")
        
        if self.retry_delay < 0:
            raise ValueError("Retry delay cannot be negative")
        
        valid_log_levels = ['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL']
        if self.log_level.upper() not in valid_log_levels:
            raise ValueError(f"Log level must be one of: {valid_log_levels}")
        
        # Normalize log level to uppercase
        self.log_level = self.log_level.upper()
    
    @classmethod
    def from_file(cls, config_file: str) -> 'ArduinoControllerConfig':
        """Load configuration from file"""
        config_data = {}
        
        if os.path.exists(config_file):
            try:
                with open(config_file, 'r') as f:
                    for line in f:
                        line = line.strip()
                        if '=' in line and not line.startswith('#'):
                            key, value = line.split('=', 1)
                            key = key.strip()
                            value = value.strip()
                            
                            # Convert values to appropriate types
                            config_data[key] = cls._convert_value(key, value)
                            
            except Exception as e:
                logging.error(f"Error loading config from {config_file}: {e}")
                
        return cls(**config_data)
    
    @staticmethod
    def _convert_value(key: str, value: str) -> Any:
        """Convert string value to appropriate type based on field"""
        # Get the expected type from dataclass fields
        field_types = {f.name: f.type for f in fields(ArduinoControllerConfig)}
        expected_type = field_types.get(key)
        
        if expected_type == int:
            try:
                return int(value)
            except ValueError:
                logging.warning(f"Invalid integer value for {key}: {value}")
                return value
        elif expected_type == float:
            try:
                return float(value)
            except ValueError:
                logging.warning(f"Invalid float value for {key}: {value}")
                return value
        elif expected_type == bool:
            return value.lower() in ('true', '1', 'yes', 'on')
        
        return value


class SystemMonitor:
    """System monitoring utilities"""
    
    @staticmethod
    def get_cpu_temperature() -> Optional[float]:
        """Get CPU temperature from system sensors"""
        for sensor_path in TEMP_SENSOR_PATHS:
            try:
                if os.path.exists(sensor_path):
                    with open(sensor_path, 'r') as f:
                        temp = int(f.read().strip())
                        # Convert millidegrees to degrees if necessary
                        if temp > 1000:
                            temp = temp / 1000
                        return round(temp, 1)
            except (OSError, ValueError) as e:
                logging.debug(f"Failed to read temperature from {sensor_path}: {e}")
                continue
        
        logging.warning("No CPU temperature sensors found")
        return None
    
    @staticmethod
    def get_uptime() -> int:
        """Get system uptime in seconds"""
        try:
            with open('/proc/uptime', 'r') as f:
                uptime_seconds = float(f.read().split()[0])
                return int(uptime_seconds)
        except (OSError, ValueError, IndexError) as e:
            logging.error(f"Error reading uptime: {e}")
            return 0
    
    @staticmethod
    def get_array_status() -> str:
        """Get Unraid array status using Unraid-specific methods"""
        try:
            # Method 1: Check Unraid's var.ini file (most reliable)
            var_ini_path = '/var/local/emhttp/var.ini'
            if os.path.exists(var_ini_path):
                with open(var_ini_path, 'r') as f:
                    for line in f:
                        if line.startswith('mdState='):
                            state = line.split('=')[1].strip().strip('"')
                            # Unraid states: STOPPED, STARTED, STARTING, STOPPING
                            if state in ['STARTED']:
                                return 'started'
                            elif state in ['STOPPED']:
                                return 'stopped'
                            elif state in ['STARTING', 'STOPPING']:
                                return 'transitioning'
                            else:
                                return state.lower()
            
            # Method 2: Check if array mount point exists and has mounted drives
            if os.path.exists('/mnt/user') and os.path.ismount('/mnt/user'):
                # Check if any drives are mounted under /mnt/disk*
                import glob
                mounted_disks = glob.glob('/mnt/disk*')
                if mounted_disks:
                    # Check if any of these are actually mounted
                    for disk_path in mounted_disks:
                        if os.path.ismount(disk_path):
                            return 'started'
            
            # Method 3: Use mdcmd command (Unraid specific)
            try:
                result = subprocess.run(['mdcmd', 'status'], 
                                      capture_output=True, text=True, timeout=5)
                if result.returncode == 0:
                    output = result.stdout.lower()
                    if 'started' in output:
                        return 'started'
                    elif 'stopped' in output:
                        return 'stopped'
            except (subprocess.TimeoutExpired, subprocess.SubprocessError, FileNotFoundError):
                pass  # mdcmd might not be available
            
            # Method 4: Fallback - check /proc/mdstat but with better parsing
            if os.path.exists('/proc/mdstat'):
                with open('/proc/mdstat', 'r') as f:
                    mdstat = f.read()
                    # Look for active md devices
                    if 'md' in mdstat and 'active' in mdstat:
                        return 'started'
            
            # If we can't determine status, assume stopped
            return 'stopped'
            
        except Exception as e:
            logging.error(f"Error reading Unraid array status: {e}")
            return 'unknown'


class DiskMonitor:
    """Disk and NVMe monitoring utilities"""
    
    @staticmethod
    def _run_command(cmd: List[str]) -> Optional[str]:
        """Run a command and return stdout, or None on error"""
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            if result.returncode == 0:
                return result.stdout.strip()
            else:
                logging.debug(f"Command {' '.join(cmd)} failed with code {result.returncode}: {result.stderr}")
        except (subprocess.TimeoutExpired, subprocess.SubprocessError, FileNotFoundError) as e:
            logging.debug(f"Command {' '.join(cmd)} failed: {e}")
        return None
    
    @classmethod
    def get_all_disks(cls) -> List[DiskInfo]:
        """Get information about all disks"""
        disks = []
        
        # Get list of block devices
        lsblk_output = cls._run_command(['lsblk', '-d', '-n', '-o', 'NAME,SIZE,TYPE'])
        if not lsblk_output:
            logging.warning("Could not get disk list from lsblk")
            return disks
        
        for line in lsblk_output.split('\n'):
            if not line.strip():
                continue
                
            parts = line.split()
            if len(parts) < 3:
                continue
                
            device_name = parts[0]
            device_type = parts[2] if len(parts) > 2 else ''
            
            # Skip non-disk devices
            if device_type not in ['disk']:
                continue
            
            # Skip loop, ram, and other virtual devices
            if device_name.startswith(('loop', 'ram', 'dm-')):
                continue
            
            device_path = f'/dev/{device_name}'
            disk_info = cls._get_disk_info(device_path)
            if disk_info:
                disks.append(disk_info)
        
        return disks
    
    @classmethod
    def _get_disk_info(cls, device_path: str) -> Optional[DiskInfo]:
        """Get detailed information about a specific disk"""
        try:
            device_name = os.path.basename(device_path)
            is_nvme = device_name.startswith('nvme')
            
            # Get capacity
            capacity_gb = cls._get_disk_capacity(device_path)
            
            # Get SMART info
            temperature, health = cls._get_smart_info(device_path)
            
            return DiskInfo(
                device=device_name,
                capacity_gb=capacity_gb,
                temperature=temperature,
                health=health,
                is_nvme=is_nvme
            )
            
        except Exception as e:
            logging.debug(f"Error getting info for {device_path}: {e}")
            return None
    
    @classmethod
    def _get_disk_capacity(cls, device_path: str) -> int:
        """Get disk capacity in GB"""
        try:
            # Try to get size from lsblk
            output = cls._run_command(['lsblk', '-d', '-n', '-b', '-o', 'SIZE', device_path])
            if output:
                size_bytes = int(output)
                return int(size_bytes / (1024 ** 3))  # Convert to GB
        except (ValueError, TypeError):
            pass
        
        # Fallback: try to read from /sys/block
        try:
            device_name = os.path.basename(device_path)
            with open(f'/sys/block/{device_name}/size', 'r') as f:
                sectors = int(f.read().strip())
                # Assume 512 byte sectors
                size_bytes = sectors * 512
                return int(size_bytes / (1024 ** 3))
        except (OSError, ValueError):
            pass
        
        logging.warning(f"Could not determine capacity for {device_path}")
        return 0
    
    @classmethod
    def _get_smart_info(cls, device_path: str) -> tuple[Optional[float], str]:
        """Get SMART temperature and health info"""
        temperature = None
        health = 'UNKNOWN'
        
        # Try smartctl
        output = cls._run_command(['smartctl', '-A', '-H', device_path])
        if not output:
            return temperature, health
        
        # Parse health
        if 'PASSED' in output:
            health = 'PASSED'
        elif 'FAILED' in output:
            health = 'FAILED'
        elif 'FAILING_NOW' in output:
            health = 'FAILING_NOW'
        
        # Parse temperature - look for common temperature attributes
        temp_patterns = [
            r'Temperature_Celsius.*?(\d+)',
            r'Airflow_Temperature_Cel.*?(\d+)',
            r'Temperature.*?(\d+)',
        ]
        
        for pattern in temp_patterns:
            match = re.search(pattern, output, re.IGNORECASE)
            if match:
                try:
                    temperature = float(match.group(1))
                    break
                except ValueError:
                    continue
        
        return temperature, health
    
    @classmethod
    def aggregate_disk_data(cls, disks: List[DiskInfo]) -> tuple[List[DiskInfo], List[DiskInfo]]:
        """Separate and return traditional disks and NVMe devices"""
        traditional_disks = [disk for disk in disks if not disk.is_nvme]
        nvme_disks = [disk for disk in disks if disk.is_nvme]
        
        return traditional_disks, nvme_disks
    
    @staticmethod
    def get_worst_health(disks: List[DiskInfo]) -> str:
        """Get the worst health status from a list of disks"""
        if not disks:
            return 'UNKNOWN'
        
        worst_health = 'UNKNOWN'
        worst_priority = HEALTH_PRIORITY.get(worst_health, 99)
        
        for disk in disks:
            disk_priority = HEALTH_PRIORITY.get(disk.health, 99)
            if disk_priority < worst_priority:
                worst_health = disk.health
                worst_priority = disk_priority
        
        return worst_health
    
    @staticmethod
    def get_max_temperature(disks: List[DiskInfo]) -> Optional[float]:
        """Get the maximum temperature from a list of disks"""
        temperatures = [disk.temperature for disk in disks if disk.temperature is not None]
        return max(temperatures) if temperatures else None
    
    @staticmethod
    def get_total_capacity(disks: List[DiskInfo]) -> int:
        """Get total capacity from a list of disks"""
        return sum(disk.capacity_gb for disk in disks)


class UPSMonitor:
    """UPS monitoring via NUT"""
    
    @staticmethod
    def _run_upsc(ups_name: str) -> Dict[str, str]:
        """Run upsc command and parse output"""
        try:
            result = subprocess.run(['upsc', ups_name], capture_output=True, text=True, timeout=10)
            if result.returncode != 0:
                logging.debug(f"upsc command failed: {result.stderr}")
                return {}
            
            ups_data = {}
            for line in result.stdout.split('\n'):
                if ':' in line:
                    key, value = line.split(':', 1)
                    ups_data[key.strip()] = value.strip()
            
            return ups_data
            
        except (subprocess.TimeoutExpired, subprocess.SubprocessError, FileNotFoundError) as e:
            logging.debug(f"Error running upsc: {e}")
            return {}
    
    @classmethod
    def get_ups_status(cls, ups_name: str) -> Dict[str, Any]:
        """Get UPS status information"""
        ups_data = cls._run_upsc(ups_name)
        
        if not ups_data:
            return {
                'online': False,
                'battery_percent': None,
                'load_percent': None,
                'runtime_minutes': None,
                'status': 'UNAVAILABLE'
            }
        
        # Parse common UPS values
        try:
            battery_percent = None
            if 'battery.charge' in ups_data:
                battery_percent = int(float(ups_data['battery.charge']))
            
            load_percent = None
            if 'ups.load' in ups_data:
                load_percent = int(float(ups_data['ups.load']))
            
            runtime_minutes = None
            if 'battery.runtime' in ups_data:
                runtime_minutes = int(float(ups_data['battery.runtime']) / 60)  # Convert seconds to minutes
            
            status = ups_data.get('ups.status', 'UNKNOWN')
            online = 'OL' in status  # Online status typically contains 'OL'
            
            return {
                'online': online,
                'battery_percent': battery_percent,
                'load_percent': load_percent,
                'runtime_minutes': runtime_minutes,
                'status': status
            }
            
        except (ValueError, KeyError) as e:
            logging.debug(f"Error parsing UPS data: {e}")
            return {
                'online': False,
                'battery_percent': None,
                'load_percent': None,
                'runtime_minutes': None,
                'status': 'PARSE_ERROR'
            }


class ArduinoSerialController:
    """Main Arduino Serial Controller class"""
    
    def __init__(self, config_file: str = DEFAULT_CONFIG_PATH):
        """Initialize the Arduino Serial Controller"""
        self.config = ArduinoControllerConfig.from_file(config_file)
        self.serial_connection: Optional[serial.Serial] = None
        self.running = False
        self.shutdown_initiated = False
        
        # Setup logging
        self.logger = self._setup_logging()
        
        # Print schema info on startup
        self._log_schema_info()
        
        # Threading for monitoring different events
        self.monitor_threads: List[threading.Thread] = []
        
        # Signal handlers for graceful shutdown
        signal.signal(signal.SIGTERM, self._signal_handler)
        signal.signal(signal.SIGINT, self._signal_handler)
        
        self.logger.info("Arduino Serial Controller initialized")
    
    def _log_schema_info(self) -> None:
        """Log the message schema for reference"""
        self.logger.info("=== Arduino Message Schema ===")
        schema = ArduinoMessage.get_schema_info()
        for field, description in schema.items():
            self.logger.info(f"  {field}: {description}")
        self.logger.info("=" * 30)
    
    def _setup_logging(self) -> logging.Logger:
        """Setup logging configuration"""
        DEFAULT_LOG_DIR.mkdir(exist_ok=True)
        
        # Create formatter
        formatter = logging.Formatter(
            '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
        )
        
        # Setup logger
        logger = logging.getLogger(__name__)
        logger.setLevel(getattr(logging, self.config.log_level))
        
        # Clear any existing handlers
        logger.handlers.clear()
        
        # File handler
        file_handler = logging.FileHandler(DEFAULT_LOG_DIR / 'arduino_controller.log')
        file_handler.setFormatter(formatter)
        logger.addHandler(file_handler)
        
        # Console handler
        console_handler = logging.StreamHandler(sys.stdout)
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)
        
        return logger
    
    def _connect_arduino(self) -> bool:
        """Establish serial connection with Arduino"""
        for attempt in range(self.config.retry_attempts):
            try:
                self.serial_connection = serial.Serial(
                    port=self.config.serial_port,
                    baudrate=self.config.baud_rate,
                    timeout=self.config.timeout
                )
                self.logger.info(f"Connected to Arduino on {self.config.serial_port}")
                time.sleep(ARDUINO_INIT_DELAY)  # Give Arduino time to initialize
                return True
                
            except serial.SerialException as e:
                self.logger.error(f"Serial connection attempt {attempt + 1} failed: {e}")
                if attempt < self.config.retry_attempts - 1:
                    time.sleep(self.config.retry_delay)
            except Exception as e:
                self.logger.error(f"Unexpected error during connection attempt {attempt + 1}: {e}")
                if attempt < self.config.retry_attempts - 1:
                    time.sleep(self.config.retry_delay)
        
        return False
    
    def _send_message(self, message_type: str, data: Optional[Dict[str, Any]] = None) -> bool:
        """Send message to Arduino with detailed logging"""
        if not self.serial_connection or not self.serial_connection.is_open:
            self.logger.warning("Cannot send message: serial connection not available")
            return False
        
        try:
            message = {
                'type': message_type,
                'timestamp': datetime.now().isoformat(),
                'data': data or {}
            }
            
            json_message = json.dumps(message) + '\n'
            
            # Log the data being sent (feature request #1)
            self.logger.info(f"Sending to Arduino - Type: {message_type}, Data: {json.dumps(data, indent=2) if data else 'None'}")
            
            self.serial_connection.write(json_message.encode('utf-8'))
            self.serial_connection.flush()
            
            self.logger.debug(f"Successfully sent message: {message_type}")
            return True
            
        except (serial.SerialException, OSError) as e:
            self.logger.error(f"Serial error sending message: {e}")
            return False
        except json.JSONEncodeError as e:
            self.logger.error(f"JSON encoding error: {e}")
            return False
        except Exception as e:
            self.logger.error(f"Unexpected error sending message: {e}")
            return False
    
    def _send_arduino_message(self, arduino_msg: ArduinoMessage) -> bool:
        """Send optimized Arduino message"""
        return self._send_message('status_update', arduino_msg.to_dict())
    
    def _get_system_status(self) -> ArduinoMessage:
        """Collect comprehensive system status in optimized format"""
        # Basic system info
        cpu_temp = SystemMonitor.get_cpu_temperature()
        uptime = SystemMonitor.get_uptime()
        array_status = SystemMonitor.get_array_status()
        
        # Initialize default values
        disk_temp, disk_cap, disk_health, disk_count = None, 0, 'UNKNOWN', 0
        nvme_temp, nvme_cap, nvme_health, nvme_count = None, 0, 'UNKNOWN', 0
        ups_online, ups_batt, ups_load, ups_runtime, ups_status = False, None, None, None, 'UNAVAILABLE'
        
        # Get disk information if enabled
        if self.config.enable_disk_monitoring:
            try:
                all_disks = DiskMonitor.get_all_disks()
                traditional_disks, nvme_disks = DiskMonitor.aggregate_disk_data(all_disks)
                
                # Traditional disks
                if traditional_disks:
                    disk_temp = DiskMonitor.get_max_temperature(traditional_disks)
                    disk_cap = DiskMonitor.get_total_capacity(traditional_disks)
                    disk_health = DiskMonitor.get_worst_health(traditional_disks)
                    disk_count = len(traditional_disks)
                
                # NVMe disks
                if nvme_disks:
                    nvme_temp = DiskMonitor.get_max_temperature(nvme_disks)
                    nvme_cap = DiskMonitor.get_total_capacity(nvme_disks)
                    nvme_health = DiskMonitor.get_worst_health(nvme_disks)
                    nvme_count = len(nvme_disks)
                    
            except Exception as e:
                self.logger.error(f"Error collecting disk information: {e}")
        
        # Get UPS information if enabled
        if self.config.enable_ups_monitoring:
            try:
                ups_info = UPSMonitor.get_ups_status(self.config.ups_name)
                ups_online = ups_info['online']
                ups_batt = ups_info['battery_percent']
                ups_load = ups_info['load_percent']
                ups_runtime = ups_info['runtime_minutes']
                ups_status = ups_info['status']
            except Exception as e:
                self.logger.error(f"Error collecting UPS information: {e}")
        
        return ArduinoMessage(
            ts=datetime.now().isoformat(),
            up=uptime,
            ct=cpu_temp,
            as_=array_status,
            d_temp=disk_temp,
            d_cap=disk_cap,
            d_health=disk_health,
            d_count=disk_count,
            n_temp=nvme_temp,
            n_cap=nvme_cap,
            n_health=nvme_health,
            n_count=nvme_count,
            ups_online=ups_online,
            ups_batt=ups_batt,
            ups_load=ups_load,
            ups_runtime=ups_runtime,
            ups_status=ups_status
        )
    
    def _monitor_array_status(self) -> None:
        """Monitor array status changes"""
        last_status: Optional[str] = None
        
        while self.running:
            try:
                current_status = SystemMonitor.get_array_status()
                
                if last_status is not None and current_status != last_status:
                    self.logger.info(f"Array status changed: {last_status} -> {current_status}")
                    self._send_message('array_status_change', {
                        'previous_status': last_status,
                        'current_status': current_status
                    })
                
                last_status = current_status
                time.sleep(ARRAY_CHECK_INTERVAL)
                
            except Exception as e:
                self.logger.error(f"Error monitoring array status: {e}")
                time.sleep(ARRAY_CHECK_INTERVAL)
    
    def _periodic_status_update(self) -> None:
        """Send periodic status updates to Arduino"""
        while self.running:
            try:
                status = self._get_system_status()
                self._send_arduino_message(status)
                
                time.sleep(self.config.update_interval)
                
            except Exception as e:
                self.logger.error(f"Error in periodic update: {e}")
                time.sleep(self.config.update_interval)
    
    def _signal_handler(self, signum: int, frame: Any) -> None:
        """Handle shutdown signals"""
        self.logger.info(f"Received signal {signum}, initiating shutdown...")
        self.shutdown()
    
    def _is_connection_healthy(self) -> bool:
        """Check if serial connection is healthy"""
        return (self.serial_connection is not None and 
                self.serial_connection.is_open)
    
    def shutdown(self) -> None:
        """Graceful shutdown"""
        if self.shutdown_initiated:
            return
            
        self.shutdown_initiated = True
        self.logger.info("Shutting down Arduino Serial Controller...")
        
        # Send shutdown notification to Arduino
        self._send_message('system_shutdown', {'reason': 'service_stop'})
        
        # Stop the main loop
        self.running = False
        
        # Wait for threads to finish
        for thread in self.monitor_threads:
            if thread.is_alive():
                thread.join(timeout=5)
                if thread.is_alive():
                    self.logger.warning(f"Thread {thread.name} did not finish gracefully")
        
        # Close serial connection
        if self.serial_connection and self.serial_connection.is_open:
            time.sleep(1)  # Give Arduino time to process shutdown message
            try:
                self.serial_connection.close()
                self.logger.info("Serial connection closed")
            except Exception as e:
                self.logger.error(f"Error closing serial connection: {e}")
        
        self.logger.info("Arduino Serial Controller stopped")
    
    def run(self) -> None:
        """Main execution loop"""
        self.logger.info("Starting Arduino Serial Controller...")
        
        # Connect to Arduino
        if not self._connect_arduino():
            self.logger.error("Failed to connect to Arduino. Exiting.")
            return
        
        # Send startup notification
        self._send_message('system_startup', {'version': VERSION})
        
        self.running = True
        
        # Start monitoring threads
        status_thread = threading.Thread(
            target=self._periodic_status_update, 
            daemon=True, 
            name="StatusUpdateThread"
        )
        array_thread = threading.Thread(
            target=self._monitor_array_status, 
            daemon=True, 
            name="ArrayMonitorThread"
        )
        
        self.monitor_threads = [status_thread, array_thread]
        
        status_thread.start()
        array_thread.start()
        
        self.logger.info("Arduino Serial Controller is running...")
        
        try:
            # Main loop - keep the service alive
            while self.running:
                time.sleep(MAIN_LOOP_INTERVAL)
                
                # Reconnect if connection lost
                if not self._is_connection_healthy():
                    self.logger.warning("Serial connection lost, attempting to reconnect...")
                    if not self._connect_arduino():
                        self.logger.error("Failed to reconnect, will retry...")
                        time.sleep(self.config.retry_delay)
                        
        except KeyboardInterrupt:
            self.logger.info("Keyboard interrupt received")
        except Exception as e:
            self.logger.error(f"Unexpected error in main loop: {e}")
        finally:
            self.shutdown()


def main() -> None:
    """Main entry point"""
    try:
        controller = ArduinoSerialController()
        controller.run()
    except Exception as e:
        logging.error(f"Failed to start Arduino Serial Controller: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()