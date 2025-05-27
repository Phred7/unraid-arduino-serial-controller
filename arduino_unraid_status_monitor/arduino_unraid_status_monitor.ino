/*
 * ESP32-S2 Unraid Status Monitor
 * 
 * Optimized for ESP32-S2 with plenty of RAM for JSON parsing
 * Compatible with S2 Mini boards
 * 
 * STATUS COLORS:
 * 🟢 GREEN (solid)     = All good! Server connected, array started
 * 🔵 BLUE (solid)      = Server connected, array stopped  
 * 🟡 YELLOW (solid)    = Warning (high temps, disk issues)
 * 🔴 RED (solid)       = Critical! UPS on battery
 * 🌈 RAINBOW (moving)  = Waiting for server connection
 * ⚪ WHITE (flashing)  = Error / communication problem
 */

#include <FastLED.h>
#include <ArduinoJson.h>

// LED Strip Configuration for ESP32-S2
#define NUM_LEDS 15
#define DATA_PIN 18  // Good pin for S2 Mini - check your board's pinout
#define LED_TYPE WS2811
#define COLOR_ORDER RBG
#define BRIGHTNESS 100

CRGB leds[NUM_LEDS];

// System States
enum SystemState {
  WAITING_CONNECTION,
  ALL_GOOD,
  ARRAY_STOPPED,
  WARNING,
  CRITICAL,
  ERROR
};

// Global variables
SystemState currentState = WAITING_CONNECTION;
unsigned long lastMessageTime = 0;
unsigned long lastAnimationTime = 0;
String inputBuffer = "";
bool bufferComplete = false;

// Animation variables
int rainbowOffset = 0;
bool flashState = false;

// Thresholds
const float HIGH_TEMP_THRESHOLD = 70.0;
const int LOW_BATTERY_THRESHOLD = 20;

void setup() {
  // Initialize LED strip
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  
  // Initialize serial
  Serial.begin(115200);
  inputBuffer.reserve(2048);  // Much larger buffer now!
  
  // Startup animation - full rainbow sweep
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV(i * 255 / NUM_LEDS, 255, 255);
    FastLED.show();
    delay(10);
  }
  delay(500);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  
  Serial.println(F("ESP32-S2 Unraid Monitor v1.0"));
  Serial.print(F("Free heap: ")); Serial.println(ESP.getFreeHeap());
  Serial.print(F("Chip model: ")); Serial.println(ESP.getChipModel());
  Serial.print(F("CPU frequency: ")); Serial.print(ESP.getCpuFreqMHz()); Serial.println(F(" MHz"));
  Serial.println(F("Ready for JSON data..."));
  
  lastMessageTime = millis();
}

void loop() {
  readSerialData();
  
  if (bufferComplete) {
    processMessage();
    bufferComplete = false;
    inputBuffer = "";
  }
  
  // Connection timeout check (60 seconds)
  if (millis() - lastMessageTime > 60000 && currentState != WAITING_CONNECTION) {
    Serial.println(F("Connection timeout - waiting for server"));
    currentState = WAITING_CONNECTION;
  }
  
  updateLEDs();
  delay(5);  // ESP32 can handle faster updates
}

void readSerialData() {
  while (Serial.available() && !bufferComplete) {
    char inChar = (char)Serial.read();
    
    if (inChar == '\n') {
      if (inputBuffer.length() > 0) {
        bufferComplete = true;
      }
    } else if (inChar != '\r') {
      inputBuffer += inChar;
      
      // Much higher limit now - ESP32 has tons of RAM
      if (inputBuffer.length() > 1500) {
        Serial.println(F("Buffer overflow (very unusual on ESP32)"));
        inputBuffer = "";
        currentState = ERROR;
        break;
      }
    }
  }
}

void processMessage() {
  Serial.print(F("Received: ")); Serial.println(inputBuffer.substring(0, 100));
  Serial.print(F("Free heap: ")); Serial.println(ESP.getFreeHeap());
  
  // Use much larger JSON buffer - ESP32 can handle it easily
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, inputBuffer);
  
  if (error) {
    Serial.print(F("JSON Error: "));
    Serial.println(error.c_str());
    Serial.print(F("Message length: ")); Serial.println(inputBuffer.length());
    currentState = ERROR;
    return;
  }
  
  lastMessageTime = millis();
  String messageType = doc["type"].as<String>();
  
  Serial.print(F("Message type: ")); Serial.println(messageType);
  
  // Handle different message types
  if (messageType == "status_update") {
    if (doc.containsKey("data")) {
      analyzeFullStatus(doc["data"]);
    }
  } else if (messageType == "system_startup") {
    Serial.println(F("=== Server Started ==="));
    if (doc.containsKey("data") && doc["data"]["version"]) {
      Serial.print(F("Version: ")); Serial.println(doc["data"]["version"].as<String>());
    }
  } else if (messageType == "system_shutdown") {
    Serial.println(F("=== Server Shutdown ==="));
    currentState = WAITING_CONNECTION;
  } else if (messageType == "array_status_change") {
    Serial.println(F("=== Array Status Change ==="));
    if (doc.containsKey("data")) {
      String prev = doc["data"]["previous_status"] | "unknown";
      String curr = doc["data"]["current_status"] | "unknown";
      Serial.print(F("Changed: ")); Serial.print(prev); 
      Serial.print(F(" → ")); Serial.println(curr);
    }
  } else if (messageType == "test_connection") {
    Serial.println(F("Test connection received"));
  } else {
    Serial.print(F("Unknown message type: ")); Serial.println(messageType);
  }
}

