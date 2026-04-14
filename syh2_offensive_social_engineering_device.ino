// ================== OSEP-32 (SYH2) ==================
// Offensive Social Engineering Platform
// Transferência de imagens via FTP (SimpleFTPServer by Renzo Mischianti)
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <ESP32FtpServer.h>

TFT_eSPI tft = TFT_eSPI();
AsyncWebServer server(80);
FtpServer ftpSrv;

SemaphoreHandle_t sdMutex;

static const int SD_CS  = 5;
static const int BL_PIN = 21;

// ===== Caminhos =====
static const char* SLIDES_DIR  = "/slides";
static const char* ORDER_FILE  = "/slides/order.txt";
static const char* CONFIG_FILE = "/config.txt";

// ===== AP fallback =====
const char* WIFI_AP_SSID = "OSEP-32(SYH2)";
const char* WIFI_AP_PASS = "solydsyh2";

// ===== Config =====
String   cfg_sta_ssid    = "";
String   cfg_sta_pass    = "";
String   cfg_panel_pass  = "solyd";
String   cfg_ftp_user    = "osep";
String   cfg_ftp_pass    = "osep1234";
uint8_t  cfg_brightness  = 255;
uint32_t cfg_slide_delay = 4000;

// ===== Estado =====
bool   wifiStaOk     = false;
String webAddress    = "";
bool   forceShowNow  = false;
String forceShowName = "";

volatile bool ftpDirty = false;
bool slideshowPaused = false;

static const int MAX_FILES = 200;
String images[MAX_FILES];
String found[MAX_FILES];   // global para não explodir o stack
int imageCount   = 0;
int currentIndex = 0;

// ===== PWM backlight =====
static const int BL_FREQ = 5000;
static const int BL_RES  = 8;

// ===== Auth =====
bool isAuthenticated(AsyncWebServerRequest *request) {
  if (!request->hasParam("token")) return false;
  return request->getParam("token")->value() == cfg_panel_pass;
}

// ===== JPEG callback =====
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return false;
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

// ===== Utils =====
void setBrightness(uint8_t b) {
  cfg_brightness = b;
  ledcWrite(BL_PIN, b);
}

bool isJpg(const String& n) {
  String s = n; s.toLowerCase();
  return s.endsWith(".jpg") || s.endsWith(".jpeg");
}

bool existsInArray(String arr[], int count, const String& value) {
  for (int i = 0; i < count; i++) if (arr[i] == value) return true;
  return false;
}

void sortArray(String arr[], int count) {
  for (int i = 0; i < count - 1; i++)
    for (int j = 0; j < count - i - 1; j++)
      if (arr[j] > arr[j+1]) { String t = arr[j]; arr[j] = arr[j+1]; arr[j+1] = t; }
}

String jsonEscape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  return out;
}

// ===== Config (sem mutex — chamador gerencia) =====
void saveConfig() {
  if (SD.exists(CONFIG_FILE)) SD.remove(CONFIG_FILE);
  File f = SD.open(CONFIG_FILE, FILE_WRITE);
  if (!f) return;
  f.println("ssid="        + cfg_sta_ssid);
  f.println("pass="        + cfg_sta_pass);
  f.println("panel_pass="  + cfg_panel_pass);
  f.println("ftp_user="    + cfg_ftp_user);
  f.println("ftp_pass="    + cfg_ftp_pass);
  f.println("brightness="  + String(cfg_brightness));
  f.println("slide_delay=" + String(cfg_slide_delay));
  f.close();
}

void loadConfig() {
  if (!SD.exists(CONFIG_FILE)) { saveConfig(); return; }
  File f = SD.open(CONFIG_FILE, FILE_READ);
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    if      (key == "ssid")        cfg_sta_ssid    = val;
    else if (key == "pass")        cfg_sta_pass    = val;
    else if (key == "panel_pass")  cfg_panel_pass  = val;
    else if (key == "ftp_user")    cfg_ftp_user    = val;
    else if (key == "ftp_pass")    cfg_ftp_pass    = val;
    else if (key == "brightness")  cfg_brightness  = (uint8_t)  constrain(val.toInt(), 1, 255);
    else if (key == "slide_delay") cfg_slide_delay = (uint32_t) constrain(val.toInt(), 1000, 30000);
  }
  f.close();
}

