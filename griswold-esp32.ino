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

// Unique build-time identifier to match against incoming messages
static const char* BUILD_ID = "DEVICE_ABC_123";

bool led_state = false;

// Choose the LED pin for your board. Many ESP32 dev boards use GPIO 2.
const int LED_PIN = 2;
// ======================

WebSocketsClient ws;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);

    // TOGGLE LED
    digitalWrite(LED_PIN, !led_state);
    led_state = !led_state;
  }
}

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
      digitalWrite(LED_PIN, HIGH);
      // will auto-reconnect based on setReconnectInterval
      break;
    }
    case WStype_CONNECTED: {
      digitalWrite(LED_PIN, LOW);
      // Send connection message: {ids: [BUILD_ID]}
      sendIds();
      // Optionally send a hello or register message here
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
  pinMode(LED_PIN, OUTPUT);
  connectWiFi();

  // Turn off once connected
  digitalWrite(LED_PIN, LOW);

  // For ws://
  ws.begin(WS_HOST, WS_PORT, WS_PATH);

  // For wss://, use:
  // ws.beginSSL(WS_HOST, WS_PORT, WS_PATH); // optionally set SNI/certs

  ws.onEvent(webSocketEvent);
  ws.setReconnectInterval(5000);   // ms
  ws.enableHeartbeat(15000, 3000, 2); // ping every 15s, timeout 3s, 2 misses
}

void loop() {
  ws.loop(); // processes incoming frames and reconnects as needed
}
