#pragma once
#include <cstddef>
#include <cstdint>

namespace bshipweb {
constexpr size_t kMaxCommandBytes = 128;
enum class CommandKind : uint8_t { Profile, Place, Randomize, Ready, Fire, Rematch, Resume };
struct Command {
  CommandKind kind = CommandKind::Profile;
  char token[33] = {};
  char resumeToken[33] = {};
  uint32_t revision = 0;
  uint8_t value[3] = {};
};
// Strict bounded JSON tuple grammar; no allocation, coercion or unknown fields.
bool parseCommand(const uint8_t* bytes, size_t size, Command& out);
struct PlacementView {
  uint32_t revision = 0;
  bool profile = false;
  bool ready = false;
  uint8_t slots[3] = {};
  // Only the browser's own draft. 255 means not placed.
  uint8_t bow[5] = {255, 255, 255, 255, 255};
  uint8_t horizontal[5] = {1, 1, 1, 1, 1};
};
size_t serializePlacement(const PlacementView& view, bool accepted, char* out, size_t capacity);
}  // namespace bshipweb
