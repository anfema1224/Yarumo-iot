/*
 * Yarumo IoT V0.3 - ESP32 Relay X4 - banco de pruebas.
 * Arduino ESP32 Dev Module, particiones compatibles con OTA.
 * Credenciales persistentes en NVS; provisionar una vez con secrets.h local.
 * Probar con contactos de rele sin cargas. DI/AI maximo 3.3 V.
 */
#include <Arduino.h>
#include "YarumoPlc.h"
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

constexpr char VERSION[] = "0.5.0";
char WIFI_SSID[33] = {};
char WIFI_PASS[65] = {};
char OTA_PASS[129] = {};
constexpr char WEB_USER[] = "admin";
char WEB_PASS[129] = {};
constexpr char CLOUD_URL[] = "https://yarumo-iot.onrender.com/api/device/poll";
char CLOUD_TOKEN[65] = {};
char CLOUD_CA[4096] = {};
// Public trust anchors: GTS Root R4 and GlobalSign ECC Root R4.
// SHA-256 verified against https://pki.goog/repo/cp-cps-tls/6.10/GTS-CP-CPS-TLS.html
constexpr char DEFAULT_CLOUD_CA[] = R"PEM(-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIB3DCCAYOgAwIBAgINAgPlfvU/k/2lCSGypjAKBggqhkjOPQQDAjBQMSQwIgYD
VQQLExtHbG9iYWxTaWduIEVDQyBSb290IENBIC0gUjQxEzARBgNVBAoTCkdsb2Jh
bFNpZ24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMTIxMTEzMDAwMDAwWhcNMzgw
MTE5MDMxNDA3WjBQMSQwIgYDVQQLExtHbG9iYWxTaWduIEVDQyBSb290IENBIC0g
UjQxEzARBgNVBAoTCkdsb2JhbFNpZ24xEzARBgNVBAMTCkdsb2JhbFNpZ24wWTAT
BgcqhkjOPQIBBggqhkjOPQMBBwNCAAS4xnnTj2wlDp8uORkcA6SumuU5BwkWymOx
uYb4ilfBV85C+nOh92VC/x7BALJucw7/xyHlGKSq2XE/qNS5zowdo0IwQDAOBgNV
HQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUVLB7rUW44kB/
+wpu+74zyTyjhNUwCgYIKoZIzj0EAwIDRwAwRAIgIk90crlgr/HmnKAWBVBfw147
bmF0774BxL4YSFlhgjICICadVGNA3jdgUM/I2O2dgq43mLyjj0xMqTQrbO/7lZsm
-----END CERTIFICATE-----
)PEM";
bool configReady = false;
// Simulation lock remains latched until reboot; no real output from the PLC.
bool simulationLock=false;
bool physicalLock=false, physicalInstalled=false;
char plcRevision[37]={};
yarumo::Engine plcPhysical;
// PLC ownership and heartbeat are protected by relayLock. Only loop task calls the engine.
bool plcActive=false, plcWatchFault=false;
uint32_t plcHeartbeat=0;
constexpr uint32_t PLC_WATCH_MS=250;
yarumo::Engine plcSim;
yarumo::Inputs simInputs;
uint32_t simLastScan=0;
uint8_t simPreset=0;
constexpr char HOSTNAME[] = "Yarumo-X4";
constexpr uint8_t DI[] = {13, 14, 16, 17, 19, 27};
constexpr uint8_t AI[] = {34, 35, 36, 39};
constexpr uint8_t DO_PIN[] = {32, 33, 25, 26};
constexpr uint8_t RELAY_ON = HIGH, RELAY_OFF = LOW;
constexpr uint32_t MAX_PULSE_MS = 86400000; // 24 h; < INT32_MAX, safe across millis rollover.
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
bool relayTimed[4] = {}, cloudHold[4] = {};
std::atomic<uint32_t> lastCloudOK{0};
constexpr uint32_t CLOUD_LOSS_MS = 60000;
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
// Explicit buffer size avoids Arduino sketch auto-prototype issues with array-reference templates.
bool copyField(JsonObjectConst obj, const char* name, char* dest, size_t capacity) {
  if (!obj[name].is<const char*>()) return false;
  const char* value = obj[name].as<const char*>();
  if (capacity == 0 || strlen(value) >= capacity) return false;
  strlcpy(dest, value, capacity);
  return true;
}
bool loadFields(JsonObjectConst obj) {
  if (obj["schema"].as<int>() != 1) return false;
  if (!copyField(obj,"ssid",WIFI_SSID,sizeof(WIFI_SSID)) || !copyField(obj,"wifi",WIFI_PASS,sizeof(WIFI_PASS)) ||
      !copyField(obj,"ota",OTA_PASS,sizeof(OTA_PASS)) || !copyField(obj,"web",WEB_PASS,sizeof(WEB_PASS)) ||
      !copyField(obj,"token",CLOUD_TOKEN,sizeof(CLOUD_TOKEN)) || !copyField(obj,"ca",CLOUD_CA,sizeof(CLOUD_CA))) return false;
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
  relayTimed[i] = false; cloudHold[i] = false;
}
void offRelay(uint8_t i) {
  if (relayLock) xSemaphoreTake(relayLock, portMAX_DELAY);
  if(physicalLock){plcActive=false;plcPhysical.stop();for(uint8_t j=0;j<4;j++)offLocked(j);}
  else offLocked(i);
  if (relayLock) xSemaphoreGive(relayLock);
}
void allOff() {
  if (relayLock) xSemaphoreTake(relayLock, portMAX_DELAY);
  plcActive=false;plcPhysical.stop();
  for (uint8_t i = 0; i < 4; ++i) offLocked(i);
  if (relayLock) xSemaphoreGive(relayLock);
}
bool cloudFresh() { const uint32_t t=lastCloudOK.load(); return uint32_t(millis()-t)<CLOUD_LOSS_MS; }
bool startPulse(uint8_t i, uint32_t ms, bool remote) {
  if (simulationLock || physicalLock || !timerReady || !relayLock || i >= 4 || ms < 50 ||
      ms > MAX_PULSE_MS || updating || WiFi.status() != WL_CONNECTED) return false;
  xSemaphoreTake(relayLock, portMAX_DELAY);
  // Do not extend a running pulse; turn it off first if needed.
  if (relayOn[i] || WiFi.status()!=WL_CONNECTED || (remote && !cloudFresh())) { xSemaphoreGive(relayLock); return false; }
  pulseEnd[i] = millis() + ms;
  relayTimed[i] = true; cloudHold[i] = remote;
  relayOn[i] = true;
  digitalWrite(DO_PIN[i], RELAY_ON);
  xSemaphoreGive(relayLock);
  return true;
}
bool startHold(uint8_t i,bool remote) {
  if(simulationLock||physicalLock||!timerReady||!relayLock||i>=4||updating||WiFi.status()!=WL_CONNECTED) return false;
  xSemaphoreTake(relayLock,portMAX_DELAY);
  if(relayOn[i] || WiFi.status()!=WL_CONNECTED ||
      (remote && !cloudFresh())){
    xSemaphoreGive(relayLock);return false;
  }
  relayTimed[i]=false;cloudHold[i]=remote;relayOn[i]=true;
  digitalWrite(DO_PIN[i],RELAY_ON);
  xSemaphoreGive(relayLock);return true;
}
void timerTask(void*) {
  for (;;) {
    xSemaphoreTake(relayLock, portMAX_DELAY);
    const uint32_t cloudOK = lastCloudOK.load();
    const uint32_t now = millis();
    const bool connected = WiFi.status() == WL_CONNECTED;
    if(plcActive){
      if(uint32_t(now-plcHeartbeat)>PLC_WATCH_MS){plcActive=false;plcWatchFault=true;for(uint8_t i=0;i<4;i++)offLocked(i);}
    }else for (uint8_t i = 0; i < 4; ++i)
      if (relayOn[i] && (!connected || (relayTimed[i] && (int32_t)(now - pulseEnd[i]) >= 0) || (cloudHold[i] && uint32_t(now-cloudOK)>=CLOUD_LOSS_MS)))
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
    remaining[i] = on[i] && relayTimed[i] && (int32_t)(pulseEnd[i] - now) > 0 ? pulseEnd[i] - now : 0;
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
  Serial.println("HELP | STATUS | PULSE <1-4> <50-86400000 ms> | OFF <1-4> | ALL OFF | RATE <250-60000 ms>");
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
      parseUInt(b, ms) && startPulse(i - 1, ms, false)) {
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
</style></head><body><header><h1>Yarumo IoT</h1><p>ESP32 Relay X4 · V0.5.0 · Panel local</p></header><main>
<p id="status" class="status" role="status">Conectando...</p><p id="info" class="muted"></p>
<h2>Entradas digitales</h2><div id="di" class="grid"></div>
<h2>Entradas analógicas</h2><div id="ai" class="grid"></div>
<p><a href="/plc">Simulador PLC</a> · <a href="/plc-physical">Control físico PLC (banco de pruebas)</a></p><h2>Relés y temporización</h2><p>Selecciona un pulso de 0,05 a 86400 segundos (24 h). No se prolonga un pulso activo.</p>
<div id="relays" class="grid"></div><button class="off" id="all" disabled>Apagar todos</button>
<footer>Banco de pruebas: sin cargas conectadas. El estado mostrado es la orden GPIO, no la posición física del contacto. Acceso únicamente en red local de confianza.</footer></main>
<script>
const diPins=[13,14,16,17,19,27],aiPins=[34,35,36,39],doPins=[32,33,25,26];
const $=id=>document.getElementById(id);
let online=false,busy=false,states=[0,0,0,0],polling=false,simLocked=false;
function init(){
diPins.forEach((p,i)=>{$('di').insertAdjacentHTML('beforeend','<div class="card">DI'+(i+1)+' · GPIO'+p+'<div class="val" id="d'+i+'">--</div></div>')});
aiPins.forEach((p,i)=>{$('ai').insertAdjacentHTML('beforeend','<div class="card">AI'+(i+1)+' · GPIO'+p+'<div class="val" id="a'+i+'">--</div><div class="muted" id="raw'+i+'"></div></div>')});
doPins.forEach((p,i)=>{$('relays').insertAdjacentHTML('beforeend','<div class="card">Relé '+(i+1)+' · GPIO'+p+'<div class="val" id="r'+i+'">--</div><div class="muted" id="t'+i+'"></div><label>Tiempo (s)<input id="sec'+i+'" type="number" min=".05" max="86400" step=".05" value="2"></label><button id="hold'+i+'" disabled>Encender</button> <button id="go'+i+'" disabled>Pulso</button> <button class="off" id="off'+i+'" disabled>Apagar</button></div>');$('hold'+i).onclick=()=>send('on',i+1);$('go'+i).onclick=()=>pulse(i);$('off'+i).onclick=()=>send('off',i+1)});
$('all').onclick=()=>send('all',0);refresh();setInterval(refresh,750);}
function controls(){doPins.forEach((p,i)=>{$('hold'+i).disabled=!online||busy||simLocked||!!states[i];$('go'+i).disabled=!online||busy||simLocked||!!states[i];$('off'+i).disabled=!online||busy});$('all').disabled=!online||busy;}
async function refresh(){if(polling||busy)return;polling=true;try{const r=await fetch('/api/status',{cache:'no-store',signal:AbortSignal.timeout(2500)});if(!r.ok)throw Error('HTTP '+r.status);const s=await r.json();online=true;states=s.relay_cmd;simLocked=s.simulation_lock===true||s.plc_locked===true;$('status').textContent=simLocked?'Modo PLC: activación manual bloqueada hasta reinicio':'Conectado';$('info').textContent='ID '+s.device+' · Firmware '+s.version;
s.di.forEach((v,i)=>{$('d'+i).textContent=v?'HIGH':'LOW';$('d'+i).className='val '+(v?'on':'idle')});s.ai_mv.forEach((v,i)=>{$('a'+i).textContent=v+' mV';$('raw'+i).textContent='ADC '+s.ai_raw[i]+' / 4095'});states.forEach((v,i)=>{$('r'+i).textContent=v?'ON':'OFF';$('r'+i).className='val '+(v?'on':'idle');$('t'+i).textContent=v?(s.remaining_ms[i]>0?(s.remaining_ms[i]/1000).toFixed(1)+' s restantes':'Encendido continuo'):'Apagado'});
}catch(e){online=false;$('status').textContent='Sin conexión o lectura no disponible';}finally{polling=false;controls();}}
function pulse(i){const v=Number($('sec'+i).value);if(!Number.isFinite(v)||v<.05||v>86400){alert('Tiempo entre 0,05 y 86400 s');return;}send('pulse',i+1,Math.round(v*1000));}
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
  j += "],\"simulation_lock\":"; j += simulationLock?"true":"false";
  j += ",\"plc_locked\":"; j += physicalLock?"true":"false";
  j += ",\"plc_transfer\":true,\"plc_revision\":\"";j+=plcRevision;j+="\",\"plc_mode\":\"";
  bool active=false,fault=false;if(relayLock){xSemaphoreTake(relayLock,portMAX_DELAY);active=plcActive;fault=plcWatchFault;xSemaphoreGive(relayLock);}
  j+=fault?"FAULT":active?"RUN":"STOP";j+="\"";
  j += ",\"hold_supported\":true,\"max_pulse_ms\":"; j += String(MAX_PULSE_MS);
  j += ",\"remaining_ms\":[";
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
  if (action=="on") { if(startHold(n-1,false)) web.send(200,"text/plain","OK"); else web.send(409,"text/plain","Relay busy or not available"); return; }
  if (action=="pulse" && parseUInt(web.arg("ms").c_str(),ms) && ms>=50 && ms<=MAX_PULSE_MS) {
    if (startPulse(n-1,ms,false)) web.send(200,"text/plain","OK");
    else web.send(409,"text/plain","Relay busy or not available");
    return;
  }
  web.send(400,"text/plain","Invalid command");
}

const char PLC_PAGE[] PROGMEM=R"PLCHTML(<!doctype html><html lang="es"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Yarumo PLC — Simulación</title><style>body{font:16px system-ui;margin:20px auto;padding:0 14px;max-width:750px;background:#eef3f7;color:#153d55}section{background:white;padding:16px;border-radius:10px;margin:12px 0}button,select{font:inherit;padding:10px;margin:4px}button:disabled{opacity:.4}.lamp{font-weight:bold;padding:6px 12px;border:1px solid #aaa;border-radius:6px;display:inline-block;margin:4px}.on{background:#c9ead5}.off{background:#eee}</style><h1>Yarumo PLC · simulación</h1><p><a href="/">Panel local</a></p><section><strong>Salidas virtuales exclusivamente.</strong><p>Al iniciar se apagan los relés físicos y quedan bloqueados hasta reiniciar el ESP32. No conectes cargas durante las pruebas.</p><label>Programa <select id="preset"><option value="1">TON: DI1 → 2 s → DO1</option><option value="2">RS: DI1 marcha / DI2 reset → DO1</option><option value="3">CTU: 3 flancos DI1 / DI2 reset → DO1</option></select></label><button id="run">Iniciar simulación</button><button id="stop">STOP</button><p id="state">Conectando…</p><p id="msg" role="status"></p></section><section><h2>Entradas virtuales</h2><button id="d1">DI1</button><button id="d2">DI2</button><p id="in"></p></section><section><h2>Salidas virtuales</h2><div id="out"></div></section><script>
const $=id=>document.getElementById(id);let snapshot=null,busy=false,polling=false;
function render(){const s=snapshot;if(!s)return;$('state').textContent=s.mode+' · '+(s.lock?'Relés físicos bloqueados':'Relés físicos en modo normal')+(s.error?' · Fallo '+s.error:'');$('in').textContent='DI1 = '+Number(s.di[0])+' · DI2 = '+Number(s.di[1]);$('d1').disabled=busy||s.mode!=='RUN';$('d2').disabled=busy||s.mode!=='RUN';$('run').disabled=busy;$('stop').disabled=busy||!s.lock;$('out').replaceChildren(...s.out.map((v,i)=>{const e=document.createElement('span');e.className='lamp '+(v?'on':'off');e.textContent='DO'+(i+1)+' '+(v?'ON':'OFF');return e;}));}
async function refresh(){if(busy||polling)return;polling=true;try{const r=await fetch('/api/plc',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);snapshot=await r.json();render();}catch(e){snapshot=null;$('state').textContent='Sin conexión';['run','stop','d1','d2'].forEach(k=>$(k).disabled=true);}finally{polling=false;}}
async function act(action,fields={}){if(busy)return;busy=true;render();try{const r=await fetch('/api/plc',{method:'POST',headers:{'X-Yarumo-Control':'1'},body:new URLSearchParams({action,...fields})});if(!r.ok)throw Error(await r.text());snapshot=await r.json();$('msg').textContent='Orden aplicada';}catch(e){$('msg').textContent=e.message;}finally{busy=false;render();refresh();}}
$('run').onclick=()=>act('run',{preset:$('preset').value});$('stop').onclick=()=>act('stop');$('d1').onclick=()=>act('input',{n:'1'});$('d2').onclick=()=>act('input',{n:'2'});['run','stop','d1','d2'].forEach(k=>$(k).disabled=true);refresh();setInterval(refresh,250);
</script></html>)PLCHTML";
String simJson(){
  JsonDocument d;
  auto mode=plcSim.mode();
  d["mode"]=mode==yarumo::Mode::Running?"RUN":mode==yarumo::Mode::Fault?"FAULT":"STOP";
  d["lock"]=simulationLock;
  d["preset"]=simPreset;
  d["error"]=(int)plcSim.error();
  JsonArray di=d["di"].to<JsonArray>();for(bool v:simInputs.di)di.add(v);
  JsonArray out=d["out"].to<JsonArray>();for(bool v:plcSim.outputs().relay)out.add(v);
  String result;serializeJson(d,result);return result;
}
void simStop(){plcSim.stop();simInputs=yarumo::Inputs{};simPreset=0;}
void simRun(uint8_t preset){
  // Must only be called on the loop task: physical activation also runs on that task.
  simulationLock=true;allOff();simStop();
  yarumo::Program p;
  p.count=3;p.nodes[0]={yarumo::Op::DI,0,0,0};p.nodes[1]={yarumo::Op::DI,1,0,0};
  if(preset==1)p.nodes[2]={yarumo::Op::TON,0,0,2000};
  else if(preset==2)p.nodes[2]={yarumo::Op::RS,0,1,0};
  else p.nodes[2]={yarumo::Op::CTU,0,1,3};
  p.output[0]=2;
  simPreset=preset;simLastScan=millis();
  if(plcSim.install(p))plcSim.arm(simLastScan);
}
void tickSim(){
  if(plcSim.mode()!=yarumo::Mode::Running)return;
  const uint32_t now=millis();
  if(uint32_t(now-simLastScan)>=20){simLastScan=now;plcSim.scan(now,simInputs);}
}
void webSim(){
  if(!webAuth())return;
  if(web.header("X-Yarumo-Control")!="1"){web.send(403,"text/plain","Missing control header");return;}
  String action=web.arg("action");uint32_t v=0;
  if(action=="run"&&physicalLock){web.send(409,"text/plain","Reinicia para cambiar de modo PLC");return;}
  if(action=="run"&&parseUInt(web.arg("preset").c_str(),v)&&v>=1&&v<=3)simRun(v);
  else if(action=="stop")simStop();
  else if(action=="input"&&plcSim.mode()==yarumo::Mode::Running&&parseUInt(web.arg("n").c_str(),v)&&v>=1&&v<=2)simInputs.di[v-1]=!simInputs.di[v-1];
  else {web.send(400,"text/plain","Invalid simulation command");return;}
  web.send(200,"application/json",simJson());
}


