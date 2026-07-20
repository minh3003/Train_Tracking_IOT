# Data Integrity Assurance Solution for IoT Train Tracking Systems

## Project Overview
This repository contains the embedded software (firmware) for the IoT Train Tracking System. The project focuses on a robust solution for collecting and transmitting real-time sensor data regarding train position, acceleration, and environmental conditions. Operating in the challenging railway environment characterized by frequent LTE/3G network dropouts and electromagnetic interference (EMI), this system is architected for high reliability, automatic fault recovery, and strict **zero data loss** guarantees.

## System Architecture
The system architecture features two parallel operational flows running on the ESP32 microcontroller: the Sensor Data Acquisition flow (Sensor Core) and the Communication Processing flow (Network Core). All data synchronization and management are handled via FreeRTOS Queues and Mutexes, ensuring thread-safe data integrity across concurrent processes.

## Hardware Components
- **Main Microcontroller:** ESP32 (Dual-core, XTensa LX6)
- **Cellular Network:** LTE/4G Module (AT Command Interface)
- **Positioning:** GPS/GNSS Module
- **Sensors:** Accelerometer (IMU), Temperature & Humidity Sensors
- **Offline Storage:** SD Card (SPI Interface)
- **Power Management:** Battery management and backup power system

## Software Stack
- **Operating System:** FreeRTOS (ESP-IDF v5.x)
- **Programming Language:** C (C99/C11)
- **Transport Protocol:** MQTT (QoS 1)
- **File System:** FAT32 (for SD Card)

## Key Features
- **Store-and-Forward (Zero Data Loss):** Automatically buffers data to the SD card during network outages and forwards it sequentially upon connection recovery.
- **Interleaved Flush & Adaptive Rate:** Intelligently flushes buffered data while adapting the sampling rate based on available network bandwidth to prevent MQTT broker overload.
- **Hardware-Level Fault Tolerance:** Integrated hardware Watchdog and self-diagnostic routines to recover from hardware freezes caused by severe EMI (via GPIO PWRKEY).
- **Core-Pinned Multitasking:** Isolates critical tasks across different CPU cores to prevent high-priority process interruptions.

## Directory Structure
- `main/`: Core firmware source code (ESP-IDF).
- `Hardware_Architecture/`: Schematics, PCB layouts, and hardware design files.
- `Software_Architecture/`: Flowcharts and software architecture documentation.
- `Demo/`: Live demonstration videos and system operation footage.
- `Docs/`: Project documentation and technical reference manuals.
- `Images/`: Diagram assets and project photos.
- `scripts_bieu_do/`: Python scripts for data visualization and analysis.
- **Utility Scripts:** `demo_live.py`, `log_mqtt.py`, `parse_heap.py` for debugging and logging.

## Build Instructions
This project utilizes the standard ESP-IDF toolchain. To build the firmware:

```bash
# 1. Setup the ESP-IDF environment
get_idf

# 2. Configure project parameters (if necessary)
idf.py menuconfig

# 3. Build the firmware binary (.bin)
idf.py build
```
*(Note: Never auto-build the project directly. Ensure your ESP-IDF environment is properly configured before building.)*

## Flash & Monitor
To flash the firmware to the ESP32 and monitor the serial output:

```bash
# Flash to the device and open the serial monitor
idf.py -p COM_PORT flash monitor
```

## Demonstration
*(Live demonstration videos and images can be found in the `Demo/` directory.)*

## Future Work
- **Context-Aware Adaptive Sampling:** Enhancing the dynamic sampling algorithm to optimize power and bandwidth consumption based on environmental context.
- **LittleFS Migration:** Upgrading the file system from FAT32 to LittleFS to optimize wear leveling and prevent memory fragmentation on the SD card.
- **Enhanced Security:** Integrating TLS (mbedTLS) for secure MQTT communication.
- **OTA Updates:** Implementing Over-The-Air (OTA) firmware update capabilities.

## License
[MIT License](LICENSE)
