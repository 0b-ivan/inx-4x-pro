#pragma once

#include <cstddef>
#include <string>

// Shared, Arduino-free text handling for the local file and bulletin servers.
// Names arrive from HTTP and filesystem entries and are not trusted.
namespace local_web {
inline bool validFileName(const std::string& name) {
  if (name.empty() || name.size() > 120 || name == "." || name == "..")
    return false;
  for (unsigned char c : name) {
    if (c < 0x20 || c == 0x7f || c == '/' || c == '\\')
      return false;
  }
  return true;
}

inline void appendJsonQuoted(std::string& output, const std::string& value) {
  constexpr char hex[] = "0123456789abcdef";
  output.push_back('"');
  for (unsigned char c : value) {
    if (c == '"') output += "\\\"";
    else if (c == '\\') output += "\\\\";
    else if (c < 0x20) {
      output += "\\u00";
      output.push_back(hex[(c >> 4) & 0xf]);
      output.push_back(hex[c & 0xf]);
    } else output.push_back(static_cast<char>(c));
  }
  output.push_back('"');
}

// RFC 5987-style percent-encoding for an HTTP filename* parameter.
inline std::string encodeHeaderFileName(const std::string& name) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(name.size() * 3);
  for (unsigned char c : name) {
    const bool alnum = (c >= 'A' && c <= 'Z') ||
                       (c >= 'a' && c <= 'z') ||
                       (c >= '0' && c <= '9');
    if (alnum || c == '-' || c == '.' || c == '_' || c == '~') {
      encoded.push_back(static_cast<char>(c));
    } else {
      encoded.push_back('%');
      encoded.push_back(hex[c >> 4]);
      encoded.push_back(hex[c & 0x0f]);
    }
  }
  return encoded;
}
}  // namespace local_web
