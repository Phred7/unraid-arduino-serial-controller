/*
 * ESP32-S2 Unraid Status Monitor - Enhanced Edition
 * 
 * Optimized for ESP32-S2 with plenty of RAM for JSON parsing
 * Compatible with S2 Mini boards
 * 
 * STATUS PATTERNS:
 * 🟣 PURPLE (breathing)    = Startup - waiting for first status message
 * 🟣 DARK PURPLE + WHITE  = All good! Server connected, array started (cool pattern)
 * 🔴 RED (breathing)      = Lost connection - waiting to reconnect
 * 🔴 RED (medium pulse)   = UPS on battery power (power outage)
 * 🔴 RED (fast blink)     = CRITICAL! UPS battery critically low
 * 🟡 YELLOW (pulse wave)  = Warning (high temps, disk issues) 
 * 🟣 PURPLE (pulse wave)  = System shutdown (unless UPS issues override)
 * 🔵 BLUE (solid)         = Server connected, array stopped  
 * ⚪ WHITE (flashing)     = Error / communication problem
 */

#include <FastLED.h>
#include <ArduinoJson.h>

// LED Strip Configuration for ESP32-S2
#define NUM_LEDS 100      // Initialize full strip length to prevent issues
#define ACTIVE_LEDS 100    // Only use first 15 LEDs for patterns
#define DATA_PIN 18       // Good pin for S2 Mini - check your board's pinout
#define LED_TYPE WS2811
#define COLOR_ORDER RBG
#define BRIGHTNESS 50

CRGB leds[NUM_LEDS];

// System States
enum SystemState {
  STARTUP,              // Initial state - purple breathing until first real message
  WAITING_CONNECTION,   // Lost connection - red breathing
  ALL_GOOD,
  ARRAY_STOPPED,
  WARNING,
  UPS_BATTERY,          // New state: UPS on battery (but not critical)
  CRITICAL,             // Reserved for truly critical situations (low battery)
  SHUTDOWN,             // System shutdown - purple pulse wave
  ERROR,
  COMMUNICATION_ERROR   // New state for comm timeouts
};

// Global variables
SystemState currentState = STARTUP;  // Start in startup mode
unsigned long lastMessageTime = 0;
unsigned long lastAnimationTime = 0;
String inputBuffer = "";
bool bufferComplete = false;
bool firstRealMessageReceived = false;  // Track if we've received a non-startup message
bool shutdownDueToUPS = false;          // Track if shutdown was caused by UPS issues

// Animation variables
int animationStep = 0;
bool animationDirection = true;
int breathingBrightness = 0;
bool flashState = false;
int wavePosition = 0;

// Thresholds and timeouts
const float HIGH_TEMP_THRESHOLD = 70.0;
const int LOW_BATTERY_THRESHOLD = 20;
const unsigned long COMMUNICATION_TIMEOUT = 65000; // 65 seconds (slightly more than Python's 60s)
const unsigned long HEARTBEAT_TIMEOUT = 35000;     // 35 seconds for heartbeat

// Communication health tracking
unsigned long lastHeartbeatTime = 0;
int parseErrorCount = 0;
bool communicationHealthy = true;

void setup() {
  // Initialize LED strip - set ALL 100 LEDs to off to prevent issues
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  fill_solid(leds, NUM_LEDS, CRGB::Black);  // Turn off all 100 LEDs
  FastLED.show();
  
  // Initialize serial
  Serial.begin(115200);
  inputBuffer.reserve(2048);  // Much larger buffer now!
  
  // Startup animation - cool purple sweep (only first 15 LEDs)
  for (int i = 0; i < ACTIVE_LEDS; i++) {
    leds[i] = CRGB(75, 0, 130); // Dark purple
    FastLED.show();
    delay(100);
  }
  delay(500);
  
  // Add white sparkles (only in active LED range)
  for (int sparkle = 0; sparkle < 5; sparkle++) {
    int pos = random(ACTIVE_LEDS);
    leds[pos] = CRGB::White;
    FastLED.show();
    delay(200);
    leds[pos] = CRGB(75, 0, 130);
    FastLED.show();
    delay(100);
  }
  
  // Clear all LEDs (including any beyond active range)
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  
  Serial.println(F("ESP32-S2 Unraid Monitor v2.0 - Enhanced Edition"));
  Serial.print(F("LED Strip: ")); Serial.print(NUM_LEDS); Serial.print(F(" initialized, "));
  Serial.print(ACTIVE_LEDS); Serial.println(F(" active"));
  Serial.print(F("Free heap: ")); Serial.println(ESP.getFreeHeap());
  Serial.print(F("Chip model: ")); Serial.println(ESP.getChipModel());
  Serial.print(F("CPU frequency: ")); Serial.print(ESP.getCpuFreqMHz()); Serial.println(F(" MHz"));
  Serial.println(F("Starting in STARTUP mode - purple breathing until first status message"));
  Serial.println(F("Ready for JSON data..."));
  Serial.println(F("Communication timeout set to 65 seconds"));
  
  lastMessageTime = millis();
  lastHeartbeatTime = millis();
}

