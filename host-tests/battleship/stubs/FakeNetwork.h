#pragma once
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#define PROGMEM
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
constexpr int WIFI_AP = 1, WIFI_STA = 2, WIFI_OFF = 0, WL_CONNECTED = 3, HTTP_GET = 0, HTTP_POST = 1;
inline void delay(int) {}
inline uint32_t clockMs = 0;
inline uint32_t millis() { return clockMs; }

struct String {
  std::string value;
  String() = default;
  String(const char* text) : value(text ? text : "") {}
  String(std::string text) : value(std::move(text)) {}
  const char* c_str() const { return value.c_str(); }
  int toInt() const { return std::atoi(value.c_str()); }
  bool operator==(const char* rhs) const { return value == (rhs ? rhs : ""); }
  bool equalsIgnoreCase(const char* rhs) const {
    if (!rhs) return false;
    const std::string s(rhs);
    if (s.size()!=value.size()) return false;
    for(size_t i=0;i<s.size();++i)
      if(std::tolower(static_cast<unsigned char>(s[i])) !=
         std::tolower(static_cast<unsigned char>(value[i]))) return false;
    return true;
  }
};

struct IPAddress {
  std::string toString() const { return "192.168.4.1"; }
};
struct FakeWifi {
  bool available = true;
  int currentMode = 0;
  int maxSoftApClients = 0;
  void mode(int mode) { currentMode = mode; }
  bool softAP(const char*, const char*, int, bool, int maxClients) {
    maxSoftApClients = maxClients;
    return available;
  }
  void softAPdisconnect(bool) {}
  IPAddress softAPIP() { return {}; }
  IPAddress localIP() { return {}; }
  std::string SSID() { return "test"; }
  int status() { return available ? WL_CONNECTED : 0; }
  int stations = 0;
  int softAPgetStationNum() { return stations; }
};
// Heap telemetry (Arduino-ESP32 EspClass subset) with controllable values.
struct FakeEsp {
  uint32_t freeHeap = 180000, minFreeHeap = 150000, maxAlloc = 90000, freePsram = 7000000;
  uint32_t getFreeHeap() const { return freeHeap; }
  uint32_t getMinFreeHeap() const { return minFreeHeap; }
  uint32_t getMaxAllocHeap() const { return maxAlloc; }
  uint32_t getFreePsram() const { return freePsram; }
};
inline FakeEsp ESP;
inline FakeWifi WiFi;
enum class DNSReplyCode { NoError };
struct DNSServer {
  void setErrorReplyCode(DNSReplyCode) {}
  void start(int, const char*, IPAddress) {}
  void stop() {}
  void processNextRequest() {}
};
struct FakeMdns {
  void end() {}
  bool begin(const char*) { return true; }
  void addService(const char*, const char*, int) {}
};
inline FakeMdns MDNS;
struct WebServer {
  inline static WebServer* instance = nullptr;
  std::map<std::string, std::function<void()>> routes;
  std::map<std::string, String> args;
  std::string response;
  int requestMethod = HTTP_GET;
  explicit WebServer(int) { instance = this; }
  template <class F>
  void on(const char* path, int, F fn) {
    routes[path] = fn;
  }
  template <class F>
  void onNotFound(F) {}
  void begin() {}
  void stop() {}
  void handleClient() {}
  void sendHeader(const char*, const char*) {}
  void send_P(int, const char*, const char*, size_t) {}
  void send(int, const char*, const char* text) { response = text; }
  bool hasArg(const char* name) const { return name != nullptr && args.find(name) != args.end(); }
  String arg(const char* name) const {
    const auto it = name == nullptr ? args.end() : args.find(name);
    return it == args.end() ? String{} : it->second;
  }
  int method() { return requestMethod; }
};
enum WStype_t { WStype_CONNECTED, WStype_DISCONNECTED, WStype_TEXT, WStype_BIN, WStype_FRAGMENT_TEXT_START };
struct WebSocketsServer {
  inline static WebSocketsServer* instance = nullptr;
  std::function<void(uint8_t, WStype_t, uint8_t*, size_t)> callback;
  std::vector<std::string> messages;
  std::vector<int> recipients;
  std::function<bool(String,String)> headerValidator;
  std::vector<std::string> requiredHeaders;
  int clients = 0;
  std::vector<uint8_t> disconnected;
  bool closed = false;
  // Optional arduinoWebSockets slot model: lowest free slot number is
  // reused, 0 = unlimited. strictDelivery drops frames to closed slots.
  int slotLimit = 0;
  bool strictDelivery = false;
  std::set<uint8_t> open;
  int accept() {
    const int limit = slotLimit ? slotLimit : 255;
    for (int i = 0; i < limit; ++i)
      if (!open.count(static_cast<uint8_t>(i))) {
        event(WStype_CONNECTED, static_cast<uint8_t>(i));
        return i;
      }
    return -1;  // library refuses the TCP upgrade: no free client slot
  }
  explicit WebSocketsServer(int) { instance = this; }
  void begin() { closed = false; }
  void enableHeartbeat(int, int, int) {}
  template <class F>
  void onValidateHttpHeader(F fn, const char* const* mandatory, size_t count) {
    headerValidator = fn;
    requiredHeaders.clear();
    for(size_t i=0;i<count;++i) requiredHeaders.emplace_back(mandatory[i]);
  }
  // Simulates library handshake validation: missing required Origin rejects.
  bool wouldAcceptOrigin(const char* origin) const {
    if (!origin) return requiredHeaders.empty();
    return !headerValidator || headerValidator(String("Origin"), String(origin));
  }
  template <class F>
  void onEvent(F fn) {
    callback = fn;
  }
  void loop() {}
  void close() {
    clients = 0;
    closed = true;
    open.clear();
  }
  void disconnect(uint8_t client) {
    if (strictDelivery && !open.count(client)) return;
    disconnected.push_back(client);
    // Simulate synchronous disconnect event: the server must have already
    // moved authenticated seat ownership to the new socket.
    event(WStype_DISCONNECTED, client);
  }
  int connectedClients() { return clients; }
  bool sendTXT(uint8_t client, char* data, size_t size) {
    if (strictDelivery && !open.count(client)) return false;
    messages.emplace_back(data, size);
    recipients.push_back(client);
    return true;
  }
  void broadcastTXT(char* data, size_t size) {
    if (clients) {
      messages.emplace_back(data, size);
      recipients.push_back(-1);
    }
  }
  void event(WStype_t type, uint8_t client = 0, std::string payload = "") {
    if (type == WStype_CONNECTED) { ++clients; open.insert(client); }
    if (type == WStype_DISCONNECTED) { --clients; open.erase(client); }
    callback(client, type, reinterpret_cast<uint8_t*>(payload.data()), payload.size());
  }
};
