/*
 * Yarumo IoT V0.1 - ESP32 Relay X4 - banco de pruebas.
 * Arduino ESP32 Dev Module, particiones compatibles con OTA.
 * Completar credenciales SOLO en copia local; no subirlas a GitHub.
 * Probar con contactos de rele sin cargas. DI/AI maximo 3.3 V.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

constexpr char VERSION[] = "0.1.0";
constexpr char WIFI_SSID[] = "TU_RED_WIFI";
constexpr char WIFI_PASS[] = "TU_CLAVE_WIFI";
constexpr char OTA_PASS[] = "TU_CLAVE_OTA";
constexpr char HOSTNAME[] = "Yarumo-X4";
constexpr uint8_t DI[] = {13, 14, 16, 17, 19, 27};
constexpr uint8_t AI[] = {34, 35, 36, 39};
constexpr uint8_t DO_PIN[] = {32, 33, 25, 26};
constexpr uint8_t RELAY_ON = HIGH, RELAY_OFF = LOW;
constexpr uint32_t MAX_PULSE_MS = 10000;
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint32_t DEFAULT_RATE = 1000;
Preferences prefs;
bool nvsOK = false;
bool relayOn[4] = {};
uint32_t pulseEnd[4] = {};
bool otaReady = false, updating = false;
bool wifiWasUp = false;
uint32_t reportRate = DEFAULT_RATE, lastReport = 0, lastRetry = 0;
char deviceId[13] = {};
char line[80] = {};
size_t lineLen = 0;
bool discardLine = false;

void offRelay(uint8_t i) {
  digitalWrite(DO_PIN[i], RELAY_OFF);
  relayOn[i] = false;
  pulseEnd[i] = 0;
}
void allOff() {
  for (uint8_t i = 0; i < 4; ++i) offRelay(i);
}
bool startPulse(uint8_t i, uint32_t ms) {
  if (i >= 4 || ms < 50 || ms > MAX_PULSE_MS || updating) return false;
  pulseEnd[i] = millis() + ms;
  relayOn[i] = true;
  digitalWrite(DO_PIN[i], RELAY_ON);
  return true;
}
void tickOutputs() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 4; ++i)
    if (relayOn[i] && (int32_t)(now - pulseEnd[i]) >= 0) offRelay(i);
}
void report() {
  Serial.printf("{\"device\":\"%s\",\"version\":\"%s\",\"wifi\":%s,\"uptime_ms\":%lu,\"di\":[",
                deviceId, VERSION, WiFi.status() == WL_CONNECTED ? "true" : "false",
                (unsigned long)millis());
  for (uint8_t i = 0; i < 6; ++i)
    Serial.printf("%s%d", i ? "," : "", digitalRead(DI[i]));
  Serial.print("],\"ai_raw\":[");
  for (uint8_t i = 0; i < 4; ++i)
    Serial.printf("%s%u", i ? "," : "", (unsigned)analogRead(AI[i]));
  Serial.print("],\"ai_mv\":[");
  for (uint8_t i = 0; i < 4; ++i)
    Serial.printf("%s%u", i ? "," : "", (unsigned)analogReadMilliVolts(AI[i]));
  Serial.print("],\"relay_cmd\":[");
  for (uint8_t i = 0; i < 4; ++i)
    Serial.printf("%s%d", i ? "," : "", relayOn[i] ? 1 : 0);
  Serial.println("]}");
}
void help() {
  Serial.println("HELP | STATUS | PULSE <1-4> <50-10000 ms> | OFF <1-4> | ALL OFF | RATE <250-60000 ms>");
}
bool parseUInt(const char* s, uint32_t &out) {
  if (!s || !*s) return false;
  uint32_t n = 0;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9') return false;
    uint32_t d = (uint32_t)(*s - '0');
    if (n > (UINT32_MAX - d) / 10) return false;
    n = n * 10 + d;
  }
  out = n;
  return true;
}
void command(char* input) {
  for (char* p = input; *p; ++p) *p = (char)toupper((unsigned char)*p);
  char* save = nullptr;
  char* cmd = strtok_r(input, " \t", &save);
  char* a = strtok_r(nullptr, " \t", &save);
  char* b = strtok_r(nullptr, " \t", &save);
  char* extra = strtok_r(nullptr, " \t", &save);
  uint32_t i, ms;
  if (!cmd) return;
  if (!strcmp(cmd, "HELP") && !a) { help(); return; }
  if (!strcmp(cmd, "STATUS") && !a) { report(); return; }
  if (!strcmp(cmd, "ALL") && a && !strcmp(a, "OFF") && !b) {
    allOff(); Serial.println("OK ALL OFF"); return;
  }
  if (!strcmp(cmd, "OFF") && !b && parseUInt(a, i) && i >= 1 && i <= 4) {
    offRelay(i - 1); Serial.println("OK OFF"); return;
  }
  if (!strcmp(cmd, "PULSE") && !extra && parseUInt(a, i) && i >= 1 && i <= 4 &&
      parseUInt(b, ms) && startPulse(i - 1, ms)) {
    Serial.println("OK PULSE"); return;
  }
  if (!strcmp(cmd, "RATE") && !b && parseUInt(a, ms) && ms >= 250 && ms <= 60000) {
    reportRate = ms;
    bool saved = nvsOK && prefs.putUInt("rate", ms) == sizeof(uint32_t);
    Serial.println(saved ? "OK RATE SAVED" : "OK RATE VOLATILE");
    return;
  }
  Serial.println("ERR command or range"); help();
}
void readSerial() {
  // Limit work per loop; never wait for a newline.
  for (int k = 0; k < 32 && Serial.available(); ++k) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (discardLine) Serial.println("ERR line too long");
      else if (lineLen) { line[lineLen] = '\0'; command(line); }
      lineLen = 0; discardLine = false;
    } else if (!discardLine) {
      if (lineLen < sizeof(line) - 1) line[lineLen++] = c;
      else discardLine = true;
    }
  }
}
void tickWiFi() {
  const bool up = WiFi.status() == WL_CONNECTED;
  if (up && !wifiWasUp) {
    Serial.print("WiFi IP: "); Serial.println(WiFi.localIP());
    if (!otaReady) {
      ArduinoOTA.begin();
      otaReady = true;
      Serial.println("OTA ready");
    }
  }
  if (!up && wifiWasUp) {
    allOff();
    Serial.println("WiFi lost; all relays off");
    if (otaReady) { ArduinoOTA.end(); otaReady = false; }
  }
  wifiWasUp = up;
  if (!up && !updating && millis() - lastRetry >= WIFI_RETRY_MS) {
    lastRetry = millis();
    WiFi.reconnect();
  }
}
void setup() {
  // Set output latch before enabling output. Hardware must also ensure safe boot.
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWrite(DO_PIN[i], RELAY_OFF);
    pinMode(DO_PIN[i], OUTPUT);
  }
  allOff();
  Serial.begin(115200);
  for (uint8_t p : DI) pinMode(p, INPUT_PULLUP);
  for (uint8_t p : AI) pinMode(p, INPUT);
  analogReadResolution(12);
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    snprintf(deviceId, sizeof(deviceId), "%02X%02X%02X%02X%02X%02X",
      (unsigned)mac[0], (unsigned)mac[1], (unsigned)mac[2],
      (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
  } else {
    strcpy(deviceId, "UNKNOWN");
    Serial.println("ERR device MAC");
  }
  nvsOK = prefs.begin("yarumo", false);
  if (nvsOK) {
    uint32_t saved = prefs.getUInt("rate", DEFAULT_RATE);
    if (saved >= 250 && saved <= 60000) reportRate = saved;
  }
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.onStart([]() { updating = true; allOff(); });
  ArduinoOTA.onEnd([]() { allOff(); });
  ArduinoOTA.onError([](ota_error_t e) {
    allOff(); updating = false; Serial.printf("OTA error %u\n", (unsigned)e);
  });
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastRetry = millis();
  Serial.printf("\nYarumo IoT %s | ID %s\n", VERSION, deviceId);
  help();
}
void loop() {
  tickOutputs();
  tickWiFi();
  if (otaReady) ArduinoOTA.handle();
  if (!updating) {
    readSerial();
    if (millis() - lastReport >= reportRate) {
      lastReport = millis();
      report();
    }
  }
  delay(1);
}