void loop() {
  readSerialData();
  
  if (bufferComplete) {
    processMessage();
    bufferComplete = false;
    inputBuffer = "";
  }
  
  // Check communication health
  checkCommunicationHealth();
  
  updateLEDs();
  delay(5);  // ESP32 can handle faster updates
}

void checkCommunicationHealth() {
  unsigned long currentTime = millis();
  
  // Check for communication timeout
  if (currentTime - lastMessageTime > COMMUNICATION_TIMEOUT) {
    if (currentState != STARTUP && currentState != WAITING_CONNECTION && currentState != COMMUNICATION_ERROR) {
      Serial.println(F("Connection timeout - no data received for over 65 seconds"));
      currentState = COMMUNICATION_ERROR;
      communicationHealthy = false;
    } else if (currentState == STARTUP) {
      // If we timeout during startup, go to waiting connection (red breathing)
      Serial.println(F("Startup timeout - switching to waiting for connection"));
      currentState = WAITING_CONNECTION;
      communicationHealthy = false;
    }
  }
  
  // Check for heartbeat timeout (less severe)
  if (currentTime - lastHeartbeatTime > HEARTBEAT_TIMEOUT) {
    if (communicationHealthy) {
      Serial.println(F("Heartbeat timeout - no communication in 35 seconds"));
      communicationHealthy = false;
    }
  }
  
  // Check for excessive parse errors
  if (parseErrorCount > 3) {
    Serial.print(F("Too many parse errors: ")); Serial.println(parseErrorCount);
    currentState = COMMUNICATION_ERROR;
    communicationHealthy = false;
  }
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
        parseErrorCount++;
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
    parseErrorCount++;
    
    // Don't immediately go to error state for single parse errors
    if (parseErrorCount <= 3) {
      Serial.println(F("Parse error logged, continuing..."));
      return;
    } else {
      currentState = ERROR;
      return;
    }
  }
  
  // Successful parse - reset error count and update timestamps
  parseErrorCount = 0;
  lastMessageTime = millis();
  communicationHealthy = true;
  
  String messageType = doc["type"].as<String>();
  
  Serial.print(F("Message type: ")); Serial.println(messageType);
  
  // Handle different message types
  if (messageType == "status_update") {
    if (doc.containsKey("data")) {
      // This is a real status message - exit startup mode if needed
      if (currentState == STARTUP) {
        Serial.println(F("First status update received - exiting startup mode"));
        firstRealMessageReceived = true;
      }
      analyzeFullStatus(doc["data"]);
    }
  } else if (messageType == "system_startup") {
    Serial.println(F("=== Server Started ==="));
    if (doc.containsKey("data") && doc["data"]["version"]) {
      Serial.print(F("Version: ")); Serial.println(doc["data"]["version"].as<String>());
    }
    // Reset communication health on startup but stay in startup mode
    communicationHealthy = true;
    parseErrorCount = 0;
    firstRealMessageReceived = false;
    shutdownDueToUPS = false;
    // Don't change state here - wait for first real status message
  } else if (messageType == "system_shutdown") {
    Serial.println(F("=== Server Shutdown ==="));
    if (doc.containsKey("data") && doc["data"]["reason"]) {
      String reason = doc["data"]["reason"].as<String>();
      Serial.print(F("Shutdown reason: ")); Serial.println(reason);
      // Check if shutdown was due to UPS issues
      shutdownDueToUPS = (reason.indexOf("ups") >= 0 || reason.indexOf("battery") >= 0 || reason.indexOf("power") >= 0);
    }
    currentState = SHUTDOWN;
  } else if (messageType == "array_status_change") {
    Serial.println(F("=== Array Status Change ==="));
    if (doc.containsKey("data")) {
      String prev = doc["data"]["previous_status"] | "unknown";
      String curr = doc["data"]["current_status"] | "unknown";
      Serial.print(F("Changed: ")); Serial.print(prev); 
      Serial.print(F(" → ")); Serial.println(curr);
    }
    // This is a real message - exit startup if needed
    if (currentState == STARTUP) {
      Serial.println(F("Array status change received - exiting startup mode"));
      firstRealMessageReceived = true;
    }
  } else if (messageType == "heartbeat") {
    Serial.println(F("Heartbeat received"));
    lastHeartbeatTime = millis();
    // Heartbeat doesn't count as a "real" message for startup purposes
  } else if (messageType == "communication_error") {
    Serial.println(F("Server detected communication error"));
    currentState = COMMUNICATION_ERROR;
  } else if (messageType == "test_connection") {
    Serial.println(F("Test connection received"));
    // Test connection doesn't count as a "real" message for startup purposes
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
  
  // If we're in shutdown state, check if UPS issues should override
  if (currentState == SHUTDOWN) {
    // Check for UPS issues that should override shutdown display
    if (upsBattery > 0 && upsBattery < LOW_BATTERY_THRESHOLD) {
      Serial.println(F("🚨 CRITICAL: UPS battery critically low (overriding shutdown display)!"));
      currentState = CRITICAL;
      return;
    }
    if (!upsOnline && upsStatus != "UNAVAILABLE") {
      Serial.println(F("⚠️  UPS: Running on battery power (overriding shutdown display)"));
      currentState = UPS_BATTERY;
      return;
    }
    // If no UPS issues, stay in shutdown state
    Serial.println(F("ℹ️  System shutdown - showing purple pulse wave"));
    return;
  }
  
  // CRITICAL: UPS battery critically low (most urgent)
  if (upsBattery > 0 && upsBattery < LOW_BATTERY_THRESHOLD) {
    Serial.println(F("🚨 CRITICAL: UPS battery critically low!"));
    currentState = CRITICAL;
    return;
  }
  
  // UPS_BATTERY: UPS on battery but battery level still OK
  if (!upsOnline && upsStatus != "UNAVAILABLE") {
    Serial.println(F("⚠️  UPS: Running on battery power"));
    currentState = UPS_BATTERY;
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
    case STARTUP:
      if (currentTime - lastAnimationTime > 30) {
        purpleBreathingPattern();
        lastAnimationTime = currentTime;
      }
      break;
      
    case ALL_GOOD:
      if (currentTime - lastAnimationTime > 100) {
        coolPurplePattern();
        lastAnimationTime = currentTime;
      }
      break;
      
    case ARRAY_STOPPED:
      fill_solid(leds, ACTIVE_LEDS, CRGB::Blue);
      // Ensure LEDs beyond active range stay off
      if (NUM_LEDS > ACTIVE_LEDS) {
        fill_solid(&leds[ACTIVE_LEDS], NUM_LEDS - ACTIVE_LEDS, CRGB::Black);
      }
      break;
      
    case WARNING:
      if (currentTime - lastAnimationTime > 80) {
        warningPulseWave();
        lastAnimationTime = currentTime;
      }
      break;
      
    case UPS_BATTERY:
      if (currentTime - lastAnimationTime > 60) {  // Slower pulse than breathing
        upsBatteryPattern();
        lastAnimationTime = currentTime;
      }
      break;
      
    case CRITICAL:
      if (currentTime - lastAnimationTime > 150) {  // Fast blink for critical battery
        flashState = !flashState;
        fill_solid(leds, ACTIVE_LEDS, flashState ? CRGB::Red : CRGB::Black);
        // Ensure LEDs beyond active range stay off
        if (NUM_LEDS > ACTIVE_LEDS) {
          fill_solid(&leds[ACTIVE_LEDS], NUM_LEDS - ACTIVE_LEDS, CRGB::Black);
        }
        lastAnimationTime = currentTime;
      }
      break;
      
    case SHUTDOWN:
      if (currentTime - lastAnimationTime > 80) {
        purplePulseWave();
        lastAnimationTime = currentTime;
      }
      break;
      
    case WAITING_CONNECTION:
      if (currentTime - lastAnimationTime > 30) {
        redBreathingPattern();
        lastAnimationTime = currentTime;
      }
      break;
    
    case COMMUNICATION_ERROR:
      if (currentTime - lastAnimationTime > 100) {
        communicationErrorPattern();
        lastAnimationTime = currentTime;
      }
      break;
      
    case ERROR:
      if (currentTime - lastAnimationTime > 250) {
        flashState = !flashState;
        fill_solid(leds, ACTIVE_LEDS, flashState ? CRGB::White : CRGB::Black);
        // Ensure LEDs beyond active range stay off
        if (NUM_LEDS > ACTIVE_LEDS) {
          fill_solid(&leds[ACTIVE_LEDS], NUM_LEDS - ACTIVE_LEDS, CRGB::Black);
        }
        lastAnimationTime = currentTime;
      }
      break;
  }
  
  FastLED.show();
}