// ===== Ordem (sem mutex) =====
void saveOrder() {
  if (SD.exists(ORDER_FILE)) SD.remove(ORDER_FILE);
  File f = SD.open(ORDER_FILE, FILE_WRITE);
  if (!f) return;
  for (int i = 0; i < imageCount; i++) f.println(images[i]);
  f.close();
}

// ===== Carrega lista de imagens (toma/libera mutex) =====
void loadImagesFromSD() {
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) return;

  imageCount = 0;
  if (!SD.exists(SLIDES_DIR)) SD.mkdir(SLIDES_DIR);

  // found[] é global para evitar stack overflow (200 Strings na stack = crash)
  int foundCount = 0;
  for (int i = 0; i < MAX_FILES; i++) found[i] = "";

  File dir = SD.open(SLIDES_DIR);
  while (true) {
    File f = dir.openNextFile();
    if (!f) break;
    if (!f.isDirectory()) {
      String name = String(f.name());
      if (!name.endsWith("order.txt") && isJpg(name))
        if (foundCount < MAX_FILES) found[foundCount++] = name;
    }
    f.close();
  }
  dir.close();

  sortArray(found, foundCount);

  if (!SD.exists(ORDER_FILE)) {
    for (int i = 0; i < foundCount; i++) images[imageCount++] = found[i];
    saveOrder();
    xSemaphoreGive(sdMutex);
    return;
  }

  File ord = SD.open(ORDER_FILE, FILE_READ);
  if (!ord) {
    for (int i = 0; i < foundCount; i++) images[imageCount++] = found[i];
    xSemaphoreGive(sdMutex);
    return;
  }

  while (ord.available()) {
    String line = ord.readStringUntil('\n'); line.trim();
    if (line.length() > 0 && existsInArray(found, foundCount, line))
      if (imageCount < MAX_FILES && !existsInArray(images, imageCount, line))
        images[imageCount++] = line;
  }
  ord.close();

  for (int i = 0; i < foundCount; i++)
    if (!existsInArray(images, imageCount, found[i]))
      if (imageCount < MAX_FILES) images[imageCount++] = found[i];

  saveOrder();
  xSemaphoreGive(sdMutex);
}

void moveImage(int from, int to) {
  if (from < 0 || from >= imageCount || to < 0 || to >= imageCount || from == to) return;
  String tmp = images[from];
  if (from < to) for (int i = from; i < to; i++) images[i] = images[i+1];
  else           for (int i = from; i > to; i--) images[i] = images[i-1];
  images[to] = tmp;
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
    saveOrder();
    xSemaphoreGive(sdMutex);
  }
}

void deleteImageByName(const String& name) {
  String path = String(SLIDES_DIR) + "/" + name;
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
    if (SD.exists(path.c_str())) SD.remove(path.c_str());
    xSemaphoreGive(sdMutex);
  }
  int pos = -1;
  for (int i = 0; i < imageCount; i++) if (images[i] == name) { pos = i; break; }
  if (pos >= 0) {
    for (int i = pos; i < imageCount - 1; i++) images[i] = images[i+1];
    imageCount--;
  }
  if (currentIndex >= imageCount) currentIndex = max(0, imageCount - 1);
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
    saveOrder();
    xSemaphoreGive(sdMutex);
  }
}

// ===== TFT =====
void drawStatusOverlay(const String& line1, const String& line2 = "") {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(8, 10);
  tft.print(line1);
  if (line2.length()) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(8, 40);
    tft.print(line2);
  }
}

void drawImageByName(const String& name) {
  static bool busy = false;
  if (busy) return;
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) return;
  busy = true;
  tft.fillScreen(TFT_BLACK);
  String path = String(SLIDES_DIR) + "/" + name;
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) { busy = false; xSemaphoreGive(sdMutex); return; }
  TJpgDec.drawSdJpg(0, 0, f);
  f.close();
  busy = false;
  xSemaphoreGive(sdMutex);
}

// ===== Wi-Fi =====
void startWifi() {
  WiFi.mode(WIFI_AP_STA);
  wifiStaOk = false;
  if (cfg_sta_ssid.length() > 0) {
    WiFi.begin(cfg_sta_ssid.c_str(), cfg_sta_pass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) delay(500);
    if (WiFi.status() == WL_CONNECTED) {
      wifiStaOk  = true;
      webAddress = WiFi.localIP().toString();
    }
  }
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  if (!wifiStaOk) webAddress = WiFi.softAPIP().toString();
}

