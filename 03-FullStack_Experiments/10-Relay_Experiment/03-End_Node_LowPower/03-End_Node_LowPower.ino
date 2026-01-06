/*
  LoRa End Node with Deep Sleep Power Management
  
  Power Optimization Strategy:
  - End node transmits sensor data periodically (configurable interval)
  - Enters ESP32 deep sleep between transmissions
  - Wakes up, sends data, waits for ACK, then sleeps again
  
  Current consumption:
  - Active (TX + RX ACK): ~120 mA for 1-3 seconds
  - Deep sleep: ~0.01 mA (10 µA)
  - Average (5 min interval): ~0.5 mA → months of battery life
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <LoRa.h>
#include <math.h>

// ===================== NODE CONFIG =============================
#define MY_NODE_ID   "C"       // End node ID
#define PEER_NODE_ID "A"       // Gateway/destination node ID

// ===================== SLEEP CONFIG ============================
#define SLEEP_INTERVAL_SECONDS  300       // 5 minutes between transmissions
#define WAKEUP_TIMEOUT_MS      5000       // Max time awake waiting for ACK
#define uS_TO_S_FACTOR         1000000ULL // Microseconds to seconds conversion

// Boot counter stored in RTC memory (survives deep sleep)
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR uint32_t successfulTX = 0;
RTC_DATA_ATTR uint32_t failedTX = 0;

// ===================== RADIO CONFIG ============================
#define FREQ_HZ   923E6
#define LORA_SYNC 0xA5
#define LORA_SF   8
const size_t LORA_MAX_PAYLOAD = 255;
const size_t HOP_HEADER_MAX = 6;
const size_t LORA_MAX_PAYLOAD_EFFECTIVE = LORA_MAX_PAYLOAD - HOP_HEADER_MAX;

// Wiring (LilyGo T-Display)
#define SCK   5
#define MISO  19
#define MOSI  27
#define SS    18
#define RST   14
#define DIO0  26

// ===================== OLED CONFIG =============================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

static void oled3(const String& a, const String& b="", const String& c="") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println(a);
  if(b.length()) display.println(b);
  if(c.length()) display.println(c);
  display.display();
}

// ===================== SENSOR SIMULATION =======================
struct SensorData {
  float temperature;
  float humidity;
  uint16_t batteryMv;
  int rssiLast;
};

SensorData readSensors() {
  SensorData data;
  // Simulate sensor readings (replace with actual sensor code)
  data.temperature = 25.0 + random(-50, 50) / 10.0;  // 20-30°C
  data.humidity = 60.0 + random(-100, 100) / 10.0;   // 50-70%
  data.batteryMv = 3700 + random(-300, 300);         // 3.4-4.0V
  data.rssiLast = 0;
  return data;
}

// ===================== HELPERS =================================
static void sendLoRaRawWrapped(const String& payload, int hop = 0) {
  if(hop < 0) hop = 0;
  String wrap = "H" + String(hop) + ":" + payload;
  LoRa.beginPacket();
  LoRa.print(wrap);
  LoRa.endPacket();
}

static bool waitForACK(uint32_t timeoutMs) {
  unsigned long startMs = millis();
  while (millis() - startMs < timeoutMs) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String rawPkt;
      while (LoRa.available()) rawPkt += (char)LoRa.read();
      
      // Check if it's an ACK for us
      if (rawPkt.indexOf("ACK,") >= 0 || rawPkt.indexOf("ACKF,") >= 0) {
        Serial.println("[LOW_POWER] ACK received!");
        return true;
      }
    }
    delay(10);  // Small delay to prevent busy-waiting
  }
  return false;
}

// ===================== MAIN FUNCTIONS ==========================

void transmitSensorData() {
  unsigned long txStartMs = millis();
  
  // Read sensors
  SensorData data = readSensors();
  
  // Build message payload
  String payload = "MSG,";
  payload += MY_NODE_ID;
  payload += ",";
  payload += String(bootCount);
  payload += ",T=";
  payload += String(data.temperature, 1);
  payload += ",H=";
  payload += String(data.humidity, 1);
  payload += ",B=";
  payload += String(data.batteryMv);
  payload += "mV,OK=";
  payload += String(successfulTX);
  payload += ",FAIL=";
  payload += String(failedTX);
  
  Serial.println("\n[LOW_POWER] ========== Wake Up ==========");
  Serial.print("[LOW_POWER] Boot count: ");
  Serial.println(bootCount);
  Serial.print("[LOW_POWER] Payload: ");
  Serial.println(payload);
  
  // Update display
  oled3("End Node LowPower",
        "Boot: " + String(bootCount),
        "TX: " + payload.substring(0, 18));
  
  // Transmit with hop header
  sendLoRaRawWrapped(payload, 0);
  Serial.println("[LOW_POWER] Packet transmitted");
  
  // Wait for ACK
  Serial.println("[LOW_POWER] Waiting for ACK...");
  bool ackReceived = waitForACK(WAKEUP_TIMEOUT_MS);
  
  unsigned long txDurationMs = millis() - txStartMs;
  
  if (ackReceived) {
    successfulTX++;
    Serial.print("[LOW_POWER] SUCCESS! Duration: ");
    Serial.print(txDurationMs);
    Serial.println(" ms");
    oled3("TX SUCCESS", 
          "ACK received",
          "Duration: " + String(txDurationMs) + "ms");
  } else {
    failedTX++;
    Serial.print("[LOW_POWER] TIMEOUT! No ACK. Duration: ");
    Serial.print(txDurationMs);
    Serial.println(" ms");
    oled3("TX TIMEOUT", 
          "No ACK received",
          "Duration: " + String(txDurationMs) + "ms");
  }
  
  delay(500);  // Brief display time
}

void enterDeepSleep() {
  Serial.print("[LOW_POWER] Success rate: ");
  Serial.print(successfulTX);
  Serial.print("/");
  Serial.println(successfulTX + failedTX);
  Serial.print("[LOW_POWER] Going to sleep for ");
  Serial.print(SLEEP_INTERVAL_SECONDS);
  Serial.println(" seconds...");
  Serial.flush();
  
  // Display sleep info
  oled3("Entering Sleep",
        String(SLEEP_INTERVAL_SECONDS) + " seconds",
        "Saves " + String((SLEEP_INTERVAL_SECONDS - 3) * 120) + " mA·s");
  delay(1000);
  
  // Turn off display to save power
  display.clearDisplay();
  display.display();
  
  // Configure wake timer
  esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_SECONDS * uS_TO_S_FACTOR);
  
  // Enter deep sleep (MCU consumes ~10 µA)
  esp_deep_sleep_start();
  
  // Code never reaches here - deep sleep resets MCU
  // Next execution starts at setup()
}

// ===================== SETUP ===================================

void setup() {
  // Increment boot counter (stored in RTC memory)
  bootCount++;
  
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== LoRa End Node with Deep Sleep ===");
  Serial.print("Boot count: ");
  Serial.println(bootCount);
  Serial.print("Sleep interval: ");
  Serial.print(SLEEP_INTERVAL_SECONDS);
  Serial.println(" seconds");
  
  // Initialize OLED
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    // Continue anyway - not critical
  }
  
  oled3("End Node LowPower",
        "Booting...",
        "Count: " + String(bootCount));
  
  // Initialize LoRa radio
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  
  if (!LoRa.begin(FREQ_HZ)) {
    Serial.println("LoRa init FAILED");
    oled3("LoRa FAIL", "Check wiring", "Sleep & retry");
    delay(2000);
    // Sleep and retry on next wake
    enterDeepSleep();
  }
  
  // Configure LoRa
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSyncWord(LORA_SYNC);
  LoRa.enableCrc();
  LoRa.setTxPower(17, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  
  Serial.println("LoRa initialized: 923 MHz, SF8");
  delay(100);
}

// ===================== LOOP ====================================

void loop() {
  // Execute transmission cycle
  transmitSensorData();
  
  // Enter deep sleep until next wake cycle
  // (This function never returns - deep sleep resets the MCU)
  enterDeepSleep();
  
  // Code never reaches here
}
