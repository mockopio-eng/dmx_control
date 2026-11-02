#include <Arduino.h>
#include <SPI.h>
#define USE_ETHERNET_GENERIC true
#include <Ethernet_Generic.h>
#include <EthernetWebServer.h>
#include <esp_dmx.h>
#include <Preferences.h>

// --- KONFIGURACJA SIECI PRZEWODOWEJ (W5500) ---
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 10);
#define W5500_CS_PIN 5
#define W5500_RST_PIN 26

EthernetWebServer server(80);

// --- KONFIGURACJA DMX ---
int transmitPin = 17;
int receivePin = 16;
dmx_port_t dmxPort = 1;
byte dmxData[DMX_PACKET_SIZE];
unsigned long lastUpdate = 0;

// --- ZARZĄDZANIE SCENAMI I CHASERAMI ---
#define MAX_SCENES 16
byte scenes[MAX_SCENES][DMX_PACKET_SIZE];
String sceneNames[MAX_SCENES];
int sceneCount = 0;

#define MAX_CHASERS 16
#define MAX_STEPS 32
struct Chaser {
  int steps[MAX_STEPS];
  int stepCount;
};
Chaser chasers[MAX_CHASERS];
int chaserCount = 0;

// Zmienne odtwarzacza
bool autoPlay = false;
bool isFading = false;
int activeChaser = -1;
int currentStepIndex = 0;
unsigned long phaseStartTime = 0;
unsigned long fadeDuration = 2000;
unsigned long holdDuration = 3000;
int targetScene = 0;
byte fadeStartData[DMX_PACKET_SIZE];

Preferences prefs;

// --- FUNKCJE ZAPISU I ODCZYTU Z PAMIĘCI FLASH ---
void saveAllToFlash() {
  prefs.begin("dmx", false);
  
  prefs.putInt("scC", sceneCount);
  for (int i = 0; i < sceneCount; i++) {
    String keyData = "s" + String(i);
    String keyName = "n" + String(i);
    prefs.putBytes(keyData.c_str(), scenes[i], DMX_PACKET_SIZE);
    prefs.putString(keyName.c_str(), sceneNames[i]);
  }
  
  prefs.putInt("chC", chaserCount);
  for (int i = 0; i < chaserCount; i++) {
    String key = "c" + String(i);
    prefs.putBytes(key.c_str(), &chasers[i], sizeof(Chaser));
  }
  prefs.end();
  Serial.println("Pomyslnie zapisano dane w Flash!");
}

void loadAllFromFlash() {
  prefs.begin("dmx", true);
  
  sceneCount = prefs.getInt("scC", 0);
  if (sceneCount > MAX_SCENES) sceneCount = MAX_SCENES;
  for (int i = 0; i < sceneCount; i++) {
    String keyData = "s" + String(i);
    String keyName = "n" + String(i);
    prefs.getBytes(keyData.c_str(), scenes[i], DMX_PACKET_SIZE);
    sceneNames[i] = prefs.getString(keyName.c_str(), "Scena " + String(i));
  }
  
  chaserCount = prefs.getInt("chC", 0);
  if (chaserCount > MAX_CHASERS) chaserCount = MAX_CHASERS;
  for (int i = 0; i < chaserCount; i++) {
    String key = "c" + String(i);
    prefs.getBytes(key.c_str(), &chasers[i], sizeof(Chaser));
  }
  prefs.end();
}

