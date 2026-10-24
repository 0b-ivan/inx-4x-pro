#include "../../src/apps_local/wardriving/WardriveWrite.h"
#include <cassert>
#include <iostream>
#include <string>

struct FakeFile {
  size_t capacity = 100;
  bool closes = true;
  bool didClose = false;
  std::string bytes;
  size_t write(const void* data, size_t size) {
    const size_t n = size < capacity ? size : capacity;
    bytes.append(static_cast<const char*>(data), n);
    return n;
  }
  bool close() { didClose = true; return closes; }
};
int main() {
  FakeFile ok{};
  assert(wardrive_write::writeComplete(ok, "a,b,c\n"));
  assert(ok.didClose && ok.bytes == "a,b,c\n");
  FakeFile partial{};
  partial.capacity = 2;
  assert(!wardrive_write::writeComplete(partial, "hello"));
  assert(partial.didClose && partial.bytes == "he");
  FakeFile removed{};
  removed.closes = false;
  assert(!wardrive_write::writeComplete(removed, "hello"));
  assert(removed.didClose);
  FakeFile empty{};
  assert(wardrive_write::writeComplete(empty, ""));
  std::cout << "PASS Wardriving full/partial/close/empty SD writes\n";
}
