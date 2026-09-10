#pragma once
namespace rsssync {
class WifiSession {
  bool owned = false;

 public:
  WifiSession() = default;
  WifiSession(const WifiSession&) = delete;
  WifiSession& operator=(const WifiSession&) = delete;
  bool connect();
  ~WifiSession();
};
}  // namespace rsssync
