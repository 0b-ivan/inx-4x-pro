#pragma once
#include <string>

namespace wardrive_write {
// Always close the file, including after short writes or SD disconnection.
template <typename File>
bool writeComplete(File& file, const std::string& line) {
  const bool complete = file.write(line.data(), line.size()) == line.size();
  const bool closed = file.close();
  return complete && closed;
}
}  // namespace wardrive_write
