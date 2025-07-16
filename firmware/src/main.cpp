#include <Arduino.h>
#include <WiFi.h>
#include <ModbusIP_ESP8266.h>

#define LED_PIN 2

// --- WiFi Credentials ---
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";


// --- Device Information (Read-Only) ---
const uint16_t FIRMWARE_VERSION = 101; // Represents v1.0.1
const char* DEVICE_NAME = "ESP32 Relay Board";

// --- Modbus Object ---
ModbusIP mb;

// --- Relay & Motor Control Setup ---
const int relayCount = 8;
const int relayPins[relayCount] = {13, 12, 14, 27, 26, 25, 33, 32};

// --- State-holding variables for timed run logic ---
unsigned long motorStartTimes[relayCount] = {0};
uint32_t motorRunDurations[relayCount] = {0};
bool motorInTimedRun[relayCount] = {false};
bool relayIsArmed[relayCount] = {false};

// Modbus addresses
const int COIL_MANUAL_START_ADDR = 0;
const int HREG_DURATION_START_ADDR = 100;
const int COIL_ARM_RELAY_START_ADDR = 20;
const int COIL_GLOBAL_TRIGGER_ADDR = 30;
const int COIL_ANY_RELAY_ON_ADDR = 40;
const int COIL_EMERGENCY_STOP_ADDR = 60; // *** NEW: E-Stop Trigger Coil ***

// Device Information Registers
const int HREG_FIRMWARE_VERSION_ADDR = 500;
const int HREG_DEVICE_NAME_START_ADDR = 501;
const int HREG_DEVICE_NAME_LEN = 10;
const int HREG_SERIAL_NUMBER_START_ADDR = 511;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

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
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());


  // Setup Modbus Server
  mb.server();

  // --- Add Control & Status Registers ---
  for (int i = 0; i < relayCount; i++) {
    mb.addCoil(COIL_MANUAL_START_ADDR + i);
    mb.addHreg(HREG_DURATION_START_ADDR + i);
    mb.addCoil(COIL_ARM_RELAY_START_ADDR + i);
  }
  mb.addCoil(COIL_GLOBAL_TRIGGER_ADDR);
  mb.addCoil(COIL_ANY_RELAY_ON_ADDR);
  mb.addCoil(COIL_EMERGENCY_STOP_ADDR); // *** NEW: Add E-Stop coil ***

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
    for (int i = 0; i < relayCount; i++) {
        // Immediately turn off all physical relays
        digitalWrite(relayPins[i], LOW);
        // Reset all Modbus states and internal logic variables
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
  for (int i = 0; i < relayCount; i++) {
    if (mb.Coil(COIL_ARM_RELAY_START_ADDR + i)) {
      relayIsArmed[i] = true;
      mb.Coil(COIL_ARM_RELAY_START_ADDR + i, 0);
    }
  }

  // --- 2. Check for the Global Trigger Command ---
  if (mb.Coil(COIL_GLOBAL_TRIGGER_ADDR)) {
    for (int i = 0; i < relayCount; i++) {
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
  for (int i = 0; i < relayCount; i++) {
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
  for (int i = 0; i < relayCount; i++) {
    if (digitalRead(relayPins[i]) == HIGH) {
      anyRelayActive = true;
      break;
    }
  }
  mb.Coil(COIL_ANY_RELAY_ON_ADDR, anyRelayActive);
  digitalWrite(LED_PIN, anyRelayActive);

  delay(10);
}
