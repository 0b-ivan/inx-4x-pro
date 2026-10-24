#include "MdnsBrowserActivity.h"
#include "../Shelf.h"

#ifndef SIMULATOR
#include <ESPmDNS.h>
#else
#include "platform/sim_biscuit_network.h"
#endif
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <algorithm>
#include <string>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

// ---- static data ----

const MdnsBrowserActivity::ServiceType MdnsBrowserActivity::SERVICE_TYPES[NUM_SERVICES] = {
    {"_http._tcp",          "Web Servers",      "HTTP services, routers, IoT dashboards"},
    {"_https._tcp",         "Secure Web",       "HTTPS services"},
    {"_printer._tcp",       "Printers",         "Network printers (IPP/AirPrint)"},
    {"_ipp._tcp",           "IPP Printers",     "Internet Printing Protocol"},
    {"_googlecast._tcp",    "Chromecast",       "Google Cast devices"},
    {"_sonos._tcp",         "Sonos",            "Sonos speakers"},
    {"_homeassistant._tcp", "Home Assistant",   "Home automation"},
    {"_mqtt._tcp",          "MQTT Brokers",     "IoT message brokers"},
    {"_ssh._tcp",           "SSH Servers",      "Secure Shell services"},
    {"_ftp._tcp",           "FTP Servers",      "File Transfer Protocol"},
};

// ---- helpers ----

// Parse "_http._tcp" into type="http", proto="tcp"
// Returns false if the format is unexpected.
static bool parseServiceType(const char* full, char* typeOut, size_t typeLen,
                             char* protoOut, size_t protoLen) {
  // Expected format: _<type>._<proto>
  // e.g. "_http._tcp"
  const char* p = full;
  if (*p == '_') p++;

  const char* dot = strchr(p, '.');
  if (!dot) return false;

  size_t tLen = static_cast<size_t>(dot - p);
  if (tLen == 0 || tLen >= typeLen) return false;
  memcpy(typeOut, p, tLen);
  typeOut[tLen] = '\0';

  const char* q = dot + 1;
  if (*q == '_') q++;

  size_t pLen = strlen(q);
  if (pLen == 0 || pLen >= protoLen) return false;
  memcpy(protoOut, q, pLen);
  protoOut[pLen] = '\0';

  return true;
}

// ---- lifecycle ----