void coolPurplePattern() {
  // Dark purple base with moving white accents (only first 15 LEDs)
  static int whitePosition = 0;
  static int direction = 1;
  static int accentBrightness = 255;
  static bool accentDirection = false;
  
  // Fill active LEDs with dark purple
  fill_solid(leds, ACTIVE_LEDS, CRGB(75, 0, 130)); // Dark purple
  
  // Add moving white accent
  leds[whitePosition] = CRGB(accentBrightness, accentBrightness, accentBrightness);
  
  // Add trailing accent
  if (whitePosition + direction >= 0 && whitePosition + direction < ACTIVE_LEDS) {
    leds[whitePosition + direction] = CRGB(accentBrightness / 3, accentBrightness / 3, accentBrightness / 3);
  }
  
  // Move the accent
  whitePosition += direction;
  if (whitePosition >= ACTIVE_LEDS - 1 || whitePosition <= 0) {
    direction *= -1;
  }
  
  // Pulse the accent brightness
  if (accentDirection) {
    accentBrightness += 8;
    if (accentBrightness >= 255) {
      accentBrightness = 255;
      accentDirection = false;
    }
  } else {
    accentBrightness -= 8;
    if (accentBrightness <= 100) {
      accentBrightness = 100;
      accentDirection = true;
    }
  }
  
  // Ensure LEDs beyond active range stay off
  if (NUM_LEDS > ACTIVE_LEDS) {
    fill_solid(&leds[ACTIVE_LEDS], NUM_LEDS - ACTIVE_LEDS, CRGB::Black);
  }
}

