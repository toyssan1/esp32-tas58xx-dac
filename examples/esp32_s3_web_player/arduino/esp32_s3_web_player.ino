#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
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
static AsyncWebServer server(80);

static AudioFileSourceSD *mp3_file = nullptr;
static AudioGeneratorMP3 *mp3_decoder = nullptr;
static AudioOutputI2S *i2s_out = nullptr;

static bool sd_ready = false;
static int volume_percent = 50;
static int eq_preset_index = 0;
static String current_file;

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
<label>EQ Preset</label><select id='preset'></select>
<div>Current file: <span id='cur'>(none)</span></div>
</div>
<div class='card'><strong>SD mount path:</strong> <span id='mount'></span><div id='files'></div></div>
<div class='card'><pre id='state'>Loading...</pre></div>
<script>
const presets = ["Flat","LF 60Hz","LF 70Hz","LF 80Hz","LF 90Hz","LF 100Hz","LF 110Hz","LF 120Hz","LF 130Hz","LF 140Hz","LF 150Hz","HF 60Hz","HF 70Hz","HF 80Hz","HF 90Hz","HF 100Hz","HF 110Hz","HF 120Hz","HF 130Hz","HF 140Hz","HF 150Hz"];
const stateEl=document.getElementById('state');
const filesEl=document.getElementById('files');
const mountEl=document.getElementById('mount');
const curEl=document.getElementById('cur');
const volEl=document.getElementById('vol');
const vLabel=document.getElementById('vLabel');
const presetEl=document.getElementById('preset');
presets.forEach((p,i)=>{const o=document.createElement('option');o.value=i;o.textContent=`${i}: ${p}`;presetEl.appendChild(o);});
async function j(url,opt){const r=await fetch(url,opt);const d=await r.json();if(!r.ok)throw new Error(d.error||`HTTP ${r.status}`);return d;}
function render(s){stateEl.textContent=JSON.stringify(s,null,2);volEl.value=s.volume;vLabel.textContent=s.volume;presetEl.value=s.eq_preset_index;curEl.textContent=s.current_file||'(none)';}
async function refresh(){const s=await j('/api/state');render(s.state);const f=await j('/api/files');mountEl.textContent=f.sd_mount_path;filesEl.innerHTML='';if(!f.files.length){filesEl.textContent='No MP3 files found';return;}f.files.forEach(file=>{const b=document.createElement('button');b.textContent=file;b.onclick=async()=>{const r=await j('/api/play?file='+encodeURIComponent(file),{method:'POST'});render(r.state);};filesEl.appendChild(b);});}
document.getElementById('play').onclick=async()=>{const r=await j('/api/play',{method:'POST'});render(r.state);};
document.getElementById('stop').onclick=async()=>{const r=await j('/api/stop',{method:'POST'});render(r.state);};
volEl.oninput=()=>vLabel.textContent=volEl.value;
volEl.onchange=async()=>{const r=await j('/api/volume?value='+volEl.value,{method:'POST'});render(r.state);};
presetEl.onchange=async()=>{const r=await j('/api/eq_preset?index='+presetEl.value+'&channel=both',{method:'POST'});render(r.state);};
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
  s += ",\"eq_preset_name\":\"" + json_escape(String(kPresetNames[eq_preset_index])) + "\"";
  s += ",\"current_file\":\"" + json_escape(current_file) + "\"";
  s += ",\"sd_ready\":" + String(sd_ready ? "true" : "false");
  s += "}";
  return s;
}

static void collect_mp3_files(const String &dir_path, std::vector<String> &files, uint8_t depth = 6) {
  if (depth == 0) return;
  File dir = SD.open(dir_path.c_str());
  if (!dir || !dir.isDirectory()) return;

  File entry = dir.openNextFile();
  while (entry) {
    String path = String(entry.path());
    if (entry.isDirectory()) {
      collect_mp3_files(path, files, depth - 1);
    } else if (path.endsWith(".mp3") || path.endsWith(".MP3")) {
      files.push_back(path);
    }
    entry = dir.openNextFile();
  }
  dir.close();
}

static bool apply_volume_percent(int percent) {
  percent = constrain(percent, 0, 100);
  volume_percent = percent;
  float level = static_cast<float>(percent) / 100.0f;
  return amp.setVolume(level);
}

static bool apply_eq_preset(int index, const String &channel) {
  if (index < 0 || index >= static_cast<int>(kPresetCount)) return false;
  eq_preset_index = index;

  bool ok = true;
  if (channel == "left" || channel == "both") {
    ok = ok && amp.setEqPreset(Tas58xx::Channel::Left, static_cast<uint8_t>(index));
  }
  if (channel == "right" || channel == "both") {
    ok = ok && amp.setEqPreset(Tas58xx::Channel::Right, static_cast<uint8_t>(index));
  }
  return ok;
}