const char PHYS_PAGE[] PROGMEM=R"PHYSHTML(<!doctype html><html lang="es"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>PLC físico · Yarumo</title><style>body{font:16px system-ui;max-width:800px;margin:20px auto;padding:0 16px;background:#eef3f7;color:#183d55}section{background:white;padding:16px;border-radius:8px;margin:12px 0}button,input{font:inherit;padding:10px;margin:4px}button:disabled{opacity:.4}#stop{background:#a33;color:white}.warn{background:#fff3d7;padding:10px}pre{white-space:pre-wrap}</style><h1>Yarumo PLC · control físico experimental</h1><p><a href="/">Panel local</a></p><p class="warn">Solo banco de pruebas sin cargas. Las salidas son relés REALES. DI usa nivel GPIO crudo (HIGH en reposo con pull-up). No conectar tensiones externas. STOP no sustituye un paro de emergencia cableado.</p><section><label>Programa JSON exportado del editor FBD <input id="file" type="file" accept=".json,application/json"></label><button id="install">Instalar en STOP</button><button id="run">RUN físico</button><button id="stop">STOP / todos OFF</button><p>El modo manual queda bloqueado hasta reiniciar. El programa se conserva en memoria NVS, pero siempre inicia en STOP. Sin Wi-Fi, el PLC continúa; para detenerlo sin red usa desconexión segura de alimentación.</p><p id="msg" role="status"></p></section><section><h2>Estado del PLC</h2><pre id="state">Conectando…</pre></section><script>
const $=id=>document.getElementById(id);let busy=false,s=null;
function render(){if(!s)return;$('state').textContent='Modo: '+s.mode+' · Programa: '+(s.installed?'instalado':'ninguno')+'\nDI: '+s.di.join('  ')+'\nAI: '+s.ai.join('  ')+'\nDO: '+s.out.join('  ')+'\nFallo de ciclo: '+(s.fault?'sí':'no');$('run').disabled=busy||!s.installed||s.mode==='RUN'||s.simulation;$('install').disabled=busy||s.mode==='RUN'||s.simulation;$('stop').disabled=busy;}
async function refresh(){if(busy)return;try{const r=await fetch('/api/plc-physical',{cache:'no-store',signal:AbortSignal.timeout(2500)});if(!r.ok)throw Error('HTTP '+r.status);s=await r.json();render();}catch(e){s=null;$('state').textContent='Sin comunicación. El PLC físico puede seguir ejecutándose.';$('run').disabled=$('install').disabled=true;}}
async function send(action,program=''){if(busy)return;busy=true;render();try{const r=await fetch('/api/plc-physical',{method:'POST',headers:{'X-Yarumo-Control':'1'},body:new URLSearchParams({action,program}),signal:AbortSignal.timeout(5000)});if(!r.ok)throw Error(await r.text());s=await r.json();$('msg').textContent='Orden aplicada: '+action;}catch(e){$('msg').textContent=e.message;}finally{busy=false;render();refresh();}}
$('install').onclick=async()=>{try{const f=$('file').files[0];if(!f||f.size>100000)throw Error('Selecciona un JSON FBD válido');const p=JSON.parse(await f.text()).program;if(!p)throw Error('Falta programa compilado');await send('install',JSON.stringify({schema:p.schema,count:p.count,nodes:p.nodes,output:p.output}));}catch(e){$('msg').textContent=e.message;}};
$('run').onclick=()=>{if(confirm('Confirmo banco sin cargas y autorización para activar relés reales. ¿RUN?'))send('run');};$('stop').onclick=()=>send('stop');$('run').disabled=$('install').disabled=true;refresh();setInterval(refresh,500);
</script></html>)PHYSHTML";
bool physicalRunning(){
  if(!relayLock)return false;
  xSemaphoreTake(relayLock,portMAX_DELAY);bool run=plcActive;xSemaphoreGive(relayLock);return run;
}
bool decodeProgram(const String& raw,yarumo::Program& p){
  if(!raw.length()||raw.length()>8192)return false;
  JsonDocument doc;if(deserializeJson(doc,raw,DeserializationOption::NestingLimit(8)))return false;
  JsonObjectConst o=doc.as<JsonObjectConst>();if(o.isNull()||!o["schema"].is<int>()||o["schema"].as<int>()!=1||!o["count"].is<int>())return false;
  int count=o["count"].as<int>();JsonArrayConst ns=o["nodes"].as<JsonArrayConst>(),out=o["output"].as<JsonArrayConst>();
  if(count<1||count>yarumo::MAX_NODES||ns.size()!=size_t(count)||out.size()!=4)return false;
  const char* names[]={"DI","AI_GE","AI_LE","CONST","NOT","AND","OR","TON","TOF","TP","RS","CTU","CTD"};
  p=yarumo::Program{};p.count=count;
  for(int i=0;i<count;i++){
    JsonObjectConst n=ns[i].as<JsonObjectConst>();
    if(!n["op"].is<const char*>()||!n["a"].is<int>()||!n["b"].is<int>()||!n["p"].is<uint32_t>())return false;
    int a=n["a"].as<int>(),b=n["b"].as<int>();uint32_t par=n["p"].as<uint32_t>();
    if(a<0||a>255||b<0||b>255||par>yarumo::MAX_TIME)return false;
    int op=-1;for(unsigned k=0;k<sizeof(names)/sizeof(names[0]);k++)if(!strcmp(n["op"].as<const char*>(),names[k])){op=k;break;}
    if(op<0)return false;
    p.nodes[i]={static_cast<yarumo::Op>(op),static_cast<uint8_t>(a),static_cast<uint8_t>(b),par};
  }
  for(int i=0;i<4;i++){if(!out[i].is<int>())return false;int v=out[i].as<int>();if(v< -1||v>=count)return false;p.output[i]=v;}
  return yarumo::Engine::valid(p);
}
bool revisionValid(const char* id){
 if(!id||strlen(id)!=36)return false;
 for(uint8_t i=0;i<36;i++){if(i==8||i==13||i==18||i==23){if(id[i]!='-')return false;}else if(!isxdigit((unsigned char)id[i]))return false;}
 return true;
}
void revisionFrom(const String& raw){
 plcRevision[0]=0;JsonDocument d;if(deserializeJson(d,raw))return;
 const char* id=d["revision"]|"";if(revisionValid(id))strlcpy(plcRevision,id,sizeof(plcRevision));
}
bool restorePhysical(){
  if(!nvsOK)return false;
  String raw=prefs.getString("plc_v1");if(!raw.length()||raw.length()>8192)return false;
  yarumo::Program p;if(!decodeProgram(raw,p))return false;
  physicalInstalled=plcPhysical.install(p);if(physicalInstalled)revisionFrom(raw);return physicalInstalled;
}
bool installPhysical(const String& raw){
  if(simulationLock||physicalRunning()||!nvsOK||!timerReady)return false;
  yarumo::Program p;if(!decodeProgram(raw,p))return false;
  physicalLock=true;allOff();
  if(prefs.putString("plc_v1",raw)!=raw.length()||prefs.getString("plc_v1")!=raw){physicalInstalled=false;plcRevision[0]=0;return false;}
  physicalInstalled=plcPhysical.install(p);if(physicalInstalled)revisionFrom(raw);return physicalInstalled;
}
bool armPhysical(){
  if(simulationLock||!physicalInstalled||!timerReady||!relayLock||updating||physicalRunning())return false;
  physicalLock=true;allOff();const uint32_t now=millis();
  if(!plcPhysical.arm(now))return false;
  xSemaphoreTake(relayLock,portMAX_DELAY);plcWatchFault=false;plcHeartbeat=now;plcActive=true;xSemaphoreGive(relayLock);return true;
}
void tickPhysical(){
  if(!physicalRunning())return;
  const uint32_t now=millis();
  xSemaphoreTake(relayLock,portMAX_DELAY);uint32_t prev=plcHeartbeat;xSemaphoreGive(relayLock);
  if(uint32_t(now-prev)<20)return;
  yarumo::Inputs in;for(uint8_t i=0;i<6;i++)in.di[i]=digitalRead(DI[i])==HIGH;
  for(uint8_t i=0;i<4;i++)in.ai[i]=analogRead(AI[i]);
  const uint32_t sampled=millis();bool ok=plcPhysical.scan(sampled,in);
  xSemaphoreTake(relayLock,portMAX_DELAY);
  if(!ok||!plcActive||uint32_t(millis()-prev)>PLC_WATCH_MS){
    plcActive=false;plcWatchFault=true;plcPhysical.stop();for(uint8_t i=0;i<4;i++)offLocked(i);
  }else{
    for(uint8_t i=0;i<4;i++){const bool q=plcPhysical.outputs().relay[i];if(relayOn[i]!=q)digitalWrite(DO_PIN[i],q?RELAY_ON:RELAY_OFF);relayOn[i]=q;relayTimed[i]=cloudHold[i]=false;pulseEnd[i]=0;}
    plcHeartbeat=sampled;
  }
  xSemaphoreGive(relayLock);
}
String physicalJson(){
  JsonDocument d;bool active=false,fault=false;bool on[4]={};
  if(relayLock){xSemaphoreTake(relayLock,portMAX_DELAY);active=plcActive;fault=plcWatchFault;for(uint8_t i=0;i<4;i++)on[i]=relayOn[i];xSemaphoreGive(relayLock);}
  d["mode"]=fault?"FAULT":active?"RUN":"STOP";d["fault"]=fault;d["installed"]=physicalInstalled;d["simulation"]=simulationLock;d["revision"]=plcRevision;
  JsonArray di=d["di"].to<JsonArray>();for(uint8_t p:DI)di.add(digitalRead(p));
  JsonArray ai=d["ai"].to<JsonArray>();for(uint8_t p:AI)ai.add(analogRead(p));
  JsonArray out=d["out"].to<JsonArray>();for(bool q:on)out.add(q?1:0);
  String s;serializeJson(d,s);return s;
}
void webPhysical(){
  if(!webAuth())return;
  if(web.header("X-Yarumo-Control")!="1"){web.send(403,"text/plain","Missing control header");return;}
  String action=web.arg("action");bool ok=false;
  if(action=="stop"){allOff();ok=true;}
  else if(action=="install"){JsonDocument d;if(!deserializeJson(d,web.arg("program"))){d.remove("revision");String raw;serializeJson(d,raw);ok=installPhysical(raw);}}
  else if(action=="run")ok=armPhysical();
  if(!ok){web.send(409,"text/plain","Programa invalido o modo no disponible; STOP antes de instalar.");return;}
  web.send(200,"application/json",physicalJson());
}