void redBreathingPattern() {
  // Smooth breathing red color for waiting connection (only first 15 LEDs)
  static bool breathingUp = true;
  
  if (breathingUp) {
    breathingBrightness += 3;
    if (breathingBrightness >= 255) {
      breathingBrightness = 255;
      breathingUp = false;
    }
  } else {
    breathingBrightness -= 3;
    if (breathingBrightness <= 30) {
      breathingBrightness = 30;
      breathingUp = true;
    }
  }
  
  CRGB redColor = CRGB(breathingBrightness, 0, 0);
  fill_solid(leds, ACTIVE_LEDS, redColor);
  
  // Ensure LEDs beyond active range stay off
  if (NUM_LEDS > ACTIVE_LEDS) {
    fill_solid(&leds[ACTIVE_LEDS], NUM_LEDS - ACTIVE_LEDS, CRGB::Black);
  }
}

void warningPulseWave() {
  // Cool warning pattern - yellow pulse wave (only first 15 LEDs)
  static int waveCenter = 0;
  static int direction = 1;
  
  // Clear active LEDs
  fill_solid(leds, ACTIVE_LEDS, CRGB::Black);
  
  // Create pulse wave effect
  for (int i = 0; i < ACTIVE_LEDS; i++) {
    int distance = abs(i - waveCenter);
    int brightness = max(0, 255 - (distance * 60));
    
    if (brightness > 0) {
      leds[i] = CRGB(brightness, brightness, 0); // Yellow
    }
  }
  
  // Move wave center
  waveCenter += direction;
  if (waveCenter >= ACTIVE_LEDS - 1 || waveCenter <= 0) {
    direction *= -1;
  }
  
  // Ensure LEDs beyond active range stay off
  if (NUM_LEDS > ACTIVE_LEDS) {
    fill_solid(&leds[ACTIVE_LEDS], NUM_LEDS - ACTIVE_LEDS, CRGB::Black);
  }
}