void analyzeFullStatus(JsonObject data) {
  // Extract comprehensive status - we can handle the full data now!
  String arrayStatus = data["as"] | "unknown";
  float cpuTemp = data["ct"] | 0.0;
  int uptime = data["up"] | 0;
  float diskTemp = data["d_temp"] | 0.0;
  int diskCapacity = data["d_cap"] | 0;
  String diskHealth = data["d_health"] | "UNKNOWN";
  int diskCount = data["d_count"] | 0;
  float nvmeTemp = data["n_temp"] | 0.0;
  int nvmeCapacity = data["n_cap"] | 0;
  String nvmeHealth = data["n_health"] | "UNKNOWN";
  int nvmeCount = data["n_count"] | 0;
  bool upsOnline = data["ups_online"] | true;
  int upsBattery = data["ups_batt"] | 100;
  int upsLoad = data["ups_load"] | 0;
  int upsRuntime = data["ups_runtime"] | 0;
  String upsStatus = data["ups_status"] | "UNAVAILABLE";
  
  // Print comprehensive status
  Serial.println(F("=== Full System Status ==="));
  Serial.print(F("Array: ")); Serial.println(arrayStatus);
  Serial.print(F("Uptime: ")); Serial.print(uptime); Serial.println(F("s"));
  Serial.print(F("CPU Temp: ")); Serial.print(cpuTemp); Serial.println(F("°C"));
  
  Serial.print(F("Disks: ")); Serial.print(diskCount);
  Serial.print(F(" (Temp: ")); Serial.print(diskTemp);
  Serial.print(F("°C, Health: ")); Serial.print(diskHealth);
  Serial.print(F(", Total: ")); Serial.print(diskCapacity); Serial.println(F("GB)"));
  
  Serial.print(F("NVMe: ")); Serial.print(nvmeCount);
  Serial.print(F(" (Temp: ")); Serial.print(nvmeTemp);
  Serial.print(F("°C, Health: ")); Serial.print(nvmeHealth);
  Serial.print(F(", Total: ")); Serial.print(nvmeCapacity); Serial.println(F("GB)"));
  
  Serial.print(F("UPS: ")); Serial.print(upsOnline ? "Online" : "Battery");
  Serial.print(F(" (Charge: ")); Serial.print(upsBattery);
  Serial.print(F("%, Load: ")); Serial.print(upsLoad);
  Serial.print(F("%, Runtime: ")); Serial.print(upsRuntime);
  Serial.print(F("min, Status: ")); Serial.print(upsStatus); Serial.println(F(")"));
  Serial.println();
  
  // Determine state with priority
  
  // CRITICAL: UPS issues
  if (!upsOnline && upsStatus != "UNAVAILABLE") {
    Serial.println(F("🚨 CRITICAL: UPS on battery power!"));
    currentState = CRITICAL;
    return;
  }
  
  if (upsBattery > 0 && upsBattery < LOW_BATTERY_THRESHOLD) {
    Serial.println(F("🚨 CRITICAL: UPS battery critically low!"));
    currentState = CRITICAL;
    return;
  }
  
  // WARNING: Temperature or health issues
  float maxTemp = max(max(cpuTemp, diskTemp), nvmeTemp);
  bool healthIssues = (diskHealth != "PASSED" && diskHealth != "OK" && diskHealth != "UNKNOWN") ||
                      (nvmeHealth != "PASSED" && nvmeHealth != "OK" && nvmeHealth != "UNKNOWN");
  
  if (maxTemp > HIGH_TEMP_THRESHOLD) {
    Serial.print(F("⚠️  WARNING: High temperature detected: "));
    Serial.print(maxTemp); Serial.println(F("°C"));
    currentState = WARNING;
    return;
  }
  
  if (healthIssues) {
    Serial.println(F("⚠️  WARNING: Disk health issues detected"));
    Serial.print(F("Disk health: ")); Serial.println(diskHealth);
    Serial.print(F("NVMe health: ")); Serial.println(nvmeHealth);
    currentState = WARNING;
    return;
  }
  
  // Normal states based on array status
  if (arrayStatus == "started") {
    Serial.println(F("✅ Status: All systems normal"));
    currentState = ALL_GOOD;
  } else if (arrayStatus == "stopped") {
    Serial.println(F("ℹ️  Status: Array stopped"));
    currentState = ARRAY_STOPPED;
  } else {
    Serial.println(F("❓ Status: Array status unknown"));
    currentState = WARNING;
  }
}

void updateLEDs() {
  unsigned long currentTime = millis();
  
  switch (currentState) {
    case ALL_GOOD:
      fill_solid(leds, NUM_LEDS, CRGB::Green);
      break;
      
    case ARRAY_STOPPED:
      fill_solid(leds, NUM_LEDS, CRGB::Blue);
      break;
      
    case WARNING:
      fill_solid(leds, NUM_LEDS, CRGB::Yellow);
      break;
      
    case CRITICAL:
      fill_solid(leds, NUM_LEDS, CRGB::Red);
      break;
      
    case WAITING_CONNECTION:
      if (currentTime - lastAnimationTime > 50) {  // Smooth animation
        rainbowAnimation();
        lastAnimationTime = currentTime;
      }
      break;
      
    case ERROR:
      if (currentTime - lastAnimationTime > 250) {
        flashState = !flashState;
        fill_solid(leds, NUM_LEDS, flashState ? CRGB::White : CRGB::Black);
        lastAnimationTime = currentTime;
      }
      break;
  }
  
  FastLED.show();
}

void rainbowAnimation() {
  // Smooth rainbow wave
  for (int i = 0; i < NUM_LEDS; i++) {
    int hue = (i + rainbowOffset) * 255 / NUM_LEDS;
    leds[i] = CHSV(hue, 255, 255);
  }
  rainbowOffset = (rainbowOffset + 2) % 255;
}