static void stop_playback() {
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
  if (!sd_ready) return false;
  if (!SD.exists(path.c_str())) return false;

  stop_playback();

  mp3_file = new AudioFileSourceSD(path.c_str());
  if (mp3_file == nullptr || !mp3_file->isOpen()) {
    stop_playback();
    return false;
  }

  i2s_out = new AudioOutputI2S();
  if (i2s_out == nullptr) {
    stop_playback();
    return false;
  }

  i2s_out->SetPinout(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT);
  i2s_out->SetOutputModeMono(false);
  i2s_out->SetGain(1.0f);

  mp3_decoder = new AudioGeneratorMP3();
  if (mp3_decoder == nullptr || !mp3_decoder->begin(mp3_file, i2s_out)) {
    stop_playback();
    return false;
  }

  current_file = path;
  return true;
}

static bool init_wifi() {
  if (strlen(WIFI_SSID) == 0) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("tas58xx-web", "tas58xx123");
    Serial.print("[WIFI] AP mode IP: ");
    Serial.println(WiFi.softAPIP());
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WIFI] Connecting");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] STA connect failed");
    return false;
  }

  Serial.print("[WIFI] Connected IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

static void init_amp() {
  if (!amp.begin(Wire, PIN_I2C_SDA, PIN_I2C_SCL, 400000, TAS58XX_GPIO_PDN)) {
    Serial.println("[AMP] begin failed");
    return;
  }
  if (!amp.applyMinimalInit()) {
    Serial.println("[AMP] minimal init failed");
    return;
  }

  amp.clearFaults();
  amp.setEqMode(Tas58xx::EqMode::Presets);
  apply_eq_preset(0, "both");
  amp.setControlState(Tas58xx::ControlState::Play);
  amp.setMute(false);
  apply_volume_percent(50);
  Serial.println("[AMP] ready");
}

static void init_sd() {
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  sd_ready = SD.begin(PIN_SD_CS, SPI, 1000000, "/sd", 10, false);
  if (!sd_ready) {
    Serial.println("[SD] mount failed");
    return;
  }
  Serial.println("[SD] mounted");
}

static void send_json(AsyncWebServerRequest *req, int status, const String &json) {
  req->send(status, "application/json", json);
}

static void init_web() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *req) {
    send_json(req, 200, String("{\"state\":") + state_json() + "}");
  });

  server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest *req) {
    std::vector<String> files;
    if (sd_ready) collect_mp3_files("/", files, 8);

    String out = "{\"sd_mount_path\":\"" + String(kSdMountPath) + "\",\"files\":[";
    for (size_t i = 0; i < files.size(); i++) {
      if (i) out += ",";
      out += "\"" + json_escape(files[i]) + "\"";
    }
    out += "]}";
    send_json(req, 200, out);
  });

  server.on("/api/play", HTTP_POST, [](AsyncWebServerRequest *req) {
    std::vector<String> files;
    if (sd_ready) collect_mp3_files("/", files, 8);
    if (files.empty()) {
      send_json(req, 404, "{\"error\":\"No MP3 files found\"}");
      return;
    }

    String file = files[0];
    if (req->hasParam("file")) {
      file = req->getParam("file")->value();
    }

    if (!start_playback(file)) {
      send_json(req, 400, "{\"error\":\"Failed to start playback\"}");
      return;
    }

    send_json(req, 200, String("{\"state\":") + state_json() + "}");
  });

  server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *req) {
    stop_playback();
    send_json(req, 200, String("{\"state\":") + state_json() + "}");
  });

  server.on("/api/volume", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("value")) {
      send_json(req, 400, "{\"error\":\"Missing value\"}");
      return;
    }

    int value = req->getParam("value")->value().toInt();
    if (!apply_volume_percent(value)) {
      send_json(req, 500, "{\"error\":\"Failed to set volume\"}");
      return;
    }

    send_json(req, 200, String("{\"state\":") + state_json() + "}");
  });

  server.on("/api/eq_preset", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("index")) {
      send_json(req, 400, "{\"error\":\"Missing index\"}");
      return;
    }

    int index = req->getParam("index")->value().toInt();
    String channel = "both";
    if (req->hasParam("channel")) channel = req->getParam("channel")->value();

    if (!apply_eq_preset(index, channel)) {
      send_json(req, 400, "{\"error\":\"Invalid preset or apply failed\"}");
      return;
    }

    send_json(req, 200, String("{\"state\":") + state_json() + "}");
  });

  server.begin();
  Serial.println("[WEB] server started");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  init_wifi();
  init_amp();
  init_sd();
  init_web();
}

void loop() {
  if (mp3_decoder != nullptr && mp3_decoder->isRunning()) {
    if (!mp3_decoder->loop()) {
      stop_playback();
    }
    return;
  }
  delay(5);
}
