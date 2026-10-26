#pragma once

// Question language is part of the *content file*, not the UI translation.
// Keep a separate progress/flag state for each pack. Both versions use the
// same XTRIVIA v1 binary format; the English paths remain backward compatible.
namespace trivia {

struct PackLocation {
  const char* packPath;
  const char* statePath;
};

inline constexpr PackLocation kEnglishPackLocation{
    "/trivia/pack.dat", "/trivia/pack.state"};
inline constexpr PackLocation kGermanPackLocation{
    "/trivia/pack-de.dat", "/trivia/pack-de.state"};

inline constexpr const PackLocation& locationFor(const bool german) {
  return german ? kGermanPackLocation : kEnglishPackLocation;
}

inline constexpr bool shouldUseGerman(const bool germanUi, const bool germanPackAvailable) {
  return germanUi && germanPackAvailable;
}

}  // namespace trivia
