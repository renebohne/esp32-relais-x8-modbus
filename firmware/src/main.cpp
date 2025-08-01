#include <Arduino.h>
#include <WiFi.h>
#include <ModbusIP_ESP8266.h>

// --- Board Selection ---
// This is controlled by the build_flags in platformio.ini
#if defined(BOARD_8_RELAY)
    // --- Configuration for the original 8-Relay Board ---
    #define LED_PIN 2
    const int relayCount = 8;
    const int relayPins[relayCount] = {13, 12, 14, 27, 26, 25, 33, 32};
    const char* DEVICE_NAME = "ESP32 8-Relay Board";
#elif defined(BOARD_6_RELAY)
    // --- Configuration for the XIAO ESP32-C6 6-Relay Board ---
    const int relayCount = 6;
    const int relayPins[relayCount] = {2, 21, 1, 0, 19, 18};
    const char* DEVICE_NAME = "XIAO 6-Relay Board";
#else
    #error "No board specified! Please define BOARD_8_RELAY or BOARD_6_RELAY in platformio.ini"
#endif


// --- WiFi Credentials ---
// IMPORTANT: Enter your network credentials here
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";


// --- Device Information (Read-Only) ---
const uint16_t FIRMWARE_VERSION = 102; // Represents v1.0.2

// --- Modbus Object ---
ModbusIP mb;

// --- State-holding variables for timed run logic ---
unsigned long motorStartTimes[8] = {0}; // Always use 8 for Modbus compatibility
uint32_t motorRunDurations[8] = {0};
bool motorInTimedRun[8] = {false};
bool relayIsArmed[8] = {false};

// Modbus addresses
const int COIL_MANUAL_START_ADDR = 0;
const int HREG_DURATION_START_ADDR = 100;
const int COIL_ARM_RELAY_START_ADDR = 20;
const int COIL_GLOBAL_TRIGGER_ADDR = 30;
const int COIL_ANY_RELAY_ON_ADDR = 40;
const int COIL_EMERGENCY_STOP_ADDR = 60;

// Device Information Registers
const int HREG_FIRMWARE_VERSION_ADDR = 500;
const int HREG_DEVICE_NAME_START_ADDR = 501;
const int HREG_DEVICE_NAME_LEN = 10;
const int HREG_SERIAL_NUMBER_START_ADDR = 511;

void setup() {
  Serial.begin(115200);
  
#if defined(LED_PIN)
  pinMode(LED_PIN, OUTPUT);
#endif

  // Initialize Relay Pins
  for (int i = 0; i < relayCount; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.print("Board: ");
  Serial.println(DEVICE_NAME);
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());


  // Setup Modbus Server
  mb.server();

  // --- Add Control & Status Registers ---
  // We will always register all 8 coils/hregs to maintain compatibility with clients
  for (int i = 0; i < 8; i++) {
    mb.addCoil(COIL_MANUAL_START_ADDR + i);
    mb.addHreg(HREG_DURATION_START_ADDR + i);
    mb.addCoil(COIL_ARM_RELAY_START_ADDR + i);
  }
  mb.addCoil(COIL_GLOBAL_TRIGGER_ADDR);
  mb.addCoil(COIL_ANY_RELAY_ON_ADDR);
  mb.addCoil(COIL_EMERGENCY_STOP_ADDR);

  // --- Add and Populate Device Info Registers ---
  mb.addHreg(HREG_FIRMWARE_VERSION_ADDR, FIRMWARE_VERSION);

  for (int i = 0; i < HREG_DEVICE_NAME_LEN; i++) {
    mb.addHreg(HREG_DEVICE_NAME_START_ADDR + i, 0);
  }
  for (int i = 0; i < strlen(DEVICE_NAME) && i < (HREG_DEVICE_NAME_LEN * 2); i += 2) {
    char c1 = DEVICE_NAME[i];
    char c2 = (i + 1 < strlen(DEVICE_NAME)) ? DEVICE_NAME[i+1] : '\0';
    uint16_t val = (c1 << 8) | c2;
    mb.Hreg(HREG_DEVICE_NAME_START_ADDR + (i/2), val);
  }

  uint8_t mac[6];
  WiFi.macAddress(mac);
  uint32_t mac_suffix_32bit = (uint32_t)(mac[2] << 24) | (uint32_t)(mac[3] << 16) | (uint32_t)(mac[4] << 8) | mac[5];
  uint32_t seven_digit_serial = mac_suffix_32bit % 10000000;

  mb.addHreg(HREG_SERIAL_NUMBER_START_ADDR);
  mb.addHreg(HREG_SERIAL_NUMBER_START_ADDR + 1);
  uint16_t serial_high = (uint16_t)(seven_digit_serial >> 16);
  uint16_t serial_low = (uint16_t)(seven_digit_serial & 0xFFFF);
  mb.Hreg(HREG_SERIAL_NUMBER_START_ADDR, serial_high);
  mb.Hreg(HREG_SERIAL_NUMBER_START_ADDR + 1, serial_low);
  
  Serial.print("Generated 7-Digit Serial: ");
  Serial.println(seven_digit_serial);
}