// ===== FTP callbacks =====
void startFtp() {
  // Garante que /slides existe antes de subir o FTP
  if (!SD.exists("/slides")) SD.mkdir("/slides");
  ftpSrv.begin(cfg_ftp_user.c_str(), cfg_ftp_pass.c_str());
}

// ===== JSON =====
String buildJsonList() {
  String json = "[";
  for (int i = 0; i < imageCount; i++) {
    if (i > 0) json += ",";
    json += "{\"name\":\"" + jsonEscape(images[i]) + "\",\"index\":" + String(i) + "}";
  }
  json += "]";
  return json;
}

// ===== HTML =====
String loginPage() {
  return R"HTML(
<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OSEP-32 Login</title>
<style>
body{margin:0;background:#0a0a0a;color:#fff;font-family:Arial;display:flex;align-items:center;justify-content:center;height:100vh}
.box{background:#111;border:1px solid #2a2a2a;border-radius:16px;padding:24px;width:320px}
h2{margin-top:0;color:#c1121f}
input,button{width:100%;padding:10px;border-radius:10px;border:1px solid #333;margin-top:10px;font-size:14px;box-sizing:border-box}
input{background:#0f0f0f;color:#fff}
button{background:#c1121f;color:#fff;border:none;cursor:pointer}
.small{font-size:12px;color:#aaa;margin-top:10px}
</style>
</head>
<body>
<div class="box">
  <h2>Offensive Social Engineering Platform</h2>
  <form action="/panel" method="GET">
    <input type="password" name="token" placeholder="Senha do painel">
    <button type="submit">Entrar</button>
  </form>
  <div class="small">Acesso protegido</div>
</div>
</body>
</html>
)HTML";
}

String panelPage() {
  return R"HTML(
<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Offensive Social Engineering Platform</title>
<style>
:root{--panel:#111;--panel2:#151515;--line:#2c2c2c;--red:#c1121f;--red2:#7f0b14;--txt:#f0f0f0;--muted:#9a9a9a}
*{box-sizing:border-box}
body{margin:0;background:linear-gradient(180deg,#050505,#101010);color:var(--txt);font-family:Arial}
.wrap{max-width:1200px;margin:0 auto;padding:20px}
.hero{background:radial-gradient(circle at top right,#240000 0%,#121212 40%,#090909 100%);border:1px solid var(--line);border-radius:18px;padding:20px;margin-bottom:18px}
.title{font-size:28px;font-weight:700;color:#fff}
.sub{margin-top:6px;color:var(--muted)}
.card{background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:16px;margin-bottom:18px}
h3{margin:0 0 12px}
input,button{font-size:14px}
input[type=text],input[type=password],input[type=number]{width:100%;padding:10px;background:#0f0f0f;border:1px solid #333;border-radius:10px;color:#fff}
button{background:linear-gradient(180deg,var(--red),var(--red2));color:#fff;border:none;padding:9px 12px;border-radius:10px;cursor:pointer}
button.sec{background:#222;border:1px solid #3a3a3a}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:14px}
.item{background:var(--panel2);border:1px solid #2b2b2b;border-radius:14px;padding:10px}
.thumb{width:100%;height:130px;object-fit:cover;background:#000;border-radius:10px;border:1px solid #3a3a3a}
.name{font-size:12px;word-break:break-all;margin:8px 0}
.row{display:flex;gap:6px;flex-wrap:wrap}
.status{margin-top:10px;font-size:13px;color:#9fd}
.small{color:#aaa;font-size:12px;margin-top:8px}
.ftp-box{background:#0d1a0d;border:1px solid #1a3a1a;border-radius:12px;padding:14px}
.ftp-box code{color:#4caf50;font-size:13px;display:block;margin-top:4px}
.cols{display:grid;grid-template-columns:1fr 1fr;gap:18px}
@media(max-width:900px){.cols{grid-template-columns:1fr}}
label{display:block;margin-top:10px;font-size:13px;color:var(--muted)}
</style>
</head>
<body>
<div class="wrap">
  <div class="hero">
    <div class="title">Offensive Social Engineering Platform (SYH2)</div>
    <div class="sub">Wi-Fi: __MODE__ &bull; __ADDR__</div>
  </div>
  <div class="cols">
    <div>
      <div class="card">
        <h3>Envio via FTP</h3>
        <div class="ftp-box">
          <div>Host: <code>__ADDR__</code></div>
          <div>Porta: <code>21</code></div>
          <div>Diretório: <code>/slides</code></div>
        </div>
        <div class="small">Use FileZilla ou WinSCP. Envie JPGs para /slides. A galeria atualiza automaticamente.</div>
        <div style="margin-top:12px;display:flex;gap:8px;flex-wrap:wrap">
          <button onclick="reloadGallery()">&#8635; Atualizar galeria</button>
          <button id="pauseBtn" class="sec" onclick="togglePause()">&#9646;&#9646; Pausar slideshow</button>
        </div>
        <div id="reloadStatus" class="status"></div>
      </div>

      <div class="card">
        <h3>Configurações</h3>
        <label>SSID Wi-Fi</label>
        <input type="text" id="wifi_ssid" value="__SSID__">
        <label>Senha Wi-Fi</label>
        <input type="password" id="wifi_pass" value="__WIFIPASS__">
        <label>Senha do painel</label>
        <input type="password" id="panel_pass" value="__PANELPASS__">
        <label>Usuário FTP</label>
        <input type="text" id="ftp_user" value="__FTP_USER__">
        <label>Senha FTP</label>
        <input type="password" id="ftp_pass" value="__FTP_PASS__">
        <label>Brilho (1-255)</label>
        <input type="number" id="brightness" min="1" max="255" value="__BRIGHT__">
        <label>Delay slideshow (ms)</label>
        <input type="number" id="slide_delay" min="1000" max="30000" value="__DELAY__">
        <div style="margin-top:12px;display:flex;gap:8px;flex-wrap:wrap">
          <button onclick="saveConfig()">Salvar</button>
          <button class="sec" onclick="rebootDevice()">Reiniciar</button>
        </div>
        <div id="cfgStatus" class="status"></div>
      </div>
    </div>

    <div>
      <div class="card">
        <h3>Campanhas Phishing &mdash; imagens</h3>
        <div id="gallery" class="grid"></div>
      </div>
    </div>
  </div>
</div>

<script>
const TOKEN = '__TOKEN__';

async function loadList() {
  const res = await fetch('/api/list?token=' + encodeURIComponent(TOKEN));
  const data = await res.json();
  const gallery = document.getElementById('gallery');
  gallery.innerHTML = '';
  data.forEach((item, i) => {
    const div = document.createElement('div'); div.className = 'item';
    const img = document.createElement('img'); img.className = 'thumb';
    img.src = '/img?token=' + encodeURIComponent(TOKEN) + '&name=' + encodeURIComponent(item.name) + '&t=' + Date.now();
    const nm = document.createElement('div'); nm.className = 'name'; nm.textContent = (i+1) + '. ' + item.name;
    const row = document.createElement('div'); row.className = 'row';

    const showBtn = document.createElement('button'); showBtn.textContent = 'Mostrar';
    showBtn.onclick = () => fetch('/api/show',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(TOKEN)+'&name='+encodeURIComponent(item.name)});

    const upBtn = document.createElement('button'); upBtn.className='sec'; upBtn.textContent='↑';
    upBtn.onclick = async () => { await fetch('/api/move',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(TOKEN)+'&from='+i+'&to='+Math.max(0,i-1)}); loadList(); };

    const dnBtn = document.createElement('button'); dnBtn.className='sec'; dnBtn.textContent='↓';
    dnBtn.onclick = async () => { await fetch('/api/move',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(TOKEN)+'&from='+i+'&to='+Math.min(data.length-1,i+1)}); loadList(); };

    const delBtn = document.createElement('button'); delBtn.className='sec'; delBtn.textContent='Excluir';
    delBtn.onclick = async () => { if(!confirm('Excluir '+item.name+'?')) return; await fetch('/api/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(TOKEN)+'&name='+encodeURIComponent(item.name)}); loadList(); };

    row.appendChild(showBtn); row.appendChild(upBtn); row.appendChild(dnBtn); row.appendChild(delBtn);
    div.appendChild(img); div.appendChild(nm); div.appendChild(row);
    gallery.appendChild(div);
  });
}

async function reloadGallery() {
  await fetch('/api/reload',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(TOKEN)});
  document.getElementById('reloadStatus').textContent = 'Atualizando...';
  setTimeout(() => { loadList(); document.getElementById('reloadStatus').textContent = ''; }, 1800);
}

async function saveConfig() {
  const body = 'token='+encodeURIComponent(TOKEN)
    +'&wifi_ssid='+encodeURIComponent(document.getElementById('wifi_ssid').value)
    +'&wifi_pass='+encodeURIComponent(document.getElementById('wifi_pass').value)
    +'&panel_pass='+encodeURIComponent(document.getElementById('panel_pass').value)
    +'&ftp_user='+encodeURIComponent(document.getElementById('ftp_user').value)
    +'&ftp_pass='+encodeURIComponent(document.getElementById('ftp_pass').value)
    +'&brightness='+encodeURIComponent(document.getElementById('brightness').value)
    +'&slide_delay='+encodeURIComponent(document.getElementById('slide_delay').value);
  const res = await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  document.getElementById('cfgStatus').textContent = await res.text();
}

async function rebootDevice() {
  await fetch('/api/reboot',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(TOKEN)});
  document.getElementById('cfgStatus').textContent = 'Reiniciando...';
}

async function togglePause() {
  const res = await fetch('/api/pause',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(TOKEN)});
  const state = await res.text();
  const btn = document.getElementById('pauseBtn');
  if (state === 'paused') {
    btn.textContent = '▶ Retomar slideshow';
    btn.style.background = '#1a5c1a';
    btn.style.border = '1px solid #2a8a2a';
  } else {
    btn.textContent = '⏸ Pausar slideshow';
    btn.style.background = '';
    btn.style.border = '';
    btn.className = 'sec';
  }
}

loadList();
</script>
</body>
</html>
)HTML";
}

String buildPanelPage(const String& token) {
  String html = panelPage();
  html.replace("__MODE__",     wifiStaOk ? "STA + AP" : "AP");
  html.replace("__ADDR__",     webAddress);
  html.replace("__TOKEN__",    token);
  html.replace("__SSID__",     cfg_sta_ssid);
  html.replace("__WIFIPASS__", cfg_sta_pass);
  html.replace("__PANELPASS__",cfg_panel_pass);
  html.replace("__FTP_USER__", cfg_ftp_user);
  html.replace("__FTP_PASS__", cfg_ftp_pass);
  html.replace("__BRIGHT__",   String(cfg_brightness));
  html.replace("__DELAY__",    String(cfg_slide_delay));
  return html;
}

// ===== Web =====
void startWeb() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html; charset=utf-8", loginPage());
  });

  server.on("/panel", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("token")) { request->send(403,"text/plain","Acesso negado"); return; }
    String token = request->getParam("token")->value();
    if (token != cfg_panel_pass) { request->send(403,"text/plain","Senha incorreta"); return; }
    request->send(200, "text/html; charset=utf-8", buildPanelPage(token));
  });

  server.on("/api/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) { request->send(403,"text/plain","Auth fail"); return; }
    request->send(200, "application/json", buildJsonList());
  });

  server.on("/img", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) { request->send(403,"text/plain","Auth fail"); return; }
    if (!request->hasParam("name")) { request->send(400,"text/plain","Missing name"); return; }
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) != pdTRUE) { request->send(500,"text/plain","SD busy"); return; }
    String name = request->getParam("name")->value();
    String path = String(SLIDES_DIR) + "/" + name;
    if (!SD.exists(path.c_str())) { xSemaphoreGive(sdMutex); request->send(404,"text/plain","Not found"); return; }
    AsyncWebServerResponse *response = request->beginResponse(SD, path, "image/jpeg");
    response->addHeader("Cache-Control","max-age=3600");
    xSemaphoreGive(sdMutex);
    request->send(response);
  });

  server.on("/api/reload", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("token",true) || request->getParam("token",true)->value() != cfg_panel_pass) {
      request->send(403,"text/plain","Auth fail"); return;
    }
    ftpDirty = true;
    request->send(200,"text/plain","OK");
  });

  server.on("/api/pause", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("token",true) || request->getParam("token",true)->value() != cfg_panel_pass) {
      request->send(403,"text/plain","Auth fail"); return;
    }
    slideshowPaused = !slideshowPaused;
    request->send(200,"text/plain", slideshowPaused ? "paused" : "running");
  });

  server.on("/api/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("token",true) || request->getParam("token",true)->value() != cfg_panel_pass) { request->send(403,"text/plain","Auth fail"); return; }
    if (!request->hasParam("name",true)) { request->send(400,"text/plain","Parametro ausente"); return; }
    deleteImageByName(request->getParam("name",true)->value());
    request->send(200,"text/plain","OK");
  });

  server.on("/api/move", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("token",true) || request->getParam("token",true)->value() != cfg_panel_pass) { request->send(403,"text/plain","Auth fail"); return; }
    if (!request->hasParam("from",true) || !request->hasParam("to",true)) { request->send(400,"text/plain","Parametros ausentes"); return; }
    moveImage(request->getParam("from",true)->value().toInt(), request->getParam("to",true)->value().toInt());
    request->send(200,"text/plain","OK");
  });

  server.on("/api/show", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("token",true) || request->getParam("token",true)->value() != cfg_panel_pass) { request->send(403,"text/plain","Auth fail"); return; }
    if (!request->hasParam("name",true)) { request->send(400,"text/plain","Parametro ausente"); return; }
    forceShowName = request->getParam("name",true)->value();
    forceShowNow  = true;
    request->send(200,"text/plain","OK");
  });

  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("token",true) || request->getParam("token",true)->value() != cfg_panel_pass) { request->send(403,"text/plain","Auth fail"); return; }
    if (request->hasParam("wifi_ssid", true))   cfg_sta_ssid    = request->getParam("wifi_ssid",  true)->value();
    if (request->hasParam("wifi_pass", true))   cfg_sta_pass    = request->getParam("wifi_pass",  true)->value();
    if (request->hasParam("panel_pass",true))   cfg_panel_pass  = request->getParam("panel_pass", true)->value();
    if (request->hasParam("ftp_user",  true))   cfg_ftp_user    = request->getParam("ftp_user",   true)->value();
    if (request->hasParam("ftp_pass",  true))   cfg_ftp_pass    = request->getParam("ftp_pass",   true)->value();
    if (request->hasParam("brightness",true)) {
      int b = constrain(request->getParam("brightness",true)->value().toInt(), 1, 255);
      setBrightness((uint8_t)b);
    }
    if (request->hasParam("slide_delay",true))
      cfg_slide_delay = (uint32_t)constrain(request->getParam("slide_delay",true)->value().toInt(), 1000, 30000);
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) { saveConfig(); xSemaphoreGive(sdMutex); }
    request->send(200,"text/plain","Config salva. Clique em reiniciar.");
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("token",true) || request->getParam("token",true)->value() != cfg_panel_pass) { request->send(403,"text/plain","Auth fail"); return; }
    request->send(200,"text/plain","Reiniciando...");
    delay(500);
    ESP.restart();
  });

  server.begin();
}

