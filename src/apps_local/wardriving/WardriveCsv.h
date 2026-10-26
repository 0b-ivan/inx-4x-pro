#pragma once
#include <string>

// SSIDs are untrusted text from nearby radio beacons, not trusted CSV fields.
// Emit one physical line per observation, escape CSV quotes and neutralize
// spreadsheet formulas that begin with =, +, -, or @ (even after whitespace).
namespace wardrive_csv {
inline std::string escapeField(const std::string& input) {
  std::string clean;
  clean.reserve(input.size() + 4);
  for (unsigned char c : input) {
    if (c == 0) continue;
    clean += (c == '\r' || c == '\n') ? ' ' : static_cast<char>(c);
  }
  size_t start = clean.find_first_not_of(" \t");
  if (start != std::string::npos &&
      (clean[start] == '=' || clean[start] == '+' ||
       clean[start] == '-' || clean[start] == '@')) {
    clean.insert(start, 1, '\'');
  }
  std::string out = "\"";
  for (char c : clean) {
    if (c == '"') out += "\"";
    out += c;
  }
  return out + "\"";
}
}  // namespace wardrive_csv
