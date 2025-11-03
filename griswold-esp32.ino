// Platform: ESP32 (Arduino core)
// Libraries (install via Library Manager):
// - arduinoWebSockets by Links2004
// - ArduinoJson by Benoit Blanchon

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// ==== CONFIGURE ME ====
#define WIFI_SSID     "Hearthside"
#define WIFI_PASS     ""

// Example: ws://example.com:8080/path
#define WS_HOST       "192.168.1.210"
#define WS_PORT       8765
#define WS_PATH       "/"

enum SystemState { PRE_WIFI_CONNECTING, WIFI_CONNECTING, PRE_SERVER_CONNECTING, SERVER_CONNECTING, RUNNING };
SystemState system_state = PRE_WIFI_CONNECTING;

// Unique build-time identifier to match against incoming messages
static const char* BUILD_ID = "DEVICE_ABC_123";

bool led_state = false;

// Choose the LED pin for your board. Many ESP32 dev boards use GPIO 2.
const int LED_PIN = 2;
const int CH0_PIN = 3;
const int CH1_PIN = 4;

// Approach 1: pure millis(), LED is a function of time+mode.
enum Mode { SOLID, FAST, SLOW, PERIODIC, OFFMODE };
Mode mode = FAST;            // change at runtime as needed
uint32_t now;

bool ledFunc(Mode m, uint32_t t) {
  switch (m) {
    case SOLID:     return true;
    case OFFMODE:   return false;
    case FAST:      return (t % 200) < 100;          // 100 on, 100 off
    case SLOW:      return (t % 1000) < 500;         // 500 on, 500 off
    case PERIODIC:  return (t % 5000) < 1000 && (t % 5000) % 100 < 50;       //Every 4s, blink for 50ms repeatedly for 1s
  }
  return false;
}


// ======================

WebSocketsClient ws;

void handleMessage(const String& payload) {
  // Expect: {"id": "<str>", "state": 0.0-1.0}
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return;

  const char* id = doc["id"];
  if (!id) return;

  // read state as float; default to -1.0 if absent
  float state = doc["state"] | -1.0f;
  if (state < 0.0f) return;

  if (strcmp(id, BUILD_ID) == 0) {
    if (state > 0.5f) {
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }
  }
}

void sendIds() {
  StaticJsonDocument<96> doc;           // enough for {"ids":["..."]}
  JsonArray arr = doc.createNestedArray("ids");
  arr.add(BUILD_ID);

  // Option A: no dynamic heap after doc
  char buf[96];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  ws.sendTXT(buf, n);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED: {
      system_state = SERVER_CONNECTING;
      // will auto-reconnect based on setReconnectInterval
      break;
    }
    case WStype_CONNECTED: {
      // Send connection message: {ids: [BUILD_ID]}
      sendIds();
      // Set state to connected
      system_state = RUNNING;
      break;
    }
    case WStype_TEXT: {
      String msg = String((char*)payload, length);
      handleMessage(msg);
      break;
    }
    case WStype_BIN:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    case WStype_PING:
    case WStype_PONG:
    default:
      break;
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(LED_PIN, OUTPUT);
  pinMode(CH0_PIN, OUTPUT);
  pinMode(CH1_PIN, OUTPUT);

  // Turn off once connected
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  switch (system_state) {
    case PRE_WIFI_CONNECTING: {
      mode = FAST;
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      system_state = WIFI_CONNECTING;
      break;
    }
    case WIFI_CONNECTING: {
      mode = FAST;
      if (WiFi.status() == WL_CONNECTED) {
        system_state = PRE_SERVER_CONNECTING;
      }
      break;
    }
    case PRE_SERVER_CONNECTING: {
      mode = SLOW;
      ws.begin(WS_HOST, WS_PORT, WS_PATH);

      ws.onEvent(webSocketEvent);
      ws.setReconnectInterval(5000);   // ms
      ws.enableHeartbeat(15000, 3000, 2); // ping every 15s, timeout 3s, 2 misses
      system_state = SERVER_CONNECTING;
      break;
    }
    case SERVER_CONNECTING: {
      mode = SLOW;
      ws.loop();
      break;
    }
    case RUNNING: {
      mode = PERIODIC;
      ws.loop(); // processes incoming frames and reconnects as needed
      break;
    }
  }

  static SystemState last_state = (SystemState)-1;
  if (system_state != last_state) {
    Serial.println(system_state);
    last_state = system_state;
  }
  
  // Display LED Blink code based on mode
  now = millis();
  digitalWrite(LED_PIN, ledFunc(mode, now) ? HIGH : LOW);
}
