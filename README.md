# esp32-relais-x8-modbus

This firmware turns an ESP32 board with an 8-relay module into a robust, network-controlled device using the Modbus TCP protocol. It provides a comprehensive interface for direct manual control, sophisticated timed operations, and device monitoring.

The project is built using PlatformIO and is designed for reliability and easy integration with standard SCADA, HMI, or other industrial control systems.

## Features

- **Modbus TCP Server:** Runs a standard Modbus TCP server on port 502.
- **8-Channel Relay Control:** Full control over 8 physical relays.
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
3.  **Configure Wi-Fi:** Open the `platformio.ini` file and modify the `src/main.cpp` file to add your Wi-Fi credentials:
    ```cpp
    // --- WiFi Credentials ---
    const char* ssid = "YOUR_WIFI_SSID";
    const char* password = "YOUR_WIFI_PASSWORD";
    ```
4.  **Build & Upload:** Connect your ESP32 board and use the PlatformIO "Upload" button. The required libraries will be downloaded automatically.

## Modbus Register Map

The device uses the following Modbus addresses for control and monitoring.

| Address | Type              | R/W       | Description                                                                                             |
| :------ | :---------------- | :-------- | :------------------------------------------------------------------------------------------------------ |
| **Control & Status Coils** |
| 0-7     | Coil              | Read/Write| **Manual Control & Status:** `1`=ON, `0`=OFF. Also reflects the state during a timed run.                  |
| 20-27   | Coil              | Write-Only| **Arm Relay:** Write `1` to arm the corresponding relay (0-7) for a timed run. Resets to `0` automatically. |
| 30      | Coil              | Write-Only| **Global Trigger:** Write `1` to start the timed run for all armed relays. Resets to `0` automatically.    |
| 40      | Coil              | Read-Only | **Any Relay On Status:** `1` if any relay is currently active, `0` otherwise.                               |
| 60      | Coil              | Write-Only| **Emergency Stop:** Write `1` to immediately stop all relays and clear all timers. Resets to `0`.         |
| **Configuration Registers** |
| 100-107 | Holding Register  | Read/Write| **Timed Run Duration:** Set the run time in milliseconds for the corresponding relay (0-7).                |
| **Device Information Registers** |
| 500     | Holding Register  | Read-Only | **Firmware Version:** e.g., a value of `101` represents v1.0.1.                                           |
| 501-510 | Holding Registers | Read-Only | **Device Name:** 20-character ASCII string packed into 10 registers.                                      |
| 511-512 | Holding Registers | Read-Only | **Serial Number:** 7-digit unique ID generated from the MAC address, stored as a 32-bit integer.          |

## How to Use the Timed Run Feature

The timed run logic is designed to be a safe, two-step process: **Arm** and **Trigger**.

1.  **Set Durations:** Write the desired run time in milliseconds to the Holding Registers for the relays you want to control (e.g., write `5000` to register `101` for a 5-second run on Relay 1).
2.  **Arm the Relays:** Write a `1` to the "Arm Relay" coil for each relay you want to include in the run (e.g., write `1` to coil `21` to arm Relay 1). You can arm multiple relays this way.
3.  **Execute:** Write a `1` to the "Global Trigger" coil (`30`). All armed relays will start running simultaneously for their configured durations.

This approach prevents accidental activation and allows for synchronized starting of multiple relays.

# Node.js Modbus TCP Test Client

This is a command-line interface (CLI) tool built with Node.js to test and interact with the **ESP32 Modbus TCP 8-Relay Controller**. It provides a simple, menu-driven way to send commands and read data from the ESP32 server over the network.

This tool is essential for debugging the ESP32 firmware, verifying its functionality, and serving as a reference client implementation.

## Features

- **Interactive CLI:** A user-friendly menu for selecting different Modbus operations.
- **Full Functionality Testing:** Supports all features of the ESP32 firmware:
    - Reading the status of all relays and the master status indicator.
    - Manually toggling individual relays on and off.
    - Configuring timed runs by setting durations and arming relays.
    - Sending the global trigger to execute synchronized timed runs.
    - Triggering the server-side Emergency Stop.
    - Reading all device identification registers (Firmware, Name, Serial Number).
- **Clear Feedback:** Displays responses from the server and provides confirmation for commands sent.

## Setup and Installation

### Prerequisites

- **Node.js:** You must have Node.js and `npm` (Node Package Manager) installed on your system. You can download it from [nodejs.org](https://nodejs.org/).
- **ESP32 Server Running:** The ESP32 Relay Controller must be powered on and connected to the same Wi-Fi network as the computer running this tool.

### Instructions

1.  **Get the Files:** Place the `index.js` and `package.json` files into a new folder on your computer.
2.  **Configure the IP Address:**
    - Find the IP address of your ESP32. This is printed in the Arduino Serial Monitor when the ESP32 boots up.
    - Open the `index.js` file and update the `HOST` constant with your ESP32's IP address.
    ```javascript
    const HOST = '192.168.1.123'; // <-- Change to your ESP32's IP address
    ```
3.  **Install Dependencies:** Open a terminal or command prompt, navigate to your project folder, and run the following command. This will download the required `jsmodbus` library.
    ```bash
    npm install
    ```
4.  **Run the Client:** Start the test client with this command:
    ```bash
    npm start
    ```

## Usage

Once the application is running, it will connect to the ESP32 and display a menu of options. Type the number corresponding to your desired action and press Enter.

### Menu Options

- **1. Read all relay statuses:** Polls the server and displays the current ON/OFF state of all 8 relays and the master status indicator.
- **2. Toggle a relay manually (ON/OFF):** Manually force a single relay ON or OFF.
- **3. Configure and Arm a Relay for Timed Run:** Prompts you to set a duration (in ms) for a specific relay and then "arms" it for a future timed run.
- **4. Execute Global Trigger:** Sends the command to start the timed run for all previously armed relays.
- **5. EMERGENCY STOP:** Sends the high-priority E-Stop command to the ESP32, which will immediately shut down all relays and clear their timers.
- **6. Read Device Information:** Fetches and displays the Device Name, Firmware Version, and unique Serial Number from the server.
- **7. Exit:** Closes the application.