void loop() {
  mb.task();

  // --- 0. Check for Emergency Stop (HIGHEST PRIORITY) ---
  if (mb.Coil(COIL_EMERGENCY_STOP_ADDR)) {
    for (int i = 0; i < relayCount; i++) { // Loop only through the available relays
        digitalWrite(relayPins[i], LOW);
    }
    // Also reset the state for all 8 coils/variables in the modbus map for consistency
    for (int i=0; i < 8; i++) {
        mb.Coil(COIL_MANUAL_START_ADDR + i, 0);
        mb.Hreg(HREG_DURATION_START_ADDR + i, 0);
        motorInTimedRun[i] = false;
        relayIsArmed[i] = false;
    }
    // Reset the E-Stop trigger itself
    mb.Coil(COIL_EMERGENCY_STOP_ADDR, 0);
    // Skip the rest of the loop to ensure the stop state is clean
    return; 
  }


  // --- 1. Check for Arming Commands ---
  for (int i = 0; i < relayCount; i++) { // Loop only through the available relays
    if (mb.Coil(COIL_ARM_RELAY_START_ADDR + i)) {
      relayIsArmed[i] = true;
      mb.Coil(COIL_ARM_RELAY_START_ADDR + i, 0);
    }
  }

  // --- 2. Check for the Global Trigger Command ---
  if (mb.Coil(COIL_GLOBAL_TRIGGER_ADDR)) {
    for (int i = 0; i < relayCount; i++) { // Loop only through the available relays
      if (relayIsArmed[i] && !motorInTimedRun[i]) {
        motorInTimedRun[i] = true;
        motorStartTimes[i] = millis();
        motorRunDurations[i] = mb.Hreg(HREG_DURATION_START_ADDR + i);

        digitalWrite(relayPins[i], HIGH);
        mb.Coil(COIL_MANUAL_START_ADDR + i, 1);
        relayIsArmed[i] = false;
      }
    }
    mb.Coil(COIL_GLOBAL_TRIGGER_ADDR, 0);
  }

  // --- 3. Manage Active Timed Runs ---
  for (int i = 0; i < relayCount; i++) { // Loop only through the available relays
    if (motorInTimedRun[i]) {
      if (millis() - motorStartTimes[i] >= motorRunDurations[i]) {
        digitalWrite(relayPins[i], LOW);
        mb.Coil(COIL_MANUAL_START_ADDR + i, 0);
        motorInTimedRun[i] = false;
      }
    }
    // --- 4. Handle Manual Control ---
    else {
      bool manualState = mb.Coil(COIL_MANUAL_START_ADDR + i);
      digitalWrite(relayPins[i], manualState);
    }
  }

  // --- 5. Update the Global "Any Relay On" Status Coil ---
  bool anyRelayActive = false;
  for (int i = 0; i < relayCount; i++) { // Loop only through the available relays
    if (digitalRead(relayPins[i]) == HIGH) {
      anyRelayActive = true;
      break;
    }
  }
  mb.Coil(COIL_ANY_RELAY_ON_ADDR, anyRelayActive);

#if defined(LED_PIN)
  digitalWrite(LED_PIN, anyRelayActive);
#endif

  delay(10);
}
