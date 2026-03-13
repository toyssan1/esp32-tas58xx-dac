#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include "tas58xx.h"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA 18
#endif
#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL 8
#endif
#ifndef PIN_I2S_BCLK
#define PIN_I2S_BCLK 11
#endif
#ifndef PIN_I2S_LRCLK
#define PIN_I2S_LRCLK 12
#endif
#ifndef PIN_I2S_DOUT
#define PIN_I2S_DOUT 10
#endif
#ifndef PIN_SD_SCK
#define PIN_SD_SCK 39
#endif
#ifndef PIN_SD_MISO
#define PIN_SD_MISO 40
#endif
#ifndef PIN_SD_MOSI
#define PIN_SD_MOSI 38
#endif
#ifndef PIN_SD_CS
#define PIN_SD_CS 41
#endif
#ifndef TAS58XX_ADDRESS
#define TAS58XX_ADDRESS 0x4C
#endif
#ifndef TAS58XX_GPIO_PDN
#define TAS58XX_GPIO_PDN 17
#endif

static const char *kSdMountPath = "/";

static const char *kPresetNames[] = {
    "Flat", "LF 60Hz", "LF 70Hz", "LF 80Hz", "LF 90Hz", "LF 100Hz", "LF 110Hz", "LF 120Hz", "LF 130Hz", "LF 140Hz", "LF 150Hz",
    "HF 60Hz", "HF 70Hz", "HF 80Hz", "HF 90Hz", "HF 100Hz", "HF 110Hz", "HF 120Hz", "HF 130Hz", "HF 140Hz", "HF 150Hz",
};
static constexpr size_t kPresetCount = sizeof(kPresetNames) / sizeof(kPresetNames[0]);

static Tas58xx amp(TAS58XX_ADDRESS);
static WebServer server(80);

static AudioFileSourceSD *mp3_file = nullptr;
static AudioGeneratorMP3 *mp3_decoder = nullptr;
static AudioOutputI2S *i2s_out = nullptr;