void MdnsBrowserActivity::onEnter() {
  Activity::onEnter();

  #ifdef SIMULATOR
  if (biscuit_sim::wifiConnected(WiFi.status() == WL_CONNECTED)) {
#else
  if (WiFi.status() == WL_CONNECTED) {
#endif
    state = SERVICE_SELECT;
    selectorIndex = 0;
    requestUpdate();
  } else {
    state = CHECK_WIFI;
    requestUpdate();
    RADIO.ensureWifi();
    startActivityForResult(
        std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
        [this](const ActivityResult& result) {
          #ifdef SIMULATOR
          if (!result.isCancelled && biscuit_sim::wifiConnected(WiFi.status() == WL_CONNECTED)) {
#else
          if (!result.isCancelled && WiFi.status() == WL_CONNECTED) {
#endif
            state = SERVICE_SELECT;
            selectorIndex = 0;
          } else {
            shelf::leave(renderer, mappedInput);
            return;
          }
          requestUpdate();
        });
  }
}

void MdnsBrowserActivity::onExit() {
  Activity::onExit();
  if (mdnsStarted) {
#ifndef SIMULATOR
    MDNS.end();
#endif
    mdnsStarted = false;
  }
  // Do NOT call RADIO.shutdown() — WiFi was pre-connected or managed externally.
}

// ---- query ----

void MdnsBrowserActivity::queryService(const char* serviceType) {
  char typeStr[32];
  char protoStr[16];
  if (!parseServiceType(serviceType, typeStr, sizeof(typeStr), protoStr, sizeof(protoStr))) {
    LOG_DBG("MDNS", "Failed to parse service type: %s", serviceType);
    return;
  }

#ifdef SIMULATOR
  const auto fixtures = biscuit_sim::mdnsDiscover(serviceType);
  for (const auto& fixture : fixtures) {
    if (results.size() >= 50) break;
    bool duplicate = false;
    for (const auto& existing : results) {
      if (existing.ip == fixture.ip && existing.port == fixture.port) { duplicate = true; break; }
    }
    if (duplicate) continue;
    ServiceResult item;
    item.hostname = fixture.host;
    item.ip = fixture.ip;
    item.port = fixture.port;
    item.serviceType = serviceType;
    item.instanceName = fixture.instance;
    results.push_back(std::move(item));
  }
#else
  int n = MDNS.queryService(typeStr, protoStr);
  LOG_DBG("MDNS", "queryService(%s, %s) => %d", typeStr, protoStr, n);

  for (int i = 0; i < n; i++) {
    if (static_cast<int>(results.size()) >= 50) break;

    std::string ip = MDNS.address(i).toString().c_str();
    uint16_t port = MDNS.port(i);

    // Deduplicate by ip + port
    bool dup = false;
    for (const auto& r : results) {
      if (r.ip == ip && r.port == port) { dup = true; break; }
    }
    if (dup) continue;

    ServiceResult sr;
    sr.hostname = MDNS.hostname(i).c_str();
    sr.ip = ip;
    sr.port = port;
    sr.serviceType = serviceType;
    sr.instanceName = MDNS.instanceName(i).c_str();
    results.push_back(std::move(sr));
  }
#endif
}

void MdnsBrowserActivity::startDiscovery(int serviceIdx) {
  state = BROWSING;
  results.clear();
  scanAllServices = false;
  scanServiceIdx = serviceIdx;

  if (!mdnsStarted) {
#ifndef SIMULATOR
    MDNS.begin("biscuit");
#endif
    mdnsStarted = true;
  }

  queryService(SERVICE_TYPES[serviceIdx].type);

  state = RESULTS;
  selectorIndex = 0;
  requestUpdate();
}

void MdnsBrowserActivity::startDiscoveryAll() {
  state = BROWSING;
  results.clear();
  scanAllServices = true;

  if (!mdnsStarted) {
#ifndef SIMULATOR
    MDNS.begin("biscuit");
#endif
    mdnsStarted = true;
  }

  // Show "Browsing..." before the blocking scan loop.
  requestUpdate(true);

  for (int i = 0; i < NUM_SERVICES; i++) {
    queryService(SERVICE_TYPES[i].type);
  }

  state = RESULTS;
  selectorIndex = 0;
  requestUpdate();
}

// ---- save ----

void MdnsBrowserActivity::saveToCsv() {
  Storage.mkdir("/crossplay/logs");

  char filename[64];
  snprintf(filename, sizeof(filename), "/crossplay/logs/mdns_%lu.csv", millis());

  auto file = Storage.open(filename, O_WRITE | O_CREAT | O_APPEND);
  if (!file) {
    LOG_DBG("MDNS", "Failed to open %s for writing", filename);
    return;
  }

  file.println("Hostname,IP,Port,ServiceType");
  for (const auto& r : results) {
    const std::string row = r.hostname + "," + r.ip + "," +
                            std::to_string(r.port) + "," + r.serviceType;
    file.println(row.c_str());
  }
  file.close();

  LOG_DBG("MDNS", "Saved %zu results to %s", results.size(), filename);
}

// ---- loop ----

void MdnsBrowserActivity::loop() {
  switch (state) {
    case CHECK_WIFI:
      // Handled by startActivityForResult callback in onEnter.
      break;

    case SERVICE_SELECT: {
      const int count = NUM_SERVICES + 1;  // index 0 = Scan All

      buttonNavigator.onNext([this, count] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count);
        requestUpdate();
      });
      buttonNavigator.onPrevious([this, count] {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count);
        requestUpdate();
      });

      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (selectorIndex == 0) startDiscoveryAll(); else startDiscovery(selectorIndex - 1);
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        shelf::leave(renderer, mappedInput);
        return;
      }
      {
        int tx_ = 0, ty_ = 0;
        if (mappedInput.wasScreenTapped(tx_, ty_)) {
          const int btn = GUI.tappedAlwaysHintButton(tx_, ty_, renderer);
          if (btn == 1) { shelf::leave(renderer, mappedInput); return; }
          if (btn == 2) { if (selectorIndex == 0) startDiscoveryAll(); else startDiscovery(selectorIndex - 1); return; }
          int touchRow = -1;
          const auto& m = UITheme::getInstance().getMetrics();
          const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
          const int contentH = renderer.getScreenHeight() - contentTop - m.buttonHintsHeight - m.verticalSpacing;
          const int rowStep = GUI.getListRowStep(false);
          const int pageItems = std::max(1, contentH / rowStep);
          const int pageStart = (selectorIndex / pageItems) * pageItems;
          const int pageCount = std::min(pageItems, count - pageStart);
          const auto rt = mappedInput.rowTouch(touchRow, contentTop, rowStep, pageCount, 0,
                                                renderer.getScreenWidth(), m.listRowHeight);
          if (rt == MappedInputManager::RowTouch::Tap && touchRow >= 0 && touchRow < pageCount) {
            selectorIndex = pageStart + touchRow; requestUpdate();
          }
        }
      }
      break;
    }

    case BROWSING:
      // Blocking scan is in progress; allow Back to abort.
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        state = SERVICE_SELECT;
        selectorIndex = 0;
        requestUpdate();
      }
      {
        int tx_ = 0, ty_ = 0;
        if (mappedInput.wasScreenTapped(tx_, ty_)) {
          if (GUI.tappedAlwaysHintButton(tx_, ty_, renderer) == 1) { state = SERVICE_SELECT; selectorIndex = 0; requestUpdate(); }
        }
      }
      break;

    case RESULTS: {
      const int count = static_cast<int>(results.size());

      buttonNavigator.onNext([this, count] {
        if (count > 0) {
          selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count);
          requestUpdate();
        }
      });
      buttonNavigator.onPrevious([this, count] {
        if (count > 0) {
          selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count);
          requestUpdate();
        }
      });

      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (mappedInput.getHeldTime() >= 500) {
          saveToCsv();
          requestUpdate();
        } else if (!results.empty()) {
          detailIndex = selectorIndex;
          state = DETAIL;
          requestUpdate();
        }
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        state = SERVICE_SELECT;
        selectorIndex = 0;
        requestUpdate();
      }
      {
        int tx_ = 0, ty_ = 0;
        if (mappedInput.wasScreenTapped(tx_, ty_)) {
          const int btn = GUI.tappedAlwaysHintButton(tx_, ty_, renderer);
          if (btn == 1) { state = SERVICE_SELECT; selectorIndex = 0; requestUpdate(); break; }
          if (btn == 2 && !results.empty()) { detailIndex = selectorIndex; state = DETAIL; requestUpdate(); break; }
          if (count > 0) {
            int touchRow = -1;
            const auto& m = UITheme::getInstance().getMetrics();
            const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
            const int contentH = renderer.getScreenHeight() - contentTop - m.buttonHintsHeight - m.verticalSpacing;
            const int rowStep = GUI.getListRowStep(false);
            const int pageItems = std::max(1, contentH / rowStep);
            const int pageStart = (selectorIndex / pageItems) * pageItems;
            const int pageCount = std::min(pageItems, count - pageStart);
            const auto rt = mappedInput.rowTouch(touchRow, contentTop, rowStep, pageCount, 0,
                                                  renderer.getScreenWidth(), m.listRowHeight);
            if (rt == MappedInputManager::RowTouch::Tap && touchRow >= 0 && touchRow < pageCount) {
              selectorIndex = pageStart + touchRow; requestUpdate();
            }
          }
        }
      }
      break;
    }

    case DETAIL:
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        state = RESULTS;
        requestUpdate();
      }
      {
        int tx_ = 0, ty_ = 0;
        if (mappedInput.wasScreenTapped(tx_, ty_)) {
          if (GUI.tappedAlwaysHintButton(tx_, ty_, renderer) == 1) { state = RESULTS; requestUpdate(); }
        }
      }
      break;
  }
}

