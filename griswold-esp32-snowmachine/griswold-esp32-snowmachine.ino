// Platform: ESP32 (Arduino core)
// Libraries (install via Library Manager):
// - arduinoWebSockets by Links2004
// - ArduinoJson by Benoit Blanchon

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <map>
#include <string>

// ==== CONFIGURE ME ====
#define WIFI_SSID     "Hearthside"
#define WIFI_PASS     ""

// Example: ws://example.com:8080/path
#define WS_HOST       "192.168.1.10"
#define WS_PORT       8765
#define WS_PATH       "/"

enum SystemState { PRE_WIFI_CONNECTING, WIFI_CONNECTING, PRE_SERVER_CONNECTING, SERVER_CONNECTING, RUNNING };
SystemState system_state = PRE_WIFI_CONNECTING;

const String MAC_ADDRESS_STRING = String(ESP.getEfuseMac());

const uint8_t CHANNELS[] = {0};
const uint8_t ON_PIN = 13;
const uint8_t OFF_PIN = 14;

bool led_state = false;

// Choose the LED pin for your board. Many ESP32 dev boards use GPIO 2.
const int LED_PIN = 2;

// Approach 1: pure millis(), LED is a function of time+mode.
enum Mode { SOLID, FAST, SLOW, PERIODIC, OFFMODE };
Mode mode = FAST;            // change at runtime as needed
uint32_t now;
uint32_t last_on_time_ms = 0;
uint32_t last_off_time_ms = 0;

const uint32_t activation_time_ms = 2000;

bool ledFunc(Mode m, uint32_t t) {
  switch (m) {
    case SOLID:     return true;
    case OFFMODE:   return false;
    case FAST:      return (t % 200) < 100;          // 100 on, 100 off
    case SLOW:      return (t % 1000) < 500;         // 500 on, 500 off
    case PERIODIC:  return (t % 60000) < 1000 && (t % 60000) % 1000 < 100;       //Every 60s, blink once for 100ms
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

  const String id = String(doc["id"]);
  if (!id) return;

  // read state as float; default to -1.0 if absent
  float state = doc["state"] | -1.0f;
  if (state < 0.0f) return;

  int delimeter_index = id.indexOf('-');
  if (delimeter_index == -1) {
    Serial.println("Invalid message");
    return;
  }

  // Expect message in format <MAC_ADDRESS>-<CHANNEL>, ex. 43891384943-13
  const String mac_string = id.substring(0, delimeter_index);
  const String channel_string = id.substring(delimeter_index+1);

  // If the message was not meant for this device, ignore it
  if (mac_string.compareTo(MAC_ADDRESS_STRING) != 0) {
    Serial.println("Message not meant for this device, ignoring.");
    return;
  }

  long tmp = channel_string.toInt();
  if (tmp < 0 || tmp > 255) {
    Serial.println("Bad channel value, ignoring.");
    return;
  }
  uint8_t channel = (uint8_t)tmp;

  for(int i = 0; i < sizeof(CHANNELS) / sizeof(CHANNELS[0]); i++) {
    if (channel == CHANNELS[i]) {
      Serial.print("Setting Channel ");
      Serial.print(channel);
      Serial.print(" to ");
      Serial.println(state);
      if (state > 0.5f) {
        last_on_time_ms = millis();
      } else {
        last_off_time_ms = millis();
      }
      // Once we set the state, return
      return;
    }
  }

  Serial.print("Channel ");
  Serial.print(channel);
  Serial.println(" is not one of the registered channels. Ignoring.");
}

void sendIds() {
  StaticJsonDocument<96> doc;           // enough for {"ids":["..."]}
  JsonArray arr = doc.createNestedArray("ids");
  for(int i = 0; i < sizeof(CHANNELS) / sizeof(CHANNELS[0]); i++) {
    arr.add(MAC_ADDRESS_STRING + "-" + String(CHANNELS[i]));
  }

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
      // Send connection message: {ids: [ESP.getEfuseMac()]}
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
  Serial.begin(115200);

  uint64_t mac_esp_efuse = ESP.getEfuseMac();
  Serial.printf("ESP32 Chip MAC (from efuse): %012llx \n", mac_esp_efuse);

  pinMode(LED_PIN, OUTPUT);
  pinMode(ON_PIN, OUTPUT);
  pinMode(OFF_PIN, OUTPUT);

  // Turn off once connected
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  switch (system_state) {
    case PRE_WIFI_CONNECTING: {
      mode = FAST;
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      WiFi.setSleep(false); 
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

  // Ignore requests during the first 2 seconds of program
  if (now < activation_time_ms) {
    return;
  }

  // Send command if we received one recently
  if (now - last_off_time_ms < activation_time_ms) {
    digitalWrite(OFF_PIN, HIGH);
  } else {
    digitalWrite(OFF_PIN, LOW);
  }

  if (now - last_on_time_ms < activation_time_ms) {
    digitalWrite(ON_PIN, HIGH);
  } else {
    digitalWrite(ON_PIN, LOW);
  }
 
}
