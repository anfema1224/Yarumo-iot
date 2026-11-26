/*
 * Yarumo IoT V0.2 - ESP32 Relay X4 - banco de pruebas.
 * Arduino ESP32 Dev Module, particiones compatibles con OTA.
 * Completar credenciales SOLO en copia local; no subirlas a GitHub.
 * Probar con contactos de rele sin cargas. DI/AI maximo 3.3 V.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_mac.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

constexpr char VERSION[] = "0.2.0";
constexpr char WIFI_SSID[] = "TU_RED_WIFI";
constexpr char WIFI_PASS[] = "TU_CLAVE_WIFI";
constexpr char OTA_PASS[] = "TU_CLAVE_OTA";
constexpr char WEB_USER[] = "admin";
constexpr char WEB_PASS[] = "TU_CLAVE_WEB"; // Cambiar en copia local (minimo 12 caracteres)
constexpr char HOSTNAME[] = "Yarumo-X4";
constexpr uint8_t DI[] = {13, 14, 16, 17, 19, 27};
constexpr uint8_t AI[] = {34, 35, 36, 39};
constexpr uint8_t DO_PIN[] = {32, 33, 25, 26};
constexpr uint8_t RELAY_ON = HIGH, RELAY_OFF = LOW;
constexpr uint32_t MAX_PULSE_MS = 10000;
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint32_t DEFAULT_RATE = 1000;
Preferences prefs;
WebServer web(80);
SemaphoreHandle_t relayLock = nullptr;
bool timerReady = false;
bool webReady = false;
bool webEnabled = false;
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

void offLocked(uint8_t i) {
  digitalWrite(DO_PIN[i], RELAY_OFF);
  relayOn[i] = false;
  pulseEnd[i] = 0;
}
void offRelay(uint8_t i) {
  if (relayLock) xSemaphoreTake(relayLock, portMAX_DELAY);
  offLocked(i);
  if (relayLock) xSemaphoreGive(relayLock);
}
void allOff() {
  if (relayLock) xSemaphoreTake(relayLock, portMAX_DELAY);
  for (uint8_t i = 0; i < 4; ++i) offLocked(i);
  if (relayLock) xSemaphoreGive(relayLock);
}
bool startPulse(uint8_t i, uint32_t ms) {
  if (!timerReady || !relayLock || i >= 4 || ms < 50 ||
      ms > MAX_PULSE_MS || updating || WiFi.status() != WL_CONNECTED) return false;
  xSemaphoreTake(relayLock, portMAX_DELAY);
  // Do not extend a running pulse; turn it off first if needed.
  if (relayOn[i]) { xSemaphoreGive(relayLock); return false; }
  pulseEnd[i] = millis() + ms;
  relayOn[i] = true;
  digitalWrite(DO_PIN[i], RELAY_ON);
  xSemaphoreGive(relayLock);
  return true;
}
void timerTask(void*) {
  for (;;) {
    xSemaphoreTake(relayLock, portMAX_DELAY);
    const uint32_t now = millis();
    const bool connected = WiFi.status() == WL_CONNECTED;
    for (uint8_t i = 0; i < 4; ++i)
      if (relayOn[i] && (!connected || (int32_t)(now - pulseEnd[i]) >= 0))
        offLocked(i);
    xSemaphoreGive(relayLock);
    vTaskDelay(pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1);
  }
}
void relaySnapshot(bool* on, uint32_t* remaining) {
  if (relayLock) xSemaphoreTake(relayLock, portMAX_DELAY);
  uint32_t now = millis();
  for (uint8_t i = 0; i < 4; ++i) {
    on[i] = relayOn[i];
    remaining[i] = on[i] && (int32_t)(pulseEnd[i] - now) > 0 ? pulseEnd[i] - now : 0;
  }
  if (relayLock) xSemaphoreGive(relayLock);
}
void report() {
  bool on[4]; uint32_t remaining[4]; relaySnapshot(on, remaining);
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
    Serial.printf("%s%d", i ? "," : "", on[i] ? 1 : 0);
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

const char PAGE[] PROGMEM = R"YARUMOHTML(<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Yarumo IoT X4</title><style>
*{box-sizing:border-box}body{margin:0;background:#eef3f7;color:#152b3c;font:16px system-ui,sans-serif}header{background:#153d55;color:white;padding:20px}main{max-width:900px;margin:auto;padding:16px}h1{margin:0;font-size:25px}h2{font-size:19px}p{margin:8px 0}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}.card{background:white;border:1px solid #d5e0e9;border-radius:12px;padding:14px}.val{font-size:24px;font-weight:bold;margin:8px 0}.muted{color:#526879;font-size:13px}button,input{font:inherit;padding:10px;border-radius:7px}button{border:0;background:#176b80;color:white;cursor:pointer;margin:4px 0}button:disabled{opacity:.45}button.off{background:#933e3e}input{width:100%;border:1px solid #aebfcb}.status{padding:10px;background:white;border-radius:8px;min-height:42px}.on{color:#077346}.idle{color:#526879}footer{font-size:13px;margin:20px 0;color:#526879}
</style></head><body><header><h1>Yarumo IoT</h1><p>ESP32 Relay X4 · V0.2.0 · Panel local</p></header><main>
<p id="status" class="status" role="status">Conectando...</p><p id="info" class="muted"></p>
<h2>Entradas digitales</h2><div id="di" class="grid"></div>
<h2>Entradas analógicas</h2><div id="ai" class="grid"></div>
<h2>Relés y temporización</h2><p>Selecciona un pulso de 0,05 a 10 segundos. No se prolonga un pulso activo.</p>
<div id="relays" class="grid"></div><button class="off" id="all" disabled>Apagar todos</button>
<footer>Banco de pruebas: sin cargas conectadas. El estado mostrado es la orden GPIO, no la posición física del contacto. Acceso únicamente en red local de confianza.</footer></main>
<script>
const diPins=[13,14,16,17,19,27],aiPins=[34,35,36,39],doPins=[32,33,25,26];
const $=id=>document.getElementById(id);
let online=false,busy=false,states=[0,0,0,0],polling=false;
function init(){
diPins.forEach((p,i)=>{$('di').insertAdjacentHTML('beforeend','<div class="card">DI'+(i+1)+' · GPIO'+p+'<div class="val" id="d'+i+'">--</div></div>')});
aiPins.forEach((p,i)=>{$('ai').insertAdjacentHTML('beforeend','<div class="card">AI'+(i+1)+' · GPIO'+p+'<div class="val" id="a'+i+'">--</div><div class="muted" id="raw'+i+'"></div></div>')});
doPins.forEach((p,i)=>{$('relays').insertAdjacentHTML('beforeend','<div class="card">Relé '+(i+1)+' · GPIO'+p+'<div class="val" id="r'+i+'">--</div><div class="muted" id="t'+i+'"></div><label>Tiempo (s)<input id="sec'+i+'" type="number" min=".05" max="10" step=".05" value="2"></label><button id="go'+i+'" disabled>Activar</button> <button class="off" id="off'+i+'" disabled>Apagar</button></div>');$('go'+i).onclick=()=>pulse(i);$('off'+i).onclick=()=>send('off',i+1)});
$('all').onclick=()=>send('all',0);refresh();setInterval(refresh,750);}
function controls(){doPins.forEach((p,i)=>{$('go'+i).disabled=!online||busy||!!states[i];$('off'+i).disabled=!online||busy});$('all').disabled=!online||busy;}
async function refresh(){if(polling||busy)return;polling=true;try{const r=await fetch('/api/status',{cache:'no-store',signal:AbortSignal.timeout(2500)});if(!r.ok)throw Error('HTTP '+r.status);const s=await r.json();online=true;states=s.relay_cmd;$('status').textContent='Conectado';$('info').textContent='ID '+s.device+' · Firmware '+s.version;
s.di.forEach((v,i)=>{$('d'+i).textContent=v?'HIGH':'LOW';$('d'+i).className='val '+(v?'on':'idle')});s.ai_mv.forEach((v,i)=>{$('a'+i).textContent=v+' mV';$('raw'+i).textContent='ADC '+s.ai_raw[i]+' / 4095'});states.forEach((v,i)=>{$('r'+i).textContent=v?'ON':'OFF';$('r'+i).className='val '+(v?'on':'idle');$('t'+i).textContent=(s.remaining_ms[i]/1000).toFixed(1)+' s restantes'});
}catch(e){online=false;$('status').textContent='Sin conexión o lectura no disponible';}finally{polling=false;controls();}}
function pulse(i){const v=Number($('sec'+i).value);if(!Number.isFinite(v)||v<.05||v>10){alert('Tiempo entre 0,05 y 10 s');return;}send('pulse',i+1,Math.round(v*1000));}
async function send(action,n,ms=0){if(!online||busy)return;busy=true;controls();try{const body=new URLSearchParams({action,n:String(n),ms:String(ms)});const r=await fetch('/api/relay',{method:'POST',headers:{'X-Yarumo-Control':'1'},body,signal:AbortSignal.timeout(2500)});if(!r.ok)throw Error(await r.text());}catch(e){alert('No confirmado: '+e.message);}finally{busy=false;await refresh();controls();}}
init();
</script></body></html>)YARUMOHTML";
bool webAuth() {
  if (!webEnabled) { web.send(503, "text/plain", "Panel disabled"); return false; }
  if (!web.authenticate(WEB_USER, WEB_PASS)) {
    web.requestAuthentication(DIGEST_AUTH, "Yarumo X4", "Authentication required");
    return false;
  }
  web.sendHeader("Cache-Control", "no-store");
  return true;
}
String statusJson() {
  bool on[4]; uint32_t remaining[4]; relaySnapshot(on, remaining);
  String j; j.reserve(400);
  j = "{\"device\":\""; j += deviceId; j += "\",\"version\":\""; j += VERSION;
  j += "\",\"di\":[";
  for (uint8_t i=0;i<6;i++) { if(i)j+=','; j+=String(digitalRead(DI[i])); }
  j += "],\"ai_raw\":[";
  for (uint8_t i=0;i<4;i++) { if(i)j+=','; j+=String(analogRead(AI[i])); }
  j += "],\"ai_mv\":[";
  for (uint8_t i=0;i<4;i++) { if(i)j+=','; j+=String(analogReadMilliVolts(AI[i])); }
  j += "],\"relay_cmd\":[";
  for (uint8_t i=0;i<4;i++) { if(i)j+=','; j+=on[i]?'1':'0'; }
  j += "],\"remaining_ms\":[";
  for (uint8_t i=0;i<4;i++) { if(i)j+=','; j+=String(remaining[i]); }
  j += "]}"; return j;
}
void webRelay() {
  if (!webAuth()) return;
  // A custom header is required for all mutations; no CORS is enabled.
  if (web.header("X-Yarumo-Control") != "1") { web.send(403,"text/plain","Missing control header"); return; }
  String action=web.arg("action");
  if (action=="all") { allOff(); web.send(200,"text/plain","OK"); return; }
  uint32_t n,ms;
  if (!parseUInt(web.arg("n").c_str(),n)||n<1||n>4) { web.send(400,"text/plain","Invalid relay"); return; }
  if (action=="off") { offRelay(n-1); web.send(200,"text/plain","OK"); return; }
  if (action=="pulse" && parseUInt(web.arg("ms").c_str(),ms) && ms>=50 && ms<=MAX_PULSE_MS) {
    if (startPulse(n-1,ms)) web.send(200,"text/plain","OK");
    else web.send(409,"text/plain","Relay busy or not available");
    return;
  }
  web.send(400,"text/plain","Invalid command");
}
void configureWeb() {
  webEnabled = strcmp(WEB_PASS,"TU_CLAVE_WEB")!=0 && strlen(WEB_PASS)>=12;
  if (!webEnabled) { Serial.println("WEB disabled: configure WEB_PASS (12+ characters)"); return; }
  const char* headers[]={"X-Yarumo-Control"};
  web.collectHeaders(headers,1);
  web.on("/",HTTP_GET,[](){ if(webAuth()) web.send_P(200,"text/html; charset=utf-8",PAGE); });
  web.on("/api/status",HTTP_GET,[](){ if(webAuth()) web.send(200,"application/json",statusJson()); });
  web.on("/api/relay",HTTP_POST,webRelay);
  web.onNotFound([](){ web.send(404,"text/plain","Not found"); });
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
    if (webEnabled && !webReady) { web.begin(); webReady=true; Serial.print("Panel: http://"); Serial.println(WiFi.localIP());
    }
  }
  if (!up && wifiWasUp) {
    allOff();
    Serial.println("WiFi lost; all relays off");
    if (otaReady) { ArduinoOTA.end(); otaReady = false; }
    if (webReady) { web.close(); webReady=false; }
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
  relayLock=xSemaphoreCreateMutex();
  if(relayLock) timerReady=(xTaskCreate(timerTask,"relayTimer",3072,nullptr,3,nullptr)==pdPASS);
  if(!timerReady) Serial.println("ERR timer task; pulses disabled");
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
  configureWeb();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastRetry = millis();
  Serial.printf("\nYarumo IoT %s | ID %s\n", VERSION, deviceId);
  help();
}
void loop() {
  tickWiFi();
  if (otaReady) ArduinoOTA.handle();
  if (!updating) {
    if (webReady) web.handleClient();
    readSerial();
    if (millis() - lastReport >= reportRate) {
      lastReport = millis();
      report();
    }
  }
  delay(1);
}