// --- INTERFEJS HTML ---
const char htmlPage[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>System DMX: Ethernet Pro</title>
  <style>
    body { background-color: #121212; color: white; font-family: sans-serif; padding: 15px; margin: 0; }
    h1 { text-align: center; font-weight: normal; margin-bottom: 20px; color: #fff; }
    .grid { display: grid; grid-template-columns: 1fr; gap: 20px; max-width: 1200px; margin: 0 auto; }
    @media (min-width: 800px) { .grid { grid-template-columns: repeat(2, 1fr); } }
    @media (min-width: 1200px) { .grid { grid-template-columns: repeat(4, 1fr); } }
    
    .panel { background: #2a2a2a; padding: 15px; border-radius: 12px; margin-bottom: 20px; max-width: 1200px; margin: 0 auto 20px auto; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
    .panel h3 { margin-top: 0; color: #00d2ff; border-bottom: 1px solid #444; padding-bottom: 10px; }
    
    .scene-list, .chaser-list { display: flex; flex-direction: column; gap: 8px; margin-top: 15px; }
    .scene-item, .chaser-item { background: #1e1e1e; padding: 10px; border-radius: 8px; display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
    
    .section-title { grid-column: 1 / -1; margin-top: 20px; border-bottom: 2px solid #555; padding-bottom: 10px; color: #fff; }
    .addr { font-size: 0.6em; color: #888; }
    
    .segment { background: #1e1e1e; padding: 15px; border-radius: 12px; }
    .segment h4 { margin: 0 0 15px 0; text-align: center; color: #ccc; }
    .preview { height: 40px; border-radius: 8px; background: #000; border: 1px solid #444; flex-grow: 1; }
    
    .row { display: flex; align-items: center; margin-bottom: 12px; }
    .row span { width: 35px; font-weight: bold; font-size: 1em; }
    .row input[type=range] { flex-grow: 1; margin: 0 10px; cursor: pointer; }
    .row input[type=number].val-box, .hex-input { background: #333; color: white; border: 1px solid #555; border-radius: 6px; padding: 4px; text-align: center; }
    .row input[type=number].val-box { width: 60px; }
    .hex-input { width: 75px; font-family: monospace; font-size: 14px; height: 30px;}
    
    .c-dim span { color: #aaaaaa; } .c-r span { color: #ff4444; } .c-g span { color: #44ff44; } .c-b span { color: #4444ff; }
    
    button { padding: 8px 12px; border: none; border-radius: 6px; background: #444; color: white; cursor: pointer; font-weight: bold; transition: 0.2s;}
    button:hover { background: #555; }
    .btn-green { background: #28a745; } .btn-green:hover { background: #218838; }
    .btn-red { background: #dc3545; } .btn-red:hover { background: #c82333; }
    .btn-blue { background: #007bff; } .btn-blue:hover { background: #0069d9; }
    .btn-blackout { background: #ff0000; color: white; font-size: 1.2em; width: 100%; padding: 15px; border-radius: 8px; font-weight: 900; margin-bottom: 20px;}
    .btn-blackout:hover { background: #cc0000; }
    
    input[type=text] { background: #333; color: white; border: 1px solid #555; padding: 8px; border-radius: 6px; min-width: 120px; }
  </style>
</head>
<body>
  <h1>Sterowanie DMX (Ethernet)</h1>
  <div style="max-width: 1200px; margin: 0 auto;">
    <button class="btn-blackout" onclick="blackout()">GŁÓWNY BLACKOUT</button>
  </div>

  <div class="panel">
    <h3>Zarządzanie Scenami</h3>
    <button class="btn-blue" onclick="saveNewScene()">+ Zapisz jako Nową Scenę</button>
    <div class="scene-list" id="sceneList">Wczytywanie scen...</div>
  </div>

  <div class="panel">
    <h3>Chasery (Sekwencje)</h3>
    <div style="margin-bottom: 15px;">
      <button class="btn-blue" onclick="addChaser()">+ Nowy Chaser</button>
      <div style="float: right; display:flex; gap:10px; align-items:center;">
        <label>Fade: <input type="number" id="g-fade" value="2000" class="val-box" style="width:70px;"> ms</label>
        <label>Hold: <input type="number" id="g-hold" value="3000" class="val-box" style="width:70px;"> ms</label>
        <button class="btn-red" onclick="cmd('/stop')">STOP CHASER</button>
      </div>
      <div style="clear:both;"></div>
    </div>
    <div class="chaser-list" id="chaserList">Wczytywanie chaserów...</div>
  </div>

  <div class="grid" id="segments"></div>

  <script>
    const layoutConfig = [
      { type: 'single', name: 'Lampa Poczatkowa 1', ch: 1 }, 
      { type: 'single', name: 'Lampa Poczatkowa 2', ch: 8 }, 
      { type: 'bar', name: 'LedBar 1', ch: 15 },
      { type: 'bar', name: 'LedBar 2', ch: 21 },
      { type: 'bar', name: 'LedBar 3', ch: 27 },
      { type: 'single', name: 'Lampa Koncowa 1', ch: 33 },
      { type: 'single', name: 'Lampa Koncowa 2', ch: 40 }
    ];
    let timers = {};

    function rgbToHex(r, g, b) {
      return "#" + (1 << 24 | r << 16 | g << 8 | b).toString(16).slice(1).toUpperCase();
    }
    function hexToRgb(hex) {
      let result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
      return result ? { r: parseInt(result[1], 16), g: parseInt(result[2], 16), b: parseInt(result[3], 16) } : null;
    }

    function initUI() {
      let html = '';
      layoutConfig.forEach(item => {
        html += '<h2 class="section-title">' + item.name + ' <span class="addr">(Adres: ' + item.ch + ')</span></h2>';
        if(item.type === 'single') {
          let rCh = item.ch+1, gCh = item.ch+2, bCh = item.ch+3;
          html += `<div class="segment">
                     <h4>RGB + Dimmer</h4>
                     <div style="display: flex; gap: 10px; margin-bottom: 15px; align-items: center;">
                       <div class="preview" id="prev-${item.ch}"></div>
                       <input type="text" class="hex-input" id="hex-${item.ch}" data-r="${rCh}" data-g="${gCh}" data-b="${bCh}" value="#000000" maxlength="7">
                     </div>
                     ${createRow(item.ch, 'dim', 'DIM', 'c-dim')}
                     ${createRow(rCh, 'r', 'R', 'c-r')}
                     ${createRow(gCh, 'g', 'G', 'c-g')}
                     ${createRow(bCh, 'b', 'B', 'c-b')}
                   </div>`;
        } else {
          for(let i=0; i<2; i++) {
            let segCh = item.ch + (i*3);
            let rCh = segCh, gCh = segCh+1, bCh = segCh+2;
            html += `<div class="segment">
                       <h4>Segment ${i+1}</h4>
                       <div style="display: flex; gap: 10px; margin-bottom: 15px; align-items: center;">
                         <div class="preview" id="prev-${segCh}"></div>
                         <input type="text" class="hex-input" id="hex-${segCh}" data-r="${rCh}" data-g="${gCh}" data-b="${bCh}" value="#000000" maxlength="7">
                       </div>
                       ${createRow(rCh, 'r', 'R', 'c-r')}
                       ${createRow(gCh, 'g', 'G', 'c-g')}
                       ${createRow(bCh, 'b', 'B', 'c-b')}
                     </div>`;
          }
        }
      });
      document.getElementById('segments').innerHTML = html;
      attachSliderEvents();
      refreshState();
      fetchDmxState();
    }

    function createRow(absCh, colorId, label, colorClass) {
      return `<div class="row ${colorClass}">
                <span>${label}</span>
                <input type="range" min="0" max="255" value="0" data-abs-ch="${absCh}" id="${colorId}-${absCh}">
                <input type="number" class="val-box" min="0" max="255" value="0" data-abs-ch="${absCh}" id="num-${colorId}-${absCh}">
              </div>`;
    }

    function attachSliderEvents() {
      document.querySelectorAll('input[type=range][data-abs-ch]').forEach(slider => {
        slider.addEventListener('input', e => {
          let ch = e.target.dataset.absCh, val = e.target.value;
          document.getElementById('num-' + e.target.id).value = val;
          sendSingleData(ch, val); updatePreviews();
        });
      });
      document.querySelectorAll('input[type=number][data-abs-ch]').forEach(num => {
        num.addEventListener('input', e => {
          let ch = e.target.dataset.absCh, val = Math.min(255, Math.max(0, parseInt(e.target.value) || 0));
          e.target.value = val; document.getElementById(e.target.id.replace('num-', '')).value = val;
          sendSingleData(ch, val); updatePreviews();
        });
      });
      document.querySelectorAll('.hex-input').forEach(hInput => {
        hInput.addEventListener('change', e => {
          let rgb = hexToRgb(e.target.value);
          if (rgb) {
            let rCh = e.target.dataset.r, gCh = e.target.dataset.g, bCh = e.target.dataset.b;
            document.getElementById('r-' + rCh).value = rgb.r; document.getElementById('num-r-' + rCh).value = rgb.r;
            document.getElementById('g-' + gCh).value = rgb.g; document.getElementById('num-g-' + gCh).value = rgb.g;
            document.getElementById('b-' + bCh).value = rgb.b; document.getElementById('num-b-' + bCh).value = rgb.b;
            sendSingleData(rCh, rgb.r); sendSingleData(gCh, rgb.g); sendSingleData(bCh, rgb.b);
          }
          updatePreviews();
        });
      });
    }

    function sendSingleData(ch, val) {
      if(timers[ch]) clearTimeout(timers[ch]);
      timers[ch] = setTimeout(() => { fetch(`/setSingle?ch=${ch}&v=${val}`).catch(()=>{}); }, 30);
    }

    function updatePreviews() {
      layoutConfig.forEach(item => {
        if(item.type === 'single') {
          let r = document.getElementById(`r-${item.ch+1}`).value, g = document.getElementById(`g-${item.ch+2}`).value, b = document.getElementById(`b-${item.ch+3}`).value;
          document.getElementById(`prev-${item.ch}`).style.backgroundColor = `rgb(${r},${g},${b})`;
          document.getElementById(`hex-${item.ch}`).value = rgbToHex(parseInt(r), parseInt(g), parseInt(b));
        } else {
          for(let i=0; i<2; i++) {
            let segCh = item.ch + (i*3);
            let r = document.getElementById(`r-${segCh}`).value, g = document.getElementById(`g-${segCh+1}`).value, b = document.getElementById(`b-${segCh+2}`).value;
            document.getElementById(`prev-${segCh}`).style.backgroundColor = `rgb(${r},${g},${b})`;
            document.getElementById(`hex-${segCh}`).value = rgbToHex(parseInt(r), parseInt(g), parseInt(b));
          }
        }
      });
    }

    function cmd(url) { fetch(url).catch(()=>{}); }

    function refreshState() {
      fetch('/state').then(r=>r.json()).then(data => {
        let sHtml = '';
        if(data.scenes.length === 0) sHtml = '<i>Brak zapisanych scen. Ustaw suwaki i zapisz.</i>';
        data.scenes.forEach(s => {
          sHtml += `<div class="scene-item">
                      <b>ID ${s.id}:</b> <input type="text" id="sname-${s.id}" value="${s.name}">
                      <button onclick="renameScene(${s.id})">Zmień nazwę</button>
                      <span style="border-left: 1px solid #555; margin: 0 5px; height: 20px;"></span>
                      <button class="btn-green" onclick="loadScene(${s.id})">Pokaż</button>
                      <button onclick="updateScene(${s.id})">Nadpisz</button>
                      <button class="btn-red" onclick="deleteScene(${s.id})">Usuń</button>
                    </div>`;
        });
        document.getElementById('sceneList').innerHTML = sHtml;
        
        let cHtml = '';
        if(data.chasers.length === 0) cHtml = '<i>Brak zapisanych chaserów. Kliknij "+ Nowy Chaser".</i>';
        data.chasers.forEach(c => {
          cHtml += `<div class="chaser-item">
                      <b>Chaser ${c.id}:</b> Sceny: 
                      <input type="text" id="seq-${c.id}" placeholder="np. 0,1,2" value="${c.seq}">
                      <button onclick="saveChaser(${c.id})">Zapisz</button>
                      <button class="btn-green" onclick="playChaser(${c.id})">PLAY</button>
                      <button class="btn-red" onclick="deleteChaser(${c.id})">Usuń</button>
                    </div>`;
        });
        document.getElementById('chaserList').innerHTML = cHtml;
      });
    }

    function blackout() {
      fetch('/blackout').then(() => { setTimeout(fetchDmxState, 100); });
      document.querySelectorAll('input[type=range][data-abs-ch]').forEach(slider => { slider.value = 0; });
      document.querySelectorAll('input[type=number][data-abs-ch]').forEach(num => { num.value = 0; });
      updatePreviews();
    }

    function saveNewScene() { fetch('/saveScene').then(()=>refreshState()); }
    function updateScene(id) { fetch(`/updateScene?id=${id}`).then(()=>alert("Scena nadpisana!")); }
    function renameScene(id) {
      let nowaNazwa = encodeURIComponent(document.getElementById(`sname-${id}`).value);
      fetch(`/renameScene?id=${id}&name=${nowaNazwa}`).then(()=>alert("Nazwa zapisana!"));
    }
    function deleteScene(id) { if(confirm("Usunąć scenę?")) fetch(`/deleteScene?id=${id}`).then(()=>refreshState()); }
    function loadScene(id) { fetch(`/loadScene?id=${id}`).then(() => { setTimeout(fetchDmxState, 100); }); }

    function fetchDmxState() {
      fetch('/getDmx').then(r=>r.json()).then(arr => {
        document.querySelectorAll('input[type=range][data-abs-ch]').forEach(s => {
          let val = arr[parseInt(s.dataset.absCh)];
          s.value = val; document.getElementById('num-' + s.id).value = val;
        });
        updatePreviews();
      });
    }

    function addChaser() { fetch('/addChaser').then(()=>refreshState()); }
    function deleteChaser(id) { if(confirm("Usunąć chaser?")) fetch(`/deleteChaser?id=${id}`).then(()=>refreshState()); }
    function saveChaser(id) {
      let seq = document.getElementById(`seq-${id}`).value;
      fetch(`/saveChaser?id=${id}&seq=${seq}`).then(()=>alert("Zapisano!"));
    }
    function playChaser(id) {
      let f = document.getElementById('g-fade').value, h = document.getElementById('g-hold').value;
      fetch(`/playChaser?id=${id}&f=${f}&h=${h}`);
    }

    window.onload = initUI;
  </script>
</body>
</html>
)=====";

void setup() {
  Serial.begin(115200);

  // --- Konfiguracja DMX ---
  dmx_config_t config = DMX_CONFIG_DEFAULT;
  dmx_personality_t personalities[] = { {1, "Default"} };
  dmx_driver_install(dmxPort, &config, personalities, 1);
  dmx_set_pin(dmxPort, transmitPin, receivePin, -1);
  memset(dmxData, 0, DMX_PACKET_SIZE);

  loadAllFromFlash();

  // --- Twardy reset modulu W5500 ---
  pinMode(W5500_RST_PIN, OUTPUT);
  digitalWrite(W5500_RST_PIN, LOW);
  delay(50);
  digitalWrite(W5500_RST_PIN, HIGH);
  delay(100);

  // --- Inicjalizacja Sieci Ethernet (W5500) ---
  Ethernet.init(W5500_CS_PIN); // CS Pin
  Serial.println("Inicjalizacja Ethernet...");
  
  Ethernet.begin(mac, ip);
  
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("Blad: Nie znaleziono modulu W5500. Sprawdz kable!");
  } else if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("Kabel Ethernet nie jest podlaczony.");
  }
  
  Serial.print("Serwer uruchomiony na adresie IP: ");
  Serial.println(Ethernet.localIP());

  // --- Endpointy Serwera (API zostaje takie samo) ---
  server.on("/", []() { server.send(200, "text/html", htmlPage); });

  server.on("/state", []() {
    String json = "{\"scenes\":[";
    for (int i = 0; i < sceneCount; i++) {
      json += "{\"id\":" + String(i) + ",\"name\":\"" + sceneNames[i] + "\"}";
      if(i < sceneCount - 1) json += ",";
    }
    json += "],\"chasers\":[";
    for (int i = 0; i < chaserCount; i++) {
      json += "{\"id\":" + String(i) + ",\"seq\":\"";
      for (int j = 0; j < chasers[i].stepCount; j++) {
        json += String(chasers[i].steps[j]);
        if(j < chasers[i].stepCount - 1) json += ",";
      }
      json += "\"}";
      if(i < chaserCount - 1) json += ",";
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  server.on("/getDmx", []() {
    String json; json.reserve(2500); json += "[";
    for(int i = 0; i <= 512; i++) { json += String(dmxData[i]); if(i < 512) json += ","; }
    json += "]";
    server.send(200, "application/json", json);
  });

  server.on("/setSingle", []() {
    autoPlay = false;
    int ch = server.arg("ch").toInt();
    int v = server.arg("v").toInt();
    if(ch >= 1 && ch <= 512) dmxData[ch] = v;
    server.send(200, "text/plain", "OK");
  });

  server.on("/blackout", []() {
    autoPlay = false;
    memset(dmxData, 0, DMX_PACKET_SIZE);
    server.send(200, "text/plain", "OK");
  });

  server.on("/saveScene", []() {
    if(sceneCount < MAX_SCENES) {
      memcpy(scenes[sceneCount], dmxData, DMX_PACKET_SIZE);
      sceneNames[sceneCount] = "Scena " + String(sceneCount);
      sceneCount++;
      saveAllToFlash(); 
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/renameScene", []() {
    int id = server.arg("id").toInt();
    if(id >= 0 && id < sceneCount) { sceneNames[id] = server.arg("name"); saveAllToFlash(); }
    server.send(200, "text/plain", "OK");
  });

  server.on("/deleteScene", []() {
    int id = server.arg("id").toInt();
    if(id >= 0 && id < sceneCount) {
      for(int i = id; i < sceneCount - 1; i++) {
        memcpy(scenes[i], scenes[i+1], DMX_PACKET_SIZE);
        sceneNames[i] = sceneNames[i+1];
      }
      sceneCount--; saveAllToFlash();
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/updateScene", []() {
    int id = server.arg("id").toInt();
    if(id >= 0 && id < sceneCount) { memcpy(scenes[id], dmxData, DMX_PACKET_SIZE); saveAllToFlash(); }
    server.send(200, "text/plain", "OK");
  });

  server.on("/loadScene", []() {
    autoPlay = false;
    int id = server.arg("id").toInt();
    if(id >= 0 && id < sceneCount) memcpy(dmxData, scenes[id], DMX_PACKET_SIZE);
    server.send(200, "text/plain", "OK");
  });

  server.on("/addChaser", []() {
    if(chaserCount < MAX_CHASERS) { chasers[chaserCount].stepCount = 0; chaserCount++; saveAllToFlash(); }
    server.send(200, "text/plain", "OK");
  });

  server.on("/deleteChaser", []() {
    int id = server.arg("id").toInt();
    if(id >= 0 && id < chaserCount) {
      autoPlay = false;
      for(int i = id; i < chaserCount - 1; i++) memcpy(&chasers[i], &chasers[i+1], sizeof(Chaser));
      chaserCount--; saveAllToFlash();
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/saveChaser", []() {
    int id = server.arg("id").toInt();
    String seq = server.arg("seq");
    if(id >= 0 && id < chaserCount) {
      int count = 0, start = 0, idx = seq.indexOf(',');
      while(idx != -1 && count < MAX_STEPS) {
        chasers[id].steps[count++] = seq.substring(start, idx).toInt();
        start = idx + 1; idx = seq.indexOf(',', start);
      }
      if(count < MAX_STEPS && start < (int)seq.length()) chasers[id].steps[count++] = seq.substring(start).toInt();
      chasers[id].stepCount = count;
      saveAllToFlash();
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/playChaser", []() {
    activeChaser = server.arg("id").toInt();
    if(activeChaser >= 0 && activeChaser < chaserCount && chasers[activeChaser].stepCount > 0) {
      fadeDuration = server.arg("f").toInt(); holdDuration = server.arg("h").toInt();
      autoPlay = true; isFading = true; currentStepIndex = 0;
      targetScene = chasers[activeChaser].steps[currentStepIndex];
      if(targetScene >= sceneCount) targetScene = 0; 
      phaseStartTime = millis(); memcpy(fadeStartData, dmxData, DMX_PACKET_SIZE);
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/stop", []() {
    autoPlay = false;
    server.send(200, "text/plain", "OK");
  });

  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  if (autoPlay && activeChaser >= 0) {
    if (isFading) {
      if (fadeDuration == 0 || now - phaseStartTime >= fadeDuration) {
        memcpy(dmxData, scenes[targetScene], DMX_PACKET_SIZE);
        isFading = false; phaseStartTime = now;
      } else {
        float progress = (float)(now - phaseStartTime) / fadeDuration;
        for (int i = 1; i < DMX_PACKET_SIZE; i++) {
          dmxData[i] = fadeStartData[i] + (scenes[targetScene][i] - fadeStartData[i]) * progress;
        }
      }
    } else {
      if (now - phaseStartTime >= holdDuration) {
        currentStepIndex = (currentStepIndex + 1) % chasers[activeChaser].stepCount;
        targetScene = chasers[activeChaser].steps[currentStepIndex];
        if(targetScene >= sceneCount) targetScene = 0;
        memcpy(fadeStartData, dmxData, DMX_PACKET_SIZE);
        isFading = true; phaseStartTime = now;
      }
    }
  }

  if (now - lastUpdate > 30) {
    dmx_write(dmxPort, dmxData, DMX_PACKET_SIZE);
    dmx_send(dmxPort);
    lastUpdate = now;
  }
}