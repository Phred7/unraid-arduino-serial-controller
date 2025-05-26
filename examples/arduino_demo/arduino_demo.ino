/*
 * Arduino Serial Controller Example
 * 
 * This example demonstrates how to receive and process messages
 * from the Unraid Arduino Serial Controller plugin.
 * 
 * Required Libraries:
 * - ArduinoJson (install via Library Manager)
 * 
 * Wiring:
 * - Connect Arduino to Unraid server via USB
 * - Optional: Connect LEDs, LCD, or other indicators
 * 
 * Author: phredd7
 * Version: 1.0
 */

#include <ArduinoJson.h>

// Configuration
const int BAUD_RATE = 9600;
const int STATUS_LED_PIN = 13;        // Built-in LED
const int ARRAY_STATUS_LED_PIN = 12;  // External LED for array status
const int SHUTDOWN_LED_PIN = 11;      // External LED for shutdown indication

// Variables
bool systemOnline = false;
bool arrayStarted = false;
float lastCpuTemp = 0.0;
unsigned long lastStatusUpdate = 0;
String lastArrayStatus = "unknown";

void setup() {
  // Initialize serial communication
  Serial.begin(BAUD_RATE);
  
  // Initialize pins
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(ARRAY_STATUS_LED_PIN, OUTPUT);
  pinMode(SHUTDOWN_LED_PIN, OUTPUT);
  
  // Initial LED state - all off
  digitalWrite(STATUS_LED_PIN, LOW);
  digitalWrite(ARRAY_STATUS_LED_PIN, LOW);
  digitalWrite(SHUTDOWN_LED_PIN, LOW);
  
  // Startup sequence - flash all LEDs
  for (int i = 0; i < 3; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    digitalWrite(ARRAY_STATUS_LED_PIN, HIGH);
    digitalWrite(SHUTDOWN_LED_PIN, HIGH);
    delay(200);
    digitalWrite(STATUS_LED_PIN, LOW);
    digitalWrite(ARRAY_STATUS_LED_PIN, LOW);
    digitalWrite(SHUTDOWN_LED_PIN, LOW);
    delay(200);
  }
  
  Serial.println("Arduino Serial Controller Ready");
  Serial.println("Waiting for Unraid connection...");
}

void loop() {
  // Check for incoming serial data
  if (Serial.available()) {
    String message = Serial.readStringUntil('\n');
    message.trim();
    
    if (message.length() > 0) {
      processMessage(message);
    }
  }
  
  // Heartbeat - blink status LED when system is online
  if (systemOnline) {
    unsigned long currentTime = millis();
    if (currentTime - lastStatusUpdate < 60000) { // Within last minute
      // Fast blink - system active
      digitalWrite(STATUS_LED_PIN, (currentTime / 500) % 2);
    } else {
      // Slow blink - no recent updates
      digitalWrite(STATUS_LED_PIN, (currentTime / 2000) % 2);
    }
  } else {
    // System offline - LED off
    digitalWrite(STATUS_LED_PIN, LOW);
  }
  
  delay(100);
}

