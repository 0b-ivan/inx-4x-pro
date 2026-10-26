#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
inline std::map<std::string, std::string> responses;
struct HttpDownloader {
  static bool fetchUrl(const std::string& url, std::function<bool(const uint8_t*, size_t)> cb, const std::string&,
                       const std::string&) {
    auto it = responses.find(url);
    return it != responses.end() && cb(reinterpret_cast<const uint8_t*>(it->second.data()), it->second.size());
  }
};
