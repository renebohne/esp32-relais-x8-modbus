# ESP32 Relay Modbus Controller (Multi-Board)

This firmware turns an ESP32 board with a relay module into a robust, network-controlled device using the Modbus TCP protocol. It provides a comprehensive interface for direct manual control, sophisticated timed operations, and device monitoring.

**This project now supports multiple boards from a single codebase.**

## Supported Boards

- **8-Channel Relay Board:** A generic ESP32-DevKitC with a standard 8-channel relay module.
- **6-Channel XIAO Board:** A Seeed Studio XIAO ESP32-C6 with a 6-channel relay board.

## Features

- **Modbus TCP Server:** Runs a standard Modbus TCP server on port 502.
- **Multi-Relay Control:** Full control over 6 or 8 physical relays, depending on the compiled version.
- **Dual Control Modes:**
    - **Manual Mode:** Instantly turn relays on or off.
    - **Timed Run Mode:** Configure a specific run duration (in milliseconds) for each relay and trigger them to run for that set time.
- **Group Execution:** Arm multiple relays with their individual timers and start them all simultaneously with a single global command.
- **Safety and Reliability:**
    - **Emergency Stop:** A dedicated command to immediately halt all relays and reset all timers.
    - **State Feedback:** Modbus coils accurately reflect the true physical state of the relays at all times.
    - **Master Status Indicator:** A single coil indicates if any relay is currently active.
- **Device Identification:** Read-only registers provide essential device info, including a unique serial number generated from the device's MAC address.

## PlatformIO Setup

1.  **Clone the Repository:** Get the project files onto your local machine.
2.  **Install PlatformIO:** Make sure you have the PlatformIO IDE extension installed in Visual Studio Code.
3.  **Select Your Board Environment:**
    - In VS Code, click the PlatformIO icon in the sidebar.
    - Under "Project Tasks", you will see the environments defined in `platformio.ini`.
    - To build for the **8-channel board**, select the `esp32dev_8_relay` environment.
    - To build for the **6-channel XIAO board**, select the `xiao_c6_6_relay` environment.
4.  **Configure Wi-Fi:** Open the `src/main.cpp` file and add your Wi-Fi credentials:
    ```cpp
    // --- WiFi Credentials ---
    const char* ssid = "YOUR_WIFI_SSID";
    const char* password = "YOUR_WIFI_PASSWORD";
    ```
5.  **Build & Upload:** Connect your ESP32 board and use the PlatformIO "Upload" button for your selected environment. The required libraries will be downloaded automatically.

## Modbus Register Map

The device uses a consistent 8-channel Modbus map regardless of the physical hardware. When using the 6-relay board, commands to channels 7 and 8 will be accepted but will have no physical effect.

| Address | Type              | R/W       | Description                                                                                             |
| :------ | :---------------- | :-------- | :------------------------------------------------------------------------------------------------------ |
| **Control & Status Coils** |
| 0-7     | Coil              | Read/Write| **Manual Control & Status:** `1`=ON, `0`=OFF. Also reflects the state during a timed run.                  |
| 20-27   | Coil              | Write-Only| **Arm Relay:** Write `1` to arm the corresponding relay for a timed run. Resets to `0` automatically. |
| 30      | Coil              | Write-Only| **Global Trigger:** Write `1` to start the timed run for all armed relays. Resets to `0` automatically.    |
| 40      | Coil              | Read-Only | **Any Relay On Status:** `1` if any relay is currently active, `0` otherwise.                               |
| 60      | Coil              | Write-Only| **Emergency Stop:** Write `1` to immediately stop all relays and clear all timers. Resets to `0`.         |
| **Configuration Registers** |
| 100-107 | Holding Register  | Read/Write| **Timed Run Duration:** Set the run time in milliseconds for the corresponding relay.                |
| **Device Information Registers** |
| 500     | Holding Register  | Read-Only | **Firmware Version:** e.g., a value of `102` represents v1.0.2.                                           |
| 501-510 | Holding Registers | Read-Only | **Device Name:** 20-character ASCII string packed into 10 registers.                                      |
| 511-512 | Holding Registers | Read-Only | **Serial Number:** 7-digit unique ID generated from the MAC address, stored as a 32-bit integer.          |

## Node.js Test Client
The test client is configured for 8 relays by default but can be easily adjusted. Open `test-client/index.js` and change the `RELAY_COUNT` constant to `6` if you are testing the XIAO board.
```javascript
const RELAY_COUNT = 8; // Change to 6 for the XIAO board