void processMessage(String jsonMessage) {
  // Parse JSON message
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, jsonMessage);
  
  if (error) {
    Serial.print("JSON parsing error: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Extract common fields
  String messageType = doc["type"];
  String timestamp = doc["timestamp"];
  
  Serial.print("[");
  Serial.print(timestamp);
  Serial.print("] ");
  Serial.print("Received: ");
  Serial.println(messageType);
  
  // Handle different message types
  if (messageType == "system_startup") {
    handleSystemStartup(doc);
    
  } else if (messageType == "status_update") {
    handleStatusUpdate(doc);
    
  } else if (messageType == "system_shutdown") {
    handleSystemShutdown(doc);
    
  } else if (messageType == "array_status_change") {
    handleArrayStatusChange(doc);
    
  } else {
    Serial.print("Unknown message type: ");
    Serial.println(messageType);
  }
}

void handleSystemStartup(DynamicJsonDocument& doc) {
  String version = doc["data"]["version"];
  
  Serial.println("=== SYSTEM STARTUP ===");
  Serial.print("Plugin version: ");
  Serial.println(version);
  
  systemOnline = true;
  lastStatusUpdate = millis();
  
  // Flash status LED rapidly 5 times
  for (int i = 0; i < 5; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(100);
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(100);
  }
}

void handleStatusUpdate(DynamicJsonDocument& doc) {
  // Extract status data
  float cpuTemp = doc["data"]["cpu_temp"];
  int uptime = doc["data"]["uptime"];
  String arrayStatus = doc["data"]["array_status"];
  
  lastCpuTemp = cpuTemp;
  lastStatusUpdate = millis();
  
  // Update array status LED
  if (arrayStatus == "started") {
    digitalWrite(ARRAY_STATUS_LED_PIN, HIGH);
    arrayStarted = true;
  } else {
    digitalWrite(ARRAY_STATUS_LED_PIN, LOW);
    arrayStarted = false;
  }
  
  // Update last known array status
  if (arrayStatus != lastArrayStatus) {
    lastArrayStatus = arrayStatus;
    Serial.print("Array status: ");
    Serial.println(arrayStatus);
  }
  
  // Print status information
  Serial.print("CPU: ");
  Serial.print(cpuTemp, 1);
  Serial.print("°C, Uptime: ");
  Serial.print(uptime / 3600); // Convert to hours
  Serial.print("h, Array: ");
  Serial.println(arrayStatus);
  
  // Temperature warning (example threshold)
  if (cpuTemp > 70.0) {
    Serial.println("WARNING: High CPU temperature!");
    // Flash all LEDs as warning
    for (int i = 0; i < 3; i++) {
      digitalWrite(STATUS_LED_PIN, HIGH);
      digitalWrite(ARRAY_STATUS_LED_PIN, HIGH);
      delay(150);
      digitalWrite(STATUS_LED_PIN, LOW);
      digitalWrite(ARRAY_STATUS_LED_PIN, LOW);
      delay(150);
    }
  }
}

void handleSystemShutdown(DynamicJsonDocument& doc) {
  String reason = doc["data"]["reason"];
  
  Serial.println("=== SYSTEM SHUTDOWN ===");
  Serial.print("Reason: ");
  Serial.println(reason);
  
  systemOnline = false;
  
  // Turn on shutdown LED
  digitalWrite(SHUTDOWN_LED_PIN, HIGH);
  
  // Turn off other LEDs
  digitalWrite(STATUS_LED_PIN, LOW);
  digitalWrite(ARRAY_STATUS_LED_PIN, LOW);
  
  // Flash shutdown LED pattern
  for (int i = 0; i < 10; i++) {
    digitalWrite(SHUTDOWN_LED_PIN, HIGH);
    delay(200);
    digitalWrite(SHUTDOWN_LED_PIN, LOW);
    delay(200);
  }
  
  // Keep shutdown LED on
  digitalWrite(SHUTDOWN_LED_PIN, HIGH);
  
  Serial.println("System shutdown acknowledged");
}

void handleArrayStatusChange(DynamicJsonDocument& doc) {
  String previousStatus = doc["data"]["previous_status"];
  String currentStatus = doc["data"]["current_status"];
  
  Serial.println("=== ARRAY STATUS CHANGE ===");
  Serial.print("Status changed: ");
  Serial.print(previousStatus);
  Serial.print(" -> ");
  Serial.println(currentStatus);
  
  lastArrayStatus = currentStatus;
  
  if (currentStatus == "started") {
    digitalWrite(ARRAY_STATUS_LED_PIN, HIGH);
    arrayStarted = true;
    
    // Flash array LED rapidly to indicate start
    for (int i = 0; i < 5; i++) {
      digitalWrite(ARRAY_STATUS_LED_PIN, LOW);
      delay(100);
      digitalWrite(ARRAY_STATUS_LED_PIN, HIGH);
      delay(100);
    }
    
    Serial.println("Array started - ready for operations");
    
  } else if (currentStatus == "stopped") {
    digitalWrite(ARRAY_STATUS_LED_PIN, LOW);
    arrayStarted = false;
    
    Serial.println("Array stopped - data protection mode");
  }
}

// Optional: Add custom functions for additional features
void displaySystemInfo() {
  Serial.println("\n=== SYSTEM INFO ===");
  Serial.print("System Online: ");
  Serial.println(systemOnline ? "Yes" : "No");
  Serial.print("Array Started: ");
  Serial.println(arrayStarted ? "Yes" : "No");
  Serial.print("Last CPU Temp: ");
  Serial.print(lastCpuTemp, 1);
  Serial.println("°C");
  Serial.print("Last Array Status: ");
  Serial.println(lastArrayStatus);
  Serial.print("Time since last update: ");
  Serial.print((millis() - lastStatusUpdate) / 1000);
  Serial.println(" seconds");
  Serial.println("==================\n");
}

// Optional: Handle serial commands for testing
void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();
    
    if (command == "info") {
      displaySystemInfo();
    } else if (command == "test") {
      Serial.println("Testing LEDs...");
      testLEDs();
    } else if (command == "reset") {
      Serial.println("Resetting status...");
      systemOnline = false;
      arrayStarted = false;
      lastCpuTemp = 0.0;
      lastStatusUpdate = 0;
      lastArrayStatus = "unknown";
      digitalWrite(STATUS_LED_PIN, LOW);
      digitalWrite(ARRAY_STATUS_LED_PIN, LOW);
      digitalWrite(SHUTDOWN_LED_PIN, LOW);
    }
  }
}

void testLEDs() {
  // Test sequence for all LEDs
  Serial.println("Testing Status LED...");
  for (int i = 0; i < 3; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(300);
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(300);
  }
  
  Serial.println("Testing Array LED...");
  for (int i = 0; i < 3; i++) {
    digitalWrite(ARRAY_STATUS_LED_PIN, HIGH);
    delay(300);
    digitalWrite(ARRAY_STATUS_LED_PIN, LOW);
    delay(300);
  }
  
  Serial.println("Testing Shutdown LED...");
  for (int i = 0; i < 3; i++) {
    digitalWrite(SHUTDOWN_LED_PIN, HIGH);
    delay(300);
    digitalWrite(SHUTDOWN_LED_PIN, LOW);
    delay(300);
  }
  
  Serial.println("LED test complete!");
}