static bool sd_ready = false;
static int volume_percent = 50;
static int eq_preset_index = 0;
static int lf_preset_index = 0;  // 0=Flat, 1-10 = LF 60-150Hz
static int hf_preset_index = 0;  // 0=Flat, 11-20 = HF 60-150Hz
static String current_file;
static std::vector<String> cached_files;

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32 TAS58xx Controller</title>
<style>
body{font-family:Arial,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:16px}
.card{background:#1e293b;border:1px solid #334155;border-radius:10px;padding:14px;margin-bottom:14px}
button,select,input{width:100%;margin-top:8px;padding:8px}
.row{display:flex;gap:8px}.row>*{flex:1}
pre{background:#020617;border:1px solid #334155;padding:10px;border-radius:8px;overflow:auto}
</style></head><body>
<h2>ESP32-S3 MP3 + TAS58xx Web Control</h2>
<div class='card'>
<div class='row'><button id='play'>Start</button><button id='stop'>Stop</button></div>
<label>Volume <span id='vLabel'>50</span></label><input id='vol' type='range' min='0' max='100' value='50'>
<label>LF Crossover</label><select id='lfPreset'></select>
<label>HF Crossover</label><select id='hfPreset'></select>
<div>Current file: <span id='cur'>(none)</span></div>
</div>
<div class='card'><strong>SD mount path:</strong> <span id='mount'></span><div id='files'></div></div>
<div class='card'><pre id='state'>Loading...</pre></div>
<script>
const lfPresets=[{i:0,n:'Flat'},{i:1,n:'60Hz'},{i:2,n:'70Hz'},{i:3,n:'80Hz'},{i:4,n:'90Hz'},{i:5,n:'100Hz'},{i:6,n:'110Hz'},{i:7,n:'120Hz'},{i:8,n:'130Hz'},{i:9,n:'140Hz'},{i:10,n:'150Hz'}];
const hfPresets=[{i:0,n:'Flat'},{i:11,n:'60Hz'},{i:12,n:'70Hz'},{i:13,n:'80Hz'},{i:14,n:'90Hz'},{i:15,n:'100Hz'},{i:16,n:'110Hz'},{i:17,n:'120Hz'},{i:18,n:'130Hz'},{i:19,n:'140Hz'},{i:20,n:'150Hz'}];
const stateEl=document.getElementById('state');
const filesEl=document.getElementById('files');
const mountEl=document.getElementById('mount');
const curEl=document.getElementById('cur');
const volEl=document.getElementById('vol');
const vLabel=document.getElementById('vLabel');
const lfEl=document.getElementById('lfPreset');
const hfEl=document.getElementById('hfPreset');
lfPresets.forEach(p=>{const o=document.createElement('option');o.value=p.i;o.textContent=p.n;lfEl.appendChild(o);});
hfPresets.forEach(p=>{const o=document.createElement('option');o.value=p.i;o.textContent=p.n;hfEl.appendChild(o);});
async function j(url,opt){const r=await fetch(url,opt);const d=await r.json();if(!r.ok)throw new Error(d.error||`HTTP ${r.status}`);return d;}
function render(s){stateEl.textContent=JSON.stringify(s,null,2);volEl.value=s.volume;vLabel.textContent=s.volume;lfEl.value=s.lf_preset_index;hfEl.value=s.hf_preset_index;curEl.textContent=s.current_file||'(none)';}
async function refresh(){const s=await j('/api/state');render(s.state);const f=await j('/api/files');mountEl.textContent=f.sd_mount_path;filesEl.innerHTML='';if(!f.files.length){filesEl.textContent='No MP3 files found';return;}f.files.forEach(file=>{const b=document.createElement('button');b.textContent=file;b.onclick=async()=>{const r=await j('/api/play?file='+encodeURIComponent(file),{method:'POST'});render(r.state);};filesEl.appendChild(b);});}
document.getElementById('play').onclick=async()=>{const r=await j('/api/play',{method:'POST'});render(r.state);};
document.getElementById('stop').onclick=async()=>{const r=await j('/api/stop',{method:'POST'});render(r.state);};
volEl.oninput=()=>vLabel.textContent=volEl.value;
volEl.onchange=async()=>{const r=await j('/api/volume?value='+volEl.value,{method:'POST'});render(r.state);};
lfEl.onchange=async()=>{const r=await j('/api/eq_preset?index='+lfEl.value+'&channel=left',{method:'POST'});render(r.state);};
hfEl.onchange=async()=>{const r=await j('/api/eq_preset?index='+hfEl.value+'&channel=right',{method:'POST'});render(r.state);};
refresh().catch(e=>stateEl.textContent=e.message);
</script></body></html>
)HTML";

static String json_escape(const String &in) {
  String out;
  out.reserve(in.length() + 4);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

static String state_json() {
  String s = "{";
  s += "\"playing\":" + String((mp3_decoder != nullptr && mp3_decoder->isRunning()) ? "true" : "false");
  s += ",\"volume\":" + String(volume_percent);
  s += ",\"eq_preset_index\":" + String(eq_preset_index);
  s += ",\"lf_preset_index\":" + String(lf_preset_index);
  s += ",\"hf_preset_index\":" + String(hf_preset_index);
  s += ",\"eq_preset_name\":\"" + json_escape(String(kPresetNames[eq_preset_index])) + "\"";
  s += ",\"current_file\":\"" + json_escape(current_file) + "\"";
  s += ",\"sd_ready\":" + String(sd_ready ? "true" : "false");
  s += "}";
  return s;
}

static void collect_mp3_files(const String &dir_path, std::vector<String> &files, uint8_t depth = 6) {
  if (depth == 0) return;
  Serial.printf("[SD] scanning dir: %s (depth=%d)\n", dir_path.c_str(), depth);
  File dir = SD.open(dir_path.c_str());
  if (!dir || !dir.isDirectory()) {
    Serial.printf("[SD]   failed to open dir: %s\n", dir_path.c_str());
    return;
  }

  File entry = dir.openNextFile();
  while (entry) {
    String path = String(entry.path());
    if (entry.isDirectory()) {
      collect_mp3_files(path, files, depth - 1);
    } else if (path.endsWith(".mp3") || path.endsWith(".MP3")) {
      Serial.printf("[SD]   found: %s (%u bytes)\n", path.c_str(), entry.size());
      files.push_back(path);
    }
    entry = dir.openNextFile();
  }
  dir.close();
}

static void refresh_file_cache() {
  cached_files.clear();
  if (!sd_ready) return;
  uint32_t t0 = millis();
  Serial.println("[SD] building file cache...");
  collect_mp3_files("/", cached_files, 4);
  Serial.printf("[SD] cache built: %d files in %lu ms\n", cached_files.size(), millis() - t0);
}

static bool apply_volume_percent(int percent) {
  percent = constrain(percent, 0, 100);
  volume_percent = percent;
  float level = static_cast<float>(percent) / 100.0f;
  Serial.printf("[AMP] setVolume %d%% (gain=%.2f)\n", percent, level);
  bool ok = amp.setVolume(level);
  Serial.printf("[AMP] setVolume => %s\n", ok ? "ok" : "FAIL");
  return ok;
}

static bool apply_eq_preset(int index, const String &channel) {
  Serial.printf("[AMP] apply_eq_preset index=%d channel=%s\n", index, channel.c_str());
  if (index < 0 || index >= static_cast<int>(kPresetCount)) {
    Serial.println("[AMP] preset index out of range");
    return false;
  }
  eq_preset_index = index;

  // Track per-channel preset index
  if (channel == "left" || channel == "both") lf_preset_index = index;
  if (channel == "right" || channel == "both") hf_preset_index = index;

  bool ok = true;
  if (channel == "left" || channel == "both") {
    ok = ok && amp.setEqPreset(Tas58xx::Channel::Left, static_cast<uint8_t>(index));
  }
  if (channel == "right" || channel == "both") {
    ok = ok && amp.setEqPreset(Tas58xx::Channel::Right, static_cast<uint8_t>(index));
  }
  Serial.printf("[AMP] apply_eq_preset => %s (lf=%d, hf=%d)\n", ok ? "ok" : "FAIL", lf_preset_index, hf_preset_index);
  return ok;
}

static void stop_playback() {
  Serial.println("[MP3] stop_playback");
  if (mp3_decoder != nullptr) {
    mp3_decoder->stop();
    delete mp3_decoder;
    mp3_decoder = nullptr;
  }
  if (mp3_file != nullptr) {
    mp3_file->close();
    delete mp3_file;
    mp3_file = nullptr;
  }
  if (i2s_out != nullptr) {
    i2s_out->stop();
    delete i2s_out;
    i2s_out = nullptr;
  }
}

static bool start_playback(const String &path) {
  uint32_t t0 = millis();
  Serial.printf("[MP3] start_playback: \"%s\"\n", path.c_str());

  if (!sd_ready) {
    Serial.println("[MP3]   SD not ready");
    return false;
  }

  Serial.printf("[MP3]   checking file exists... ");
  uint32_t t1 = millis();
  if (!SD.exists(path.c_str())) {
    Serial.printf("NOT FOUND (%lu ms)\n", millis() - t1);
    return false;
  }
  Serial.printf("yes (%lu ms)\n", millis() - t1);

  stop_playback();

  Serial.printf("[MP3]   opening file... ");
  t1 = millis();
  mp3_file = new AudioFileSourceSD(path.c_str());
  if (mp3_file == nullptr || !mp3_file->isOpen()) {
    Serial.printf("FAILED (%lu ms)\n", millis() - t1);
    stop_playback();
    return false;
  }
  Serial.printf("ok, size=%u (%lu ms)\n", mp3_file->getSize(), millis() - t1);

  Serial.printf("[MP3]   creating I2S output... ");
  t1 = millis();
  i2s_out = new AudioOutputI2S();
  if (i2s_out == nullptr) {
    Serial.printf("FAILED (%lu ms)\n", millis() - t1);
    stop_playback();
    return false;
  }
  i2s_out->SetPinout(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT);
  i2s_out->SetOutputModeMono(false);
  i2s_out->SetGain(1.0f);
  Serial.printf("ok (%lu ms)\n", millis() - t1);

  Serial.printf("[MP3]   starting decoder... ");
  t1 = millis();
  mp3_decoder = new AudioGeneratorMP3();
  if (mp3_decoder == nullptr || !mp3_decoder->begin(mp3_file, i2s_out)) {
    Serial.printf("FAILED (%lu ms)\n", millis() - t1);
    stop_playback();
    return false;
  }
  Serial.printf("ok (%lu ms)\n", millis() - t1);

  current_file = path;
  Serial.printf("[MP3]   playback started in %lu ms total, heap=%u\n", millis() - t0, ESP.getFreeHeap());
  return true;
}

static bool init_wifi() {
  const char *ap_ssid = (strlen(WIFI_SSID) > 0) ? WIFI_SSID : "tas58xx-web";
  const char *ap_pass = (strlen(WIFI_PASSWORD) > 0) ? WIFI_PASSWORD : "tas58xx123";

  Serial.println("[WIFI] ---- WiFi Init ----");
  Serial.printf("[WIFI] WIFI_SSID=\"%s\" (len=%d)\n", WIFI_SSID, strlen(WIFI_SSID));
  Serial.printf("[WIFI] WIFI_PASSWORD=\"%s\" (len=%d)\n", WIFI_PASSWORD, strlen(WIFI_PASSWORD));
  Serial.printf("[WIFI] Starting AP: SSID=\"%s\" PASS=\"%s\"\n", ap_ssid, ap_pass);

  WiFi.mode(WIFI_AP);
  delay(100);
  bool ok = WiFi.softAP(ap_ssid, ap_pass);
  Serial.printf("[WIFI] softAP returned: %s\n", ok ? "true" : "false");

  delay(500); // let DHCP settle
  IPAddress apip = WiFi.softAPIP();
  Serial.printf("[WIFI] AP IP: %s\n", apip.toString().c_str());
  Serial.printf("[WIFI] AP MAC: %s\n", WiFi.softAPmacAddress().c_str());
  Serial.println("[WIFI] ---- WiFi Init Done ----");
  return ok;
}

static void init_amp() {
  uint32_t t0 = millis();
  Serial.printf("[AMP] begin(SDA=%d, SCL=%d, addr=0x%02X, PDN=%d)...\n",
                PIN_I2C_SDA, PIN_I2C_SCL, TAS58XX_ADDRESS, TAS58XX_GPIO_PDN);
  if (!amp.begin(Wire, PIN_I2C_SDA, PIN_I2C_SCL, 400000, TAS58XX_GPIO_PDN)) {
    Serial.println("[AMP] begin FAILED");
    return;
  }
  Serial.printf("[AMP] begin ok (%lu ms)\n", millis() - t0);

  Serial.print("[AMP] applyMinimalInit... ");
  uint32_t t1 = millis();
  if (!amp.applyMinimalInit()) {
    Serial.printf("FAILED (%lu ms)\n", millis() - t1);
    return;
  }
  Serial.printf("ok (%lu ms)\n", millis() - t1);

  Serial.println("[AMP] clearFaults");
  amp.clearFaults();
  Serial.println("[AMP] setEqMode=Presets");
  amp.setEqMode(Tas58xx::EqMode::Presets);
  apply_eq_preset(0, "both");
  Serial.println("[AMP] setControlState=Play");
  amp.setControlState(Tas58xx::ControlState::Play);
  Serial.println("[AMP] setMute=false");
  amp.setMute(false);
  apply_volume_percent(50);
  Serial.printf("[AMP] ready (total %lu ms)\n", millis() - t0);
}

static void init_sd() {
  uint32_t t0 = millis();
  Serial.printf("[SD] SPI.begin(SCK=%d, MISO=%d, MOSI=%d, CS=%d)\n",
                PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  Serial.printf("[SD] SD.begin(CS=%d, freq=1MHz)...\n", PIN_SD_CS);
  sd_ready = SD.begin(PIN_SD_CS, SPI, 1000000, "/sd", 10, false);
  if (!sd_ready) {
    Serial.printf("[SD] mount FAILED (%lu ms)\n", millis() - t0);
    return;
  }
  uint8_t ct = SD.cardType();
  Serial.printf("[SD] mounted, cardType=%d, size=%llu MB (%lu ms)\n",
                ct, SD.cardSize() / (1024 * 1024), millis() - t0);
  refresh_file_cache();
}

static void send_json(int status, const String &json) {
  server.send(status, "application/json", json);
}

static void handle_root() {
  server.send_P(200, "text/html", INDEX_HTML);
}

static void handle_state() {
  Serial.printf("[WEB] GET /api/state (heap=%u)\n", ESP.getFreeHeap());
  send_json(200, String("{\"state\":") + state_json() + "}");
}

static void handle_files() {
  Serial.printf("[WEB] GET /api/files (cached: %d files)\n", cached_files.size());
  String out = "{\"sd_mount_path\":\"" + String(kSdMountPath) + "\",\"files\":[";
  for (size_t i = 0; i < cached_files.size(); i++) {
    if (i) out += ",";
    out += "\"" + json_escape(cached_files[i]) + "\"";
  }
  out += "]}";
  send_json(200, out);
}

static void handle_play() {
  Serial.println("[WEB] POST /api/play");
  uint32_t t0 = millis();

  if (cached_files.empty()) {
    Serial.println("[WEB]   no files in cache");
    send_json(404, "{\"error\":\"No MP3 files found\"}");
    return;
  }

  String file = cached_files[0];
  if (server.hasArg("file")) {
    file = server.arg("file");
  }
  Serial.printf("[WEB]   file=\"%s\"\n", file.c_str());

  if (!start_playback(file)) {
    Serial.printf("[WEB]   start_playback FAILED (%lu ms)\n", millis() - t0);
    send_json(400, "{\"error\":\"Failed to start playback\"}");
    return;
  }

  Serial.printf("[WEB]   play request handled in %lu ms\n", millis() - t0);
  send_json(200, String("{\"state\":") + state_json() + "}");
}

static void handle_stop() {
  Serial.println("[WEB] POST /api/stop");
  stop_playback();
  send_json(200, String("{\"state\":") + state_json() + "}");
}

static void handle_volume() {
  Serial.println("[WEB] POST /api/volume");
  if (!server.hasArg("value")) {
    send_json(400, "{\"error\":\"Missing value\"}");
    return;
  }

  int value = server.arg("value").toInt();
  Serial.printf("[WEB]   value=%d\n", value);
  if (!apply_volume_percent(value)) {
    send_json(500, "{\"error\":\"Failed to set volume\"}");
    return;
  }

  send_json(200, String("{\"state\":") + state_json() + "}");
}

static void handle_eq_preset() {
  Serial.println("[WEB] POST /api/eq_preset");
  if (!server.hasArg("index")) {
    send_json(400, "{\"error\":\"Missing index\"}");
    return;
  }

  int index = server.arg("index").toInt();
  Serial.printf("[WEB]   index=%d\n", index);
  String channel = "both";
  if (server.hasArg("channel")) channel = server.arg("channel");

  if (!apply_eq_preset(index, channel)) {
    send_json(400, "{\"error\":\"Invalid preset or apply failed\"}");
    return;
  }

  send_json(200, String("{\"state\":") + state_json() + "}");
}

static void init_web() {
  server.on("/", HTTP_GET, handle_root);
  server.on("/api/state", HTTP_GET, handle_state);
  server.on("/api/files", HTTP_GET, handle_files);
  server.on("/api/play", HTTP_POST, handle_play);
  server.on("/api/stop", HTTP_POST, handle_stop);
  server.on("/api/volume", HTTP_POST, handle_volume);
  server.on("/api/eq_preset", HTTP_POST, handle_eq_preset);
  server.begin();
  Serial.println("[WEB] server started");
}

void setup() {
  Serial.begin(115200);
  delay(1000); // give USB-serial time to connect
  Serial.println();
  Serial.println("============================");
  Serial.println("ESP32-S3 TAS58xx Web Player");
  Serial.println("============================");
  Serial.printf("CPU freq: %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("SDK: %s\n", ESP.getSdkVersion());
  Serial.println();

  Serial.println("[SETUP] init_wifi...");
  if (!init_wifi()) {
    Serial.println("[SETUP] WiFi FAILED — continuing anyway");
  }

  Serial.println("[SETUP] init_amp...");
  init_amp();

  Serial.println("[SETUP] init_sd...");
  init_sd();

  Serial.println("[SETUP] init_web...");
  init_web();

  Serial.println();
  Serial.println("[SETUP] ===== READY =====");
  Serial.printf("[SETUP] Free heap: %u bytes\n", ESP.getFreeHeap());
}

void loop() {
  server.handleClient();

  if (mp3_decoder != nullptr && mp3_decoder->isRunning()) {
    if (!mp3_decoder->loop()) {
      stop_playback();
    }
  }
}
