/*
 * Yarumo IoT V0.3 - ESP32 Relay X4 - banco de pruebas.
 * Arduino ESP32 Dev Module, particiones compatibles con OTA.
 * Credenciales persistentes en NVS; provisionar una vez con secrets.h local.
 * Probar con contactos de rele sin cargas. DI/AI maximo 3.3 V.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h> // v7
#include <time.h>
#include <atomic>
#include <memory>
#include <new>
#include <freertos/queue.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_mac.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Optional ONE-TIME provisioning file. Ignored by Git.
#if __has_include("secrets.h")
#include "secrets.h"
#define YARUMO_HAS_SECRETS 1
#endif
#ifndef YARUMO_REPLACE_CONFIG
#define YARUMO_REPLACE_CONFIG 0
#endif

constexpr char VERSION[] = "0.3.1";
char WIFI_SSID[33] = {};
char WIFI_PASS[65] = {};
char OTA_PASS[129] = {};
constexpr char WEB_USER[] = "admin";
char WEB_PASS[129] = {};
constexpr char CLOUD_URL[] = "https://yarumo-iot.onrender.com/api/device/poll";
char CLOUD_TOKEN[65] = {};
char CLOUD_CA[4096] = {};
bool configReady = false;
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

// One versioned NVS blob; never print credentials. No writes on normal boot.
// Keep this namespace/key stable across releases and compatible OTA partitions.
constexpr char CFG_KEY[] = "cfg_v1";
constexpr size_t CFG_MAX = 8192;
template <size_t N>
bool copyField(JsonObjectConst obj, const char* name, char (&dest)[N]) {
  if (!obj[name].is<const char*>()) return false;
  const char* value = obj[name].as<const char*>();
  if (strlen(value) >= N) return false;
  strlcpy(dest, value, N);
  return true;
}
bool loadFields(JsonObjectConst obj) {
  if (obj["schema"].as<int>() != 1) return false;
  if (!copyField(obj,"ssid",WIFI_SSID) || !copyField(obj,"wifi",WIFI_PASS) ||
      !copyField(obj,"ota",OTA_PASS) || !copyField(obj,"web",WEB_PASS) ||
      !copyField(obj,"token",CLOUD_TOKEN) || !copyField(obj,"ca",CLOUD_CA)) return false;
  if (!WIFI_SSID[0] || !strcmp(WIFI_SSID,"TU_RED_WIFI") ||
      strlen(WIFI_PASS)<8 || !strcmp(WIFI_PASS,"TU_CLAVE_WIFI") ||
      !OTA_PASS[0] || !strcmp(OTA_PASS,"TU_CLAVE_OTA") ||
      strlen(WEB_PASS)<12 || !strcmp(WEB_PASS,"TU_CLAVE_WEB")) return false;
  if (CLOUD_TOKEN[0]) {
    if(strlen(CLOUD_TOKEN)!=64) return false;
    for(const char* p=CLOUD_TOKEN; *p; ++p)
      if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f'))) return false;
  }
  if(CLOUD_CA[0] && (!strstr(CLOUD_CA,"-----BEGIN CERTIFICATE-----") ||
                      !strstr(CLOUD_CA,"-----END CERTIFICATE-----"))) return false;
  return true;
}
bool readStoredConfig() {
  size_t n=prefs.getBytesLength(CFG_KEY);
  if (!n || n>CFG_MAX) return false;
  std::unique_ptr<char[]> raw(new(std::nothrow) char[n+1]);
  if (!raw || prefs.getBytes(CFG_KEY,raw.get(),n)!=n) return false;
  raw[n]='\0';
  JsonDocument doc;
  if (deserializeJson(doc,raw.get(),n)) return false;
  return loadFields(doc.as<JsonObjectConst>());
}
bool initConfig() {
  if (!nvsOK) { Serial.println("CONFIG: NVS unavailable; network disabled"); return false; }
  const bool exists=prefs.isKey(CFG_KEY);
#if defined(YARUMO_HAS_SECRETS)
  if (!exists || YARUMO_REPLACE_CONFIG) {
    JsonDocument doc;
    doc["schema"]=1;
    doc["ssid"]=YARUMO_WIFI_SSID;
    doc["wifi"]=YARUMO_WIFI_PASS;
    doc["ota"]=YARUMO_OTA_PASS;
    doc["web"]=YARUMO_WEB_PASS;
    doc["token"]=YARUMO_CLOUD_TOKEN;
    doc["ca"]=YARUMO_CLOUD_CA;
    if (loadFields(doc.as<JsonObjectConst>())) {
      String encoded;
      serializeJson(doc,encoded);
      const size_t n=encoded.length();
      if (n && n<=CFG_MAX && prefs.putBytes(CFG_KEY,encoded.c_str(),n)==n &&
          readStoredConfig()) {
        Serial.println("CONFIG: saved to NVS");
        return true;
      }
      Serial.println("CONFIG: could not save credentials");
    } else Serial.println("CONFIG: invalid secrets.h");
    // Preserve prior usable configuration if provisioning validation fails.
    if(exists && readStoredConfig()){Serial.println("CONFIG: previous NVS configuration loaded");return true;}
    return false;
  }
#endif
  if (exists && readStoredConfig()) {
    Serial.println("CONFIG: loaded from NVS");
    return true;
  }
  Serial.println("CONFIG: missing or invalid; provision with secrets.h over USB");
  return false;
}

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
</style></head><body><header><h1>Yarumo IoT</h1><p>ESP32 Relay X4 · V0.3.1 · Panel local</p></header><main>
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


struct CloudSample { char json[768]; };
struct CloudOrder { char id[37]; char action[8]; uint8_t relay; uint32_t ms; uint32_t start; uint32_t ttl; };
struct CloudAck { char id[37]; bool executed; };
QueueHandle_t sampleQueue=nullptr, orderQueue=nullptr, ackQueue=nullptr;
std::atomic<bool> cloudPaused{false};
bool cloudReady=false;
uint32_t sampleAt=0;
bool validHexToken() {
  if(strlen(CLOUD_TOKEN)!=64) return false;
  for(const char* p=CLOUD_TOKEN; *p; ++p) if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f'))) return false;
  return true;
}
bool validUuid(const char* id) {
  if(strlen(id)!=36) return false;
  for(uint8_t i=0;i<36;i++){
    if(i==8||i==13||i==18||i==23){if(id[i]!='-')return false;}
    else if(!isxdigit((unsigned char)id[i]))return false;
  }
  return true;
}
void cloudTask(void*) {
  CloudAck pending={};
  bool awaiting=false;
  for(;;) {
    CloudAck fresh={};
    if(xQueueReceive(ackQueue,&fresh,0)==pdTRUE){pending=fresh;awaiting=false;}
    if(!cloudPaused.load()&&!awaiting&&WiFi.status()==WL_CONNECTED&&time(nullptr)>1700000000){
      CloudSample sample={};
      if(xQueuePeek(sampleQueue,&sample,0)==pdTRUE){
        JsonDocument body;
        JsonDocument data;
        if(!deserializeJson(data,sample.json)){
          body["snapshot"]=data.as<JsonObjectConst>();
          if(pending.id[0]){body["ack"]["id"]=pending.id;body["ack"]["state"]=pending.executed?"executed":"rejected";}
          String out;serializeJson(body,out);
          WiFiClientSecure tls;tls.setCACert(CLOUD_CA);tls.setHandshakeTimeout(5);
          HTTPClient http;http.setConnectTimeout(3000);http.setTimeout(3500);
          if(http.begin(tls,CLOUD_URL)){
            http.addHeader("Content-Type","application/json");
            http.addHeader("Authorization",String("Bearer ")+CLOUD_TOKEN);
            const uint32_t started=millis();
            int rc=http.POST(out);
            if(rc==200){
              // Server does not retry delivery. If response is lost, outcome stays unknown.
              String response=http.getString();
              JsonDocument reply;
              if(response.length()<=2048&&!deserializeJson(reply,response)){
                pending={};
                JsonObjectConst cmd=reply["command"].as<JsonObjectConst>();
                if(!cmd.isNull()){
                  CloudOrder o={}; const char* id=cmd["id"]|"";
                  const char* action=cmd["action"]|"";
                  uint32_t ttl=cmd["ttl_ms"]|0U,ms=cmd["ms"]|0U;
                  int relay=cmd["relay"]|-1;
                  if(validUuid(id)&&strlen(action)<sizeof(o.action)){
                    strlcpy(o.id,id,sizeof(o.id));strlcpy(o.action,action,sizeof(o.action));
                    o.relay=(relay>=0&&relay<=4)?relay:255;o.ms=ms;
                    o.start=started;o.ttl=ttl<=10000?ttl:0;
                    // Deadline is conservatively measured from request start, not receipt.
                    if(!cloudPaused.load()&&xQueueSend(orderQueue,&o,0)==pdTRUE) awaiting=true;
                    else {strlcpy(pending.id,id,sizeof(pending.id));pending.executed=false;}
                  }
                }
              }
            }
            http.end();
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2500));
  }
}
void startCloud() {
  if(strncmp(CLOUD_URL,"https://",8)||!validHexToken()||!strstr(CLOUD_CA,"-----BEGIN CERTIFICATE-----")){
    Serial.println("Cloud disabled: missing token or root CA in persistent configuration");return;
  }
  sampleQueue=xQueueCreate(1,sizeof(CloudSample));orderQueue=xQueueCreate(1,sizeof(CloudOrder));
  ackQueue=xQueueCreate(1,sizeof(CloudAck));
  if(!sampleQueue||!orderQueue||!ackQueue){Serial.println("ERR cloud queues");return;}
  configTime(0,0,"pool.ntp.org","time.nist.gov");
  cloudReady=xTaskCreate(cloudTask,"yarumoCloud",16384,nullptr,1,nullptr)==pdPASS;
  Serial.println(cloudReady?"Cloud worker ready; TLS requires synchronized time":"ERR cloud task");
}
void tickCloud() {
  if(!cloudReady)return;
  CloudOrder o={};
  if(xQueueReceive(orderQueue,&o,0)==pdTRUE){
    bool ok=false;
    if(!updating&&WiFi.status()==WL_CONNECTED&&o.ttl>0&&millis()-o.start<o.ttl){
      if(!strcmp(o.action,"all")){allOff();ok=true;}
      else if(o.relay>=1&&o.relay<=4){
        if(!strcmp(o.action,"off")){offRelay(o.relay-1);ok=true;}
        else if(!strcmp(o.action,"pulse"))ok=startPulse(o.relay-1,o.ms);
      }
    }
    CloudAck ack={};strlcpy(ack.id,o.id,sizeof(ack.id));ack.executed=ok;xQueueOverwrite(ackQueue,&ack);
    Serial.printf("Cloud command %s: %s\n",o.id,ok?"executed":"rejected");
  }
  if(millis()-sampleAt>=1000){
    sampleAt=millis();
    String json=statusJson();CloudSample sample={};
    if(json.length()<sizeof(sample.json)){strlcpy(sample.json,json.c_str(),sizeof(sample.json));xQueueOverwrite(sampleQueue,&sample);}
  }
}

void tickWiFi() {
  if(!configReady) return;
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
  configReady=initConfig();
  if(!configReady){
    allOff();
    Serial.printf("Yarumo IoT %s | ID %s | configuration required\n",VERSION,deviceId);
    help();
    return;
  }
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.onStart([]() { updating = true; cloudPaused.store(true); allOff(); });
  ArduinoOTA.onEnd([]() { allOff(); });
  ArduinoOTA.onError([](ota_error_t e) {
    allOff(); updating = false; cloudPaused.store(false); Serial.printf("OTA error %u\n", (unsigned)e);
  });
  configureWeb();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastRetry = millis();
  startCloud();
  Serial.printf("\nYarumo IoT %s | ID %s\n", VERSION, deviceId);
  help();
}
void loop() {
  tickWiFi();
  if (otaReady) ArduinoOTA.handle();
  if (!updating) {
    tickCloud();
    if (webReady) web.handleClient();
    readSerial();
    if (millis() - lastReport >= reportRate) {
      lastReport = millis();
      report();
    }
  }
  delay(1);
}
