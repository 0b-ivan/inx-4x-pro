#pragma once

#include <string>

// Shared, Arduino-free validation for the explicitly labelled portal lab.
// No production credentials, identities or passwords are accepted or stored.
namespace portal_lab {

inline bool asciiAlnum(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9');
}

inline bool validSuffix(const std::string& suffix) {
  if (suffix.empty() || suffix.size() > 12) return false;
  for (char c : suffix) {
    if (!asciiAlnum(c) && c != '-') return false;
  }
  return true;
}

inline std::string ssidFor(const std::string& suffix) {
  if (!validSuffix(suffix)) return "X4PRO-PORTAL";
  return "X4PRO-PORTAL-" + suffix;
}

inline bool validTestId(const std::string& id) {
  constexpr char PREFIX[] = "X4TEST-";
  constexpr size_t PREFIX_LENGTH = sizeof(PREFIX) - 1;
  if (id.size() <= PREFIX_LENGTH || id.size() > 24 ||
      id.compare(0, PREFIX_LENGTH, PREFIX) != 0) return false;
  for (size_t i = PREFIX_LENGTH; i < id.size(); ++i) {
    if (!asciiAlnum(id[i])) return false;
  }
  return true;
}

}  // namespace portal_lab