void configureWeb() {
  webEnabled = strcmp(WEB_PASS,"TU_CLAVE_WEB")!=0 && strlen(WEB_PASS)>=12;
  if (!webEnabled) { Serial.println("WEB disabled: configure WEB_PASS (12+ characters)"); return; }
  const char* headers[]={"X-Yarumo-Control"};
  web.collectHeaders(headers,1);
  web.on("/",HTTP_GET,[](){ if(webAuth()) web.send_P(200,"text/html; charset=utf-8",PAGE); });
  web.on("/api/status",HTTP_GET,[](){ if(webAuth()) web.send(200,"application/json",statusJson()); });
  web.on("/api/relay",HTTP_POST,webRelay);
  web.on("/plc",HTTP_GET,[](){if(webAuth())web.send_P(200,"text/html; charset=utf-8",PLC_PAGE);});
  web.on("/api/plc",HTTP_GET,[](){if(webAuth())web.send(200,"application/json",simJson());});
  web.on("/api/plc",HTTP_POST,webSim);
  web.on("/plc-physical",HTTP_GET,[](){if(webAuth())web.send_P(200,"text/html; charset=utf-8",PHYS_PAGE);});
  web.on("/api/plc-physical",HTTP_GET,[](){if(webAuth())web.send(200,"application/json",physicalJson());});
  web.on("/api/plc-physical",HTTP_POST,webPhysical);
  web.onNotFound([](){ web.send(404,"text/plain","Not found"); });
}


