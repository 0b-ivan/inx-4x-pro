#include "DeadDropActivity.h"
#include "../util/LocalWebText.h"
#include "DevMode.h"
#include "../Shelf.h"

#include <GfxRenderer.h>
#include <algorithm>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_random.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/RadioManager.h"

// Minimal HTML page embedded in flash
static const char DROP_HTML[] PROGMEM =
    "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width\">"
    "<title>Dead Drop</title>"
    "<style>body{font-family:sans-serif;max-width:600px;margin:0 auto;padding:16px}"
    "h1{font-size:1.5em}input[type=file]{margin:8px 0}button{padding:8px 16px}"
    ".files a{display:block;padding:4px 0}</style></head>"
    "<body><h1>Dead Drop</h1>"
    "<form method=\"POST\" action=\"/upload\" enctype=\"multipart/form-data\">"
    "<input type=\"file\" name=\"file\"><button type=\"submit\">Upload</button></form>"
    "<h2>Files</h2><div class=\"files\" id=\"fl\">Loading...</div>"
    "<script>fetch('/list').then(r=>{if(!r.ok)throw Error('list');return r.json()})"
    ".then(f=>{const root=document.getElementById('fl');root.replaceChildren();"
    "f.forEach(n=>{const a=document.createElement('a');"
    "a.href='/download?file='+encodeURIComponent(n);a.textContent=n;root.appendChild(a)});"
    "if(!f.length)root.textContent='No files yet';})"
    ".catch(()=>{document.getElementById('fl').textContent='Could not load files';})"
    "</script>"
    "</body></html>";

// Static instance pointer for web handler lambdas
DeadDropActivity* DeadDropActivity::activeInstance = nullptr;

// constexpr array definitions (required for ODR)
constexpr int DeadDropActivity::TIMER_OPTIONS[];

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void DeadDropActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  state = CONFIG;
  configIndex = 0;
  timerIndex = 1;
  timerMinutes = TIMER_OPTIONS[timerIndex];
  filesReceived = 0;
  connectedClients = 0;
  uploadInProgress = false;
  uploadComplete = false;
  uploadHttpError = 0;
  uploadPath.clear();
  ssidName.clear();
  requestUpdate();
}

void DeadDropActivity::onExit() {
  // Release AP + HTTP server before Developer Mode retakes the WiFi radio.
  if (state == ACTIVE) {
    stopDrop();
  }
  devmode::resume();
  Activity::onExit();
}

// ---------------------------------------------------------------------------
// startDrop / stopDrop
// ---------------------------------------------------------------------------

void DeadDropActivity::startDrop() {
  Storage.mkdir("/crossplay");
  Storage.mkdir(DROP_DIR);

  // Generate random SSID suffix
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X", static_cast<uint16_t>(esp_random() & 0xFFFF));
  ssidName = std::string("DROP-") + suffix;

  RADIO.ensureWifi();
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssidName.c_str())) {
    LOG_ERR("DEAD", "Failed to start local AP: %s", ssidName.c_str());
    RADIO.shutdown();
    requestUpdate();
    return;
  }

  setupWebServer();

  startTime = millis();
  filesReceived = 0;
  connectedClients = 0;
  state = ACTIVE;
  activeInstance = this;
}

void DeadDropActivity::stopDrop() {
  if (server) {
    server->stop();
    delete server;
    server = nullptr;
  }
  // Close any file left open from an interrupted upload
  if (uploadInProgress) {
    uploadFile.close();
    uploadInProgress = false;
    if (!uploadPath.empty()) Storage.remove(uploadPath.c_str());
  }
  uploadPath.clear();
  WiFi.softAPdisconnect(true);
  RADIO.shutdown();
  activeInstance = nullptr;
}

// ---------------------------------------------------------------------------
// Web server setup
// ---------------------------------------------------------------------------