void purpleBreathingPattern() {
  // Smooth breathing purple color for startup
  static bool breathingUp = true;
  
  if (breathingUp) {
    breathingBrightness += 3;
    if (breathingBrightness >= 200) {  // Don't go to full brightness
      breathingBrightness = 200;
      breathingUp = false;
    }
  } else {
    breathingBrightness -= 3;
    if (breathingBrightness <= 30) {
      breathingBrightness = 30;
      breathingUp = true;
    }
  }
  
  // Calculate purple color (R:75, G:0, B:130 scaled by brightness)
  int r = (75 * breathingBrightness) / 200;
  int g = 0;
  int b = (130 * breathingBrightness) / 200;
  
  CRGB purpleColor = CRGB(r, g, b);
  fill_solid(leds, ACTIVE_LEDS, purpleColor);
  
  // Ensure LEDs beyond active range stay off
  if (NUM_LEDS > ACTIVE_LEDS) {
    fill_solid(&leds[ACTIVE_LEDS], NUM_LEDS - ACTIVE_LEDS, CRGB::Black);
  }
}

void purplePulseWave() {
  // Purple pulse wave pattern for shutdown
  static int waveCenter = 0;
  static int direction = 1;
  
  // Clear active LEDs
  fill_solid(leds, ACTIVE_LEDS, CRGB::Black);
  
  // Create pulse wave effect with purple
  for (int i = 0; i < ACTIVE_LEDS; i++) {
    int distance = abs(i - waveCenter);
    int brightness = max(0, 255 - (distance * 60));
    
    if (brightness > 0) {
      // Purple color scaled by brightness
      int r = (75 * brightness) / 255;
      int g = 0;
      int b = (130 * brightness) / 255;
      leds[i] = CRGB(r, g, b);
    }
  }
  
  // Move wave center
  waveCenter += direction;
  if (waveCenter >= ACTIVE_LEDS - 1 || waveCenter <= 0) {
    direction *= -1;
  }
  
  // Ensure LEDs beyond active range stay off
  if (NUM_LEDS > ACTIVE_LEDS) {
    fill_solid(&leds[ACTIVE_LEDS], NUM_LEDS - ACTIVE_LEDS, CRGB::Black);
  }
}

void upsBatteryPattern() {
  // UPS on battery pattern - medium-speed red pulse (distinct from breathing and blinking)
  static bool pulseUp = true;
  static int pulseBrightness = 80; // Start at medium brightness
  
  if (pulseUp) {
    pulseBrightness += 12;  // Faster than breathing (3), slower than instant blink
    if (pulseBrightness >= 255) {
      pulseBrightness = 255;
      pulseUp = false;
    }
  } else {
    pulseBrightness -= 12;
    if (pulseBrightness <= 80) {  // Don't go as dim as breathing pattern
      pulseBrightness = 80;
      pulseUp = true;
    }
  }
  
  CRGB redColor = CRGB(pulseBrightness, 0, 0);
  fill_solid(leds, ACTIVE_LEDS, redColor);
  
  // Ensure LEDs beyond active range stay off
  if (NUM_LEDS > ACTIVE_LEDS) {
    fill_solid(&leds[ACTIVE_LEDS], NUM_LEDS - ACTIVE_LEDS, CRGB::Black);
  }
}

void communicationErrorPattern() {
  // Distinctive pattern for communication errors - alternating red/white (only first 15 LEDs)
  static bool toggle = false;
  static int step = 0;
  
  for (int i = 0; i < ACTIVE_LEDS; i++) {
    if ((i + step) % 3 == 0) {
      leds[i] = toggle ? CRGB::Red : CRGB::White;
    } else {
      leds[i] = CRGB::Black;
    }
  }
  
  step = (step + 1) % 3;
  toggle = !toggle;
  
  // Ensure LEDs beyond active range stay off
  if (NUM_LEDS > ACTIVE_LEDS) {
    fill_solid(&leds[ACTIVE_LEDS], NUM_LEDS - ACTIVE_LEDS, CRGB::Black);  
  }
}