struct CloudSample { char json[1024]; };
struct CloudOrder { char id[37]; char action[8]; uint8_t relay; uint32_t ms; uint32_t start; uint32_t ttl; char program[4096]; };
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
              if(response.length()<=6144&&!deserializeJson(reply,response)){
                lastCloudOK.store(millis());
                pending={};
                JsonObjectConst cmd=reply["command"].as<JsonObjectConst>();
                if(!cmd.isNull()){
                  static CloudOrder o; o={}; const char* id=cmd["id"]|"";
                  const char* action=cmd["action"]|"";
                  uint32_t ttl=cmd["ttl_ms"]|0U,ms=cmd["ms"]|0U;
                  int relay=cmd["relay"]|-1;
                  if(validUuid(id)&&strlen(action)<sizeof(o.action)){
                    strlcpy(o.id,id,sizeof(o.id));strlcpy(o.action,action,sizeof(o.action));
                    o.relay=(relay>=0&&relay<=4)?relay:255;o.ms=ms;
                    o.start=started;o.ttl=ttl<=10000?ttl:0;
                    if(!strcmp(action,"plcset")){
                      const char* rev=cmd["revision"]|"";
                      JsonDocument transfer;transfer.set(cmd["program"]);transfer["revision"]=rev;
                      if(!revisionValid(rev)||!transfer.is<JsonObject>()||measureJson(transfer)>=sizeof(o.program))o.ttl=0;
                      else serializeJson(transfer,o.program,sizeof(o.program));
                    }
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
  if(!CLOUD_CA[0]) strlcpy(CLOUD_CA,DEFAULT_CLOUD_CA,sizeof(CLOUD_CA));
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
  static CloudOrder o;
  if(xQueueReceive(orderQueue,&o,0)==pdTRUE){
    bool ok=false;
    if(!updating&&WiFi.status()==WL_CONNECTED&&o.ttl>0&&millis()-o.start<o.ttl){
      if(!strcmp(o.action,"all")){allOff();ok=true;}
      else if(!strcmp(o.action,"plcset")&&!simulationLock&&!physicalRunning()){
        bool clear=true;if(relayLock){xSemaphoreTake(relayLock,portMAX_DELAY);for(bool v:relayOn)if(v)clear=false;xSemaphoreGive(relayLock);}else clear=false;
        if(clear&&o.program[0])ok=installPhysical(String(o.program));
      }
      else if(o.relay>=1&&o.relay<=4){
        if(!strcmp(o.action,"off")){offRelay(o.relay-1);ok=true;}
        else if(!strcmp(o.action,"on")&&o.ms==0)ok=startHold(o.relay-1,true);
        else if(!strcmp(o.action,"pulse"))ok=startPulse(o.relay-1,o.ms,true);
      }
    }
    String freshJson=statusJson();CloudSample freshSample={};
    if(freshJson.length()<sizeof(freshSample.json)){strlcpy(freshSample.json,freshJson.c_str(),sizeof(freshSample.json));xQueueOverwrite(sampleQueue,&freshSample);}
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
    if(!physicalRunning()){allOff();Serial.println("WiFi lost; manual relays off");}
    else Serial.println("WiFi lost; physical PLC remains autonomous");
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
  if(restorePhysical())Serial.println("PLC: program restored in STOP; manual mode available until explicit RUN");
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.onStart([]() { updating = true; cloudPaused.store(true); simStop(); allOff(); });
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
    tickPhysical();
    tickSim();
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