void DeadDropActivity::setupWebServer() {
  server = new WebServer(80);

  server->on("/", HTTP_GET, [this] { handleRoot(); });

  // Upload: second lambda is the upload data handler, first is called on completion
  server->on(
      "/upload", HTTP_POST, [this] { handleUpload(); }, [this] { handleUploadData(); });

  server->on("/list", HTTP_GET, [this] { handleFileList(); });
  server->on("/download", HTTP_GET, [this] { handleDownload(); });

  server->begin();
  WiFi.setSleep(false);
}

// ---------------------------------------------------------------------------
// Web handlers
// ---------------------------------------------------------------------------

void DeadDropActivity::handleRoot() {
  if (!server) return;
  server->send(200, "text/html", DROP_HTML);
}

void DeadDropActivity::handleUploadData() {
  if (!server) return;
  HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    if (uploadInProgress) {
      uploadFile.close();
      if (!uploadPath.empty()) Storage.remove(uploadPath.c_str());
    }
    uploadInProgress = false;
    uploadComplete = false;
    uploadHttpError = 0;
    uploadPath.clear();
    const std::string fname = upload.filename.c_str();
    if (!local_web::validFileName(fname)) {
      uploadHttpError = 400;
      return;
    }
    uploadPath = std::string(DROP_DIR) + fname;
    // Existing file can only be replaced after a validated basename.
    if (Storage.exists(uploadPath.c_str()))
      Storage.remove(uploadPath.c_str());
    uploadFile = Storage.open(uploadPath.c_str(), O_WRITE | O_CREAT | O_TRUNC);
    uploadInProgress = static_cast<bool>(uploadFile);
    if (!uploadInProgress) uploadHttpError = 507;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadHttpError) return;
    if (!uploadInProgress || !uploadFile ||
        uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
      uploadHttpError = 507;
      if (uploadInProgress) uploadFile.close();
      uploadInProgress = false;
      if (!uploadPath.empty()) Storage.remove(uploadPath.c_str());
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadHttpError) return;
    if (!uploadInProgress || !uploadFile) {
      uploadHttpError = 507;
      return;
    }
    uploadFile.close();
    uploadInProgress = false;
    uploadComplete = true;
    ++filesReceived;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadInProgress) uploadFile.close();
    uploadInProgress = false;
    if (!uploadPath.empty()) Storage.remove(uploadPath.c_str());
    uploadHttpError = 400;
  }
}

void DeadDropActivity::handleUpload() {
  if (!server) return;
  if (uploadHttpError || !uploadComplete) {
    server->send(uploadHttpError ? uploadHttpError : 400,
                 "text/plain", "Upload failed");
    return;
  }
  server->send(200, "text/plain", "OK");
}

void DeadDropActivity::handleFileList() {
  if (!server) return;
  constexpr size_t MAX_JSON_BYTES = 4096;
  std::string json = "[";
  bool tooLarge = false;
  HalFile dir = Storage.open(DROP_DIR, O_RDONLY);
  if (!dir) {
    server->send(503, "text/plain", "File storage unavailable");
    return;
  }
  bool first = true;
  HalFile entry;
  while ((entry = dir.openNextFile())) {
    if (!entry.isDirectory()) {
      char name[256] = {};
      entry.getName(name, sizeof(name));
      if (local_web::validFileName(name)) {
        std::string quoted;
        local_web::appendJsonQuoted(quoted, name);
        if (json.size() + quoted.size() + (first ? 0 : 1) + 1 > MAX_JSON_BYTES) {
          tooLarge = true;
          entry.close();
          break;
        }
        if (!first) json.push_back(',');
        json += quoted;
        first = false;
      }
    }
    entry.close();
  }
  dir.close();
  if (tooLarge) {
    server->send(507, "text/plain", "File listing exceeds size limit");
    return;
  }
  json.push_back(']');
  server->send(200, "application/json; charset=utf-8", json.c_str());
}

