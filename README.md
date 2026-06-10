# IoT Train Tracking System - ESP32 Firmware

## Overview
This repository contains the firmware for an IoT-based Train Tracking System built on the ESP32 platform using FreeRTOS (ESP-IDF). The system is designed with high reliability for industrial environments, specifically addressing network instability and EMI (Electromagnetic Interference) issues commonly found in railway operations.

## Key Technical Features
* **Store-and-Forward Mechanism:** Utilizes an SD card as an offline buffer to prevent telemetry data loss when the LTE/3G network connection drops. Data is automatically synchronized via MQTT once the connection is restored.
* **Adaptive Rate Limiting:** Dynamically adjusts the telemetry transmission rate based on network conditions and buffer status to prevent MQTT broker overload and optimize bandwidth.
* **Hardware-Level Fault Tolerance:** Implements robust watchdog timers and physical reset mechanisms (via PWRKEY GPIO) to recover from EMI-induced modem crashes.
* **RTOS Architecture:** Task-based architecture utilizing FreeRTOS features (Queues, Mutexes, Event Groups) to separate sensor data acquisition, network communication, and system monitoring.

## System Architecture
* **Microcontroller:** ESP32 (Dual-core, XTensa LX6)
* **RTOS:** FreeRTOS (ESP-IDF v5.x)
* **Network Interface:** LTE/4G Module via UART AT Commands
* **Protocol:** MQTT (QoS 1)
* **Storage:** SD Card (SPI Mode) for offline buffering

## Project Structure
* `main/`: Contains the core application logic.
  * `services/`: High-level business logic (MQTT service, Sensor acquisition, Command Handler).
  * `drivers/`: Hardware interaction (SD Card logging).
  * `hal/`: Hardware Abstraction Layer (LTE AT Command wrapper).
  * `config/`: System and Pin configurations.
  * `utils/`: Helper functions and logging utilities.

## Build and Flash
This project is built using the standard ESP-IDF toolchain.
```bash
# Set up ESP-IDF environment
get_idf

# Configure the project (if needed)
idf.py menuconfig

# Build the firmware
idf.py build

# Flash to the ESP32 and monitor
idf.py -p COM_PORT flash monitor
```

## License
MIT License
