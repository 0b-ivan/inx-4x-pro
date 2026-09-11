#pragma once
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
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
};

struct IPAddress {
  std::string toString() const { return "192.168.4.1"; }
};
struct FakeWifi {
  bool available = true;
  int currentMode = 0;
  void mode(int mode) { currentMode = mode; }
  bool softAP(const char*, const char*, int, bool, int) { return available; }
  void softAPdisconnect(bool) {}
  IPAddress softAPIP() { return {}; }
  IPAddress localIP() { return {}; }
  std::string SSID() { return "test"; }
  int status() { return available ? WL_CONNECTED : 0; }
};
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
  int clients = 0;
  bool closed = false;
  explicit WebSocketsServer(int) { instance = this; }
  void begin() { closed = false; }
  void enableHeartbeat(int, int, int) {}
  template <class F>
  void onEvent(F fn) {
    callback = fn;
  }
  void loop() {}
  void close() {
    clients = 0;
    closed = true;
  }
  int connectedClients() { return clients; }
  void sendTXT(uint8_t client, char* data, size_t size) {
    messages.emplace_back(data, size);
    recipients.push_back(client);
  }
  void broadcastTXT(char* data, size_t size) {
    if (clients) {
      messages.emplace_back(data, size);
      recipients.push_back(-1);
    }
  }
  void event(WStype_t type, uint8_t client = 0, std::string payload = "") {
    if (type == WStype_CONNECTED) ++clients;
    if (type == WStype_DISCONNECTED) --clients;
    callback(client, type, reinterpret_cast<uint8_t*>(payload.data()), payload.size());
  }
};