void DeadDropActivity::handleDownload() {
  if (!server) return;

  if (!server->hasArg("file")) {
    server->send(400, "text/plain", "Missing file param");
    return;
  }

  const std::string fname = server->arg("file").c_str();
  if (!local_web::validFileName(fname)) {
    server->send(400, "text/plain", "Invalid file name");
    return;
  }
  const String filePath = String(DROP_DIR) + fname.c_str();

  if (!Storage.exists(filePath.c_str())) {
    server->send(404, "text/plain", "Not found");
    return;
  }

  HalFile f = Storage.open(filePath.c_str(), O_RDONLY);
  if (!f) {
    server->send(500, "text/plain", "Open failed");
    return;
  }

  const std::string encoded = local_web::encodeHeaderFileName(fname);
  const String contentDisp = String("attachment; filename=\"download.bin\"; filename*=UTF-8''") + encoded.c_str();
  server->sendHeader("Content-Disposition", contentDisp);

  // HalFile doesn't support streamFile() — stream manually
  size_t fileSize = f.size();
  server->setContentLength(fileSize);
  server->send(200, "application/octet-stream", "");

  uint8_t streamBuf[512];
  while (fileSize > 0) {
    size_t chunk = (fileSize < sizeof(streamBuf)) ? fileSize : sizeof(streamBuf);
    int rd = f.read(streamBuf, chunk);
    if (rd <= 0) break;
    server->client().write(streamBuf, rd);
    fileSize -= rd;
  }
  f.close();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void DeadDropActivity::loop() {
  if (state == CONFIG) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
      return;
    }

    buttonNavigator.onNext([this] {
      configIndex = ButtonNavigator::nextIndex(configIndex, 2); requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      configIndex = ButtonNavigator::previousIndex(configIndex, 2); requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (configIndex == 0) { timerIndex = ButtonNavigator::nextIndex(timerIndex, TIMER_COUNT); timerMinutes = TIMER_OPTIONS[timerIndex]; requestUpdate(); }
      else { startDrop(); requestUpdate(); }
    }
    {
      int tx_ = 0, ty_ = 0;
      if (mappedInput.wasScreenTapped(tx_, ty_)) {
        const int btn = GUI.tappedAlwaysHintButton(tx_, ty_, renderer);
        if (btn == 1) { shelf::leave(renderer, mappedInput); return; }
        int tappedRow = -1;
        const auto& m = UITheme::getInstance().getMetrics();
        const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
        // The always-visible Biscuit footer occupies 40 pixels on touch screens.
        const int pageHeight = renderer.getScreenHeight();
        const int contentHeight = pageHeight - contentTop - BaseMetrics::values.buttonHintsHeight - m.verticalSpacing;
        const int rowStep = GUI.getListRowStep(false);
        const int rowCount = std::min(2, std::max(1, contentHeight / rowStep));
        const auto touch = mappedInput.rowTouch(tappedRow, contentTop, rowStep, rowCount, 0,
                                                renderer.getScreenWidth(), m.listRowHeight);
        if (touch != MappedInputManager::RowTouch::None && tappedRow >= 0 && tappedRow < 2) {
          configIndex = tappedRow;
          if (touch == MappedInputManager::RowTouch::Tap || btn == 2) {
            if (configIndex == 0) {
              timerIndex = ButtonNavigator::nextIndex(timerIndex, TIMER_COUNT);
              timerMinutes = TIMER_OPTIONS[timerIndex];
            } else { startDrop(); }
          }
          requestUpdate();
          return;
        }
        if (btn == 2) {
          if (configIndex == 0) {
            timerIndex = ButtonNavigator::nextIndex(timerIndex, TIMER_COUNT);
            timerMinutes = TIMER_OPTIONS[timerIndex];
          } else { startDrop(); }
          requestUpdate();
        }
      }
    }
    return;
  }

  if (state == ACTIVE) {
    if (server) server->handleClient();
    connectedClients = WiFi.softAPgetStationNum();

    unsigned long elapsed = millis() - startTime;
    if (elapsed >= static_cast<unsigned long>(timerMinutes) * 60000UL) {
      stopDrop(); state = DONE; requestUpdate(); return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      stopDrop(); state = DONE; requestUpdate(); return;
    }
    {
      int tx_ = 0, ty_ = 0;
      if (mappedInput.wasScreenTapped(tx_, ty_)) {
        if (GUI.tappedAlwaysHintButton(tx_, ty_, renderer) == 1) { stopDrop(); state = DONE; requestUpdate(); return; }
      }
    }

    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate >= 2000) {
      lastUpdate = millis();
      requestUpdate();
    }
    return;
  }

  if (state == DONE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      shelf::leave(renderer, mappedInput);
    }
    {
      int tx_ = 0, ty_ = 0;
      if (mappedInput.wasScreenTapped(tx_, ty_)) { shelf::leave(renderer, mappedInput); }
    }
    return;
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void DeadDropActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Dead Drop");

  switch (state) {
    case CONFIG:
      renderConfig();
      break;
    case ACTIVE:
      renderActive();
      break;
    case DONE:
      renderDone();
      break;
  }

  renderer.displayBuffer();
}