// ===== Setup =====
void setup() {
  sdMutex = xSemaphoreCreateMutex();
  if (!sdMutex) while(true);

  Serial.begin(115200);

  pinMode(BL_PIN, OUTPUT);
  digitalWrite(BL_PIN, HIGH);
  ledcAttach(BL_PIN, BL_FREQ, BL_RES);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  SPI.begin(18, 19, 23);

  if (!SD.begin(SD_CS)) {
    drawStatusOverlay("SD FAIL");
    while (true) delay(1000);
  }

  loadConfig();
  setBrightness(cfg_brightness);
  startWifi();
  startWeb();
  startFtp();
  loadImagesFromSD();

  drawStatusOverlay("OSEP-32 (SYH2)", webAddress);
  delay(1800);
}

// ===== Loop =====
void loop() {
  ftpSrv.handleFTP();

  // Recarrega lista se solicitado via /api/reload ou após FTP
  if (ftpDirty) {
    ftpDirty = false;
    delay(500); // aguarda FTP fechar o arquivo
    loadImagesFromSD();
  }

  static uint32_t lastMs = 0;
  uint32_t now = millis();

  if (forceShowNow) {
    forceShowNow = false;
    drawImageByName(forceShowName);
    for (int i = 0; i < imageCount; i++)
      if (images[i] == forceShowName) { currentIndex = i; break; }
    lastMs = now;
    return;
  }

  if (!slideshowPaused && now - lastMs >= cfg_slide_delay) {
    lastMs = now;
    if (imageCount > 0) {
      drawImageByName(images[currentIndex]);
      currentIndex = (currentIndex + 1) % imageCount;
    } else {
      drawStatusOverlay("Sem imagens", webAddress);
    }
  }
}
