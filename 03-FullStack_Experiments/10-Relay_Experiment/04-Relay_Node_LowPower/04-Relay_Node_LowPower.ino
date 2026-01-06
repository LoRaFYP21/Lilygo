/*
  LoRa Relay Node with Light Sleep Power Management
  
  Power Optimization Strategy:
  - Relay node must stay responsive to forward packets
  - Uses light sleep between packet receptions (faster wake than deep sleep)
  - Wakes on timeout to check for packets
  - Processes and forwards packet, then returns to light sleep
  
  Current consumption:
  - Active (RX/TX): ~80 mA during packet handling
  - Light sleep: ~1.5 mA (1500 µA)
  - Average (low traffic): ~2-5 mA → weeks of battery life
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <LoRa.h>
#include <driver/rtc_io.h>

// ===================== SLEEP CONFIG ============================
#define SLEEP_TIMEOUT_MS        30000     // Sleep after 30s of no activity
#define RX_CHECK_INTERVAL_MS    100       // How often to check for packets while awake
#define uS_TO_MS_FACTOR         1000ULL   // Microseconds to milliseconds

// Statistics stored in RTC memory
RTC_DATA_ATTR uint32_t totalPacketsRelayed = 0;
RTC_DATA_ATTR uint32_t totalWakeCycles = 0;
RTC_DATA_ATTR uint32_t totalSleepCycles = 0;

// Runtime statistics (reset each power cycle)
uint32_t packetsRelayedThisBoot = 0;
unsigned long lastPacketMs = 0;
unsigned long bootTimeMs = 0;

// ===================== RADIO CONFIG ============================
#define FREQ_HZ   923E6
#define LORA_SYNC 0xA5
#define LORA_SF   8

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

// ===================== HOP HEADER HELPERS ======================

static bool stripHopHeader(const String& in, String& outPayload, int& hopCount) {
  hopCount = 0;
  if(in.length() < 3 || in.charAt(0) != 'H') {
    outPayload = in;
    return false;
  }
  int colon = in.indexOf(':');
  if(colon <= 1) {
    outPayload = in;
    return false;
  }
  String hopStr = in.substring(1, colon);
  int h = hopStr.toInt();
  if(h < 0) h = 0;
  hopCount = h;
  outPayload = in.substring(colon + 1);
  return true;
}

static void sendLoRaRawWrapped(const String& payload, int hop) {
  if(hop < 0) hop = 0;
  String wrap = "H" + String(hop) + ":" + payload;
  LoRa.beginPacket();
  LoRa.print(wrap);
  LoRa.endPacket();
}

// ===================== POWER MANAGEMENT ========================

void updateDisplayStats() {
  unsigned long uptimeS = (millis() - bootTimeMs) / 1000;
  String line1 = "Relay LP Wakes:" + String(totalWakeCycles);
  String line2 = "Pkts:" + String(totalPacketsRelayed) + 
                 " Boot:" + String(packetsRelayedThisBoot);
  String line3 = "Up:" + String(uptimeS) + "s Sleep:" + String(totalSleepCycles);
  oled3(line1, line2, line3);
}

void enterLightSleep(uint32_t sleepMs) {
  Serial.print("[LOW_POWER] Entering light sleep for ");
  Serial.print(sleepMs);
  Serial.println(" ms");
  Serial.flush();
  
  totalSleepCycles++;
  updateDisplayStats();
  
  // Configure wake on timer
  esp_sleep_enable_timer_wakeup(sleepMs * uS_TO_MS_FACTOR);
  
  // Enter light sleep (maintains state, fast wake)
  esp_light_sleep_start();
  
  // Code continues here after wake
  totalWakeCycles++;
  Serial.println("[LOW_POWER] Woke from light sleep");
}

// ===================== PACKET HANDLING =========================

void processPacket() {
  int packetSize = LoRa.parsePacket();
  if (!packetSize) return;
  
  // Update last activity timestamp
  lastPacketMs = millis();
  packetsRelayedThisBoot++;
  totalPacketsRelayed++;
  
  // Read packet
  String rawPkt;
  while (LoRa.available()) rawPkt += (char)LoRa.read();
  
  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();
  
  // Parse hop header
  String payload;
  int hopIn = 0;
  stripHopHeader(rawPkt, payload, hopIn);
  
  // Determine packet type
  bool isAck = payload.startsWith("ACK,");
  bool isAckF = payload.startsWith("ACKF,");
  
  // For ACK/ACKF: keep original hop for routing
  // For MSG/MSGF: increment hop count
  int hopOut = (isAck || isAckF) ? hopIn : (hopIn + 1);
  
  Serial.print("[RELAY RX] hopIn=");
  Serial.print(hopIn);
  Serial.print(" | RSSI=");
  Serial.print(rssi);
  Serial.print(" SNR=");
  Serial.print(snr, 1);
  Serial.print(" | type=");
  Serial.print(isAck ? "ACK" : (isAckF ? "ACKF" : "MSG"));
  Serial.print(" | Total relayed: ");
  Serial.println(totalPacketsRelayed);
  
  // Update display
  oled3("Relay RX h=" + String(hopIn),
        payload.substring(0, 16),
        "RSSI:" + String(rssi) + " Tot:" + String(totalPacketsRelayed));
  
  delay(5);  // Small guard before retransmit
  
  // Forward packet
  sendLoRaRawWrapped(payload, hopOut);
  
  Serial.print("[RELAY TX] hopOut=");
  Serial.print(hopOut);
  Serial.println(" | Forwarded");
  
  // Update stats display
  updateDisplayStats();
}

// ===================== SETUP ===================================

void setup() {
  bootTimeMs = millis();
  totalWakeCycles++;
  
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== LoRa Relay Node with Light Sleep ===");
  Serial.print("Total wake cycles: ");
  Serial.println(totalWakeCycles);
  Serial.print("Total packets relayed: ");
  Serial.println(totalPacketsRelayed);
  Serial.print("Total sleep cycles: ");
  Serial.println(totalSleepCycles);
  
  // Initialize OLED
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
  }
  
  oled3("Relay Node LowPower",
        "Wake: " + String(totalWakeCycles),
        "Initializing...");
  
  // Initialize LoRa
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  
  if (!LoRa.begin(FREQ_HZ)) {
    oled3("Relay: LoRa FAIL", "Check wiring", "");
    Serial.println("LoRa init failed");
    while (1) {
      delay(1000);
    }
  }
  
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSyncWord(LORA_SYNC);
  LoRa.enableCrc();
  LoRa.setTxPower(17, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  
  Serial.println("LoRa initialized: 923 MHz, SF8");
  
  oled3("Relay Ready",
        "923MHz SF8",
        "Low power mode");
  
  lastPacketMs = millis();
  delay(1000);
}

// ===================== LOOP ====================================

void loop() {
  // Check for incoming packets
  processPacket();
  
  // Check if we should enter sleep mode
  unsigned long idleTime = millis() - lastPacketMs;
  
  if (idleTime > SLEEP_TIMEOUT_MS) {
    // No activity for a while, enter light sleep
    Serial.println("[LOW_POWER] No activity detected");
    Serial.print("[LOW_POWER] Power savings: ~80mA → 1.5mA = ");
    Serial.print(int((80.0 - 1.5) / 80.0 * 100));
    Serial.println("% reduction");
    
    // Sleep for a period (will wake on timer)
    enterLightSleep(SLEEP_TIMEOUT_MS);
    
    // After waking, reset the last packet timestamp to give
    // the radio a chance to receive before sleeping again
    lastPacketMs = millis();
  }
  
  // Small delay to prevent busy-waiting
  delay(RX_CHECK_INTERVAL_MS);
}