void DeadDropActivity::renderConfig() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - BaseMetrics::values.buttonHintsHeight - metrics.verticalSpacing;

  // Build timer label inline
  char timerLabel[32];
  snprintf(timerLabel, sizeof(timerLabel), "Timer: %d min", timerMinutes);

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, 2, configIndex,
      [&](int i) -> std::string {
        if (i == 0) return timerLabel;
        return "Start Dead Drop";
      });

  const auto labels = mappedInput.mapLabels("Back", "Select", "^", "v");
  GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DeadDropActivity::renderActive() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Compute time remaining
  unsigned long elapsed = millis() - startTime;
  unsigned long totalMs = static_cast<unsigned long>(timerMinutes) * 60000UL;
  unsigned long remainMs = (elapsed < totalMs) ? (totalMs - elapsed) : 0;
  unsigned int remainSec = static_cast<unsigned int>(remainMs / 1000);
  unsigned int remMin = remainSec / 60;
  unsigned int remSec = remainSec % 60;

  char timeBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", remMin, remSec);

  // Layout: centered block starting just below header
  const int startY = metrics.topPadding + metrics.headerHeight + 20;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int lineH12 = renderer.getLineHeight(UI_12_FONT_ID);
  int y = startY;

  // SSID — large and bold
  renderer.drawCenteredText(UI_12_FONT_ID, y, ssidName.c_str(), true, EpdFontFamily::BOLD);
  y += lineH12 + 8;

  // IP address
  renderer.drawCenteredText(UI_10_FONT_ID, y, "192.168.4.1", true);
  y += lineH + 16;

  // Time remaining — prominent
  renderer.drawCenteredText(UI_12_FONT_ID, y, "Time left:", true);
  y += lineH12 + 4;
  renderer.drawCenteredText(UI_12_FONT_ID, y, timeBuf, true, EpdFontFamily::BOLD);
  y += lineH12 + 16;

  // Files received
  char filesBuf[32];
  snprintf(filesBuf, sizeof(filesBuf), "Files: %d", filesReceived);
  renderer.drawCenteredText(UI_10_FONT_ID, y, filesBuf, true);
  y += lineH + 8;

  // Connected clients
  char clientsBuf[32];
  snprintf(clientsBuf, sizeof(clientsBuf), "Clients: %d", connectedClients);
  renderer.drawCenteredText(UI_10_FONT_ID, y, clientsBuf, true);

  const auto labels = mappedInput.mapLabels("Stop", "", "", "");
  GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DeadDropActivity::renderDone() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const int lineH12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);

  int y = pageHeight / 2 - 60;

  renderer.drawCenteredText(UI_12_FONT_ID, y, "Drop Complete", true, EpdFontFamily::BOLD);
  y += lineH12 + 16;

  char filesBuf[32];
  snprintf(filesBuf, sizeof(filesBuf), "Files received: %d", filesReceived);
  renderer.drawCenteredText(UI_10_FONT_ID, y, filesBuf, true);
  y += lineH + 8;

  renderer.drawCenteredText(UI_10_FONT_ID, y, ssidName.c_str(), true);

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