// ---- render ----

void MdnsBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  switch (state) {
    case CHECK_WIFI: {
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "mDNS Browser");
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Connecting to WiFi...");
      break;
    }

    case SERVICE_SELECT: {
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "mDNS Browser");

      const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
      const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
      const int itemCount = NUM_SERVICES + 1;

      GUI.drawList(
          renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectorIndex,
          [](int index) -> std::string {
            if (index == 0) return "> Scan All Services";
            const auto& st = SERVICE_TYPES[index - 1];
            // e.g. "Web Servers (_http._tcp)"
            std::string label = st.label;
            label += " (";
            label += st.type;
            label += ')';
            return label;
          },
          [](int index) -> std::string {
            if (index == 0) return "Query all service types at once";
            return SERVICE_TYPES[index - 1].description;
          });

      const auto labels = mappedInput.mapLabels("Back", "Scan", "^", "v");
      GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case BROWSING: {
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "mDNS Browser");
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Browsing services...");
      break;
    }

    case RESULTS: {
      char subtitleBuf[24];
      snprintf(subtitleBuf, sizeof(subtitleBuf), "%d found", static_cast<int>(results.size()));
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "mDNS Results",
                     subtitleBuf);

      const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
      const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
      const int count = static_cast<int>(results.size());

      if (count == 0) {
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No services found");
      } else {
        GUI.drawList(
            renderer, Rect{0, contentTop, pageWidth, contentHeight}, count, selectorIndex,
            [this](int index) -> std::string {
              const auto& r = results[index];
              return r.hostname.empty() ? r.ip : r.hostname;
            },
            [this](int index) -> std::string {
              const auto& r = results[index];
              // Short service name: strip leading '_' and trailing '._tcp' / '._udp'
              const char* svcFull = r.serviceType.c_str();
              const char* svcStart = (svcFull[0] == '_') ? svcFull + 1 : svcFull;
              const char* dot = strchr(svcStart, '.');
              char shortSvc[24] = {};
              if (dot) {
                size_t len = static_cast<size_t>(dot - svcStart);
                if (len >= sizeof(shortSvc)) len = sizeof(shortSvc) - 1;
                memcpy(shortSvc, svcStart, len);
                shortSvc[len] = '\0';
              } else {
                strncpy(shortSvc, svcStart, sizeof(shortSvc) - 1);
              }
              char buf[64];
              snprintf(buf, sizeof(buf), "%s:%u [%s]", r.ip.c_str(), r.port, shortSvc);
              return buf;
            });
      }

      const auto labels = mappedInput.mapLabels("Back", "Detail", "^", "v");
      GUI.drawButtonHintsAlways(renderer, labels.btn1, "Hold: CSV", labels.btn3, labels.btn4);
      break;
    }

    case DETAIL: {
      if (detailIndex < 0 || detailIndex >= static_cast<int>(results.size())) break;
      const auto& r = results[detailIndex];

      const std::string headerTitle = r.hostname.empty() ? r.ip : r.hostname;
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                     headerTitle.c_str());

      const int leftPad = metrics.contentSidePadding;
      int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 20;
      const int lineH = 45;

      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Hostname", true, EpdFontFamily::BOLD);
      y += 22;
      renderer.drawText(UI_10_FONT_ID, leftPad, y, r.hostname.empty() ? "(none)" : r.hostname.c_str());
      y += lineH;

      renderer.drawText(SMALL_FONT_ID, leftPad, y, "IP", true, EpdFontFamily::BOLD);
      y += 22;
      renderer.drawText(UI_10_FONT_ID, leftPad, y, r.ip.c_str());
      y += lineH;

      char portBuf[12];
      snprintf(portBuf, sizeof(portBuf), "%u", r.port);
      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Port", true, EpdFontFamily::BOLD);
      y += 22;
      renderer.drawText(UI_10_FONT_ID, leftPad, y, portBuf);
      y += lineH;

      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Service Type", true, EpdFontFamily::BOLD);
      y += 22;
      renderer.drawText(UI_10_FONT_ID, leftPad, y, r.serviceType.c_str());
      y += lineH;

      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Instance Name", true, EpdFontFamily::BOLD);
      y += 22;
      renderer.drawText(UI_10_FONT_ID, leftPad, y, r.instanceName.empty() ? "(none)" : r.instanceName.c_str());

      const auto labels = mappedInput.mapLabels("Back", "", "", "");
      GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
  }

  renderer.displayBuffer();
}
