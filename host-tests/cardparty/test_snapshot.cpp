// Crash-safe history snapshot: power fails after every single SD step of a
// save, including halfway through the .part write, and the next boot must
// still load the old or the new snapshot. Also: corrupt primary, backup
// promotion, and a real Book round trip.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../src/apps_local/cardparty/CardPartyHistory.h"
#include "../../src/apps_local/cardparty/CardPartySnapshot.h"

using namespace cardparty;

static int checks = 0, failures = 0;
#define CHECK(x)                                               \
  do {                                                         \
    ++checks;                                                  \
    if (!(x)) {                                                \
      ++failures;                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    }                                                          \
  } while (0)

namespace {
using Bytes = std::vector<uint8_t>;

// An SD card that loses power after `budget` mutating steps. A write that
// runs out of budget leaves the first half of its bytes behind.
struct FakeStore {
  std::map<std::string, Bytes> files;
  int budget = 1 << 30;
  bool alive() { return budget-- > 0; }
  bool exists(const char* p) { return files.count(p) != 0; }
  bool remove(const char* p) {
    if (!alive()) return false;
    files.erase(p);
    return true;
  }
  bool rename(const char* from, const char* to) {
    if (!alive() || !files.count(from)) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }
  bool write(const char* p, const uint8_t* b, size_t n) {
    if (!alive()) return false;
    files[p] = Bytes(b, b + (budget > 0 ? n : n / 2));
    return alive();
  }
  bool read(const char* p, Bytes& out, size_t max) {
    if (!files.count(p) || files[p].empty() || files[p].size() > max) return false;
    out = files[p];
    return true;
  }
};

constexpr snapshot::Files kFiles{"h.bin", "h.bin.part", "h.bin.bak"};

// A payload that only decodes whole: tag byte, then length check.
Bytes payload(uint8_t tag, size_t n) {
  Bytes b(n, tag);
  b[0] = static_cast<uint8_t>(n & 0xff);
  return b;
}
snapshot::Loaded loadTag(FakeStore& store, uint8_t& tag) {
  tag = 0;
  return snapshot::load(store, kFiles, 4096, [&](const uint8_t* b, size_t n) {
    if (n < 2 || b[0] != (n & 0xff)) return false;  // torn or corrupt
    tag = b[1];
    return true;
  });
}
}  // namespace

void powerLossAtEveryStep() {
  const Bytes oldBytes = payload(0xA1, 300), newBytes = payload(0xB2, 500);
  // Count the steps of one clean save over an existing snapshot.
  FakeStore probe;
  probe.files[kFiles.path] = oldBytes;
  probe.budget = 1 << 30;
  CHECK(snapshot::save(probe, kFiles, newBytes.data(), newBytes.size()));
  const int steps = (1 << 30) - probe.budget;
  CHECK(steps >= 5);
  for (int cut = 0; cut <= steps; ++cut) {
    FakeStore store;
    store.files[kFiles.path] = oldBytes;
    store.budget = cut;
    const bool saved = snapshot::save(store, kFiles, newBytes.data(), newBytes.size());
    store.budget = 1 << 30;  // power is back
    uint8_t tag = 0;
    const auto loaded = loadTag(store, tag);
    CHECK(loaded != snapshot::Loaded::None);
    CHECK(tag == 0xA1 || tag == 0xB2);
    if (saved) CHECK(tag == 0xB2);
    // After a load, the next save works again from whatever was left.
    CHECK(snapshot::save(store, kFiles, newBytes.data(), newBytes.size()));
    CHECK(loadTag(store, tag) == snapshot::Loaded::Primary && tag == 0xB2);
  }
  std::puts("PASS power loss after every SD step: old or new snapshot, never neither");
}

void corruptPrimaryRecoversBackup() {
  FakeStore store;
  store.files[kFiles.path] = payload(0xC3, 200);
  store.files[kFiles.path][0] ^= 0xff;  // torn primary
  store.files[kFiles.backup] = payload(0xA1, 300);
  uint8_t tag = 0;
  CHECK(loadTag(store, tag) == snapshot::Loaded::Backup && tag == 0xA1);
  // Promoted: the good copy is now the primary, the corrupt one is gone.
  CHECK(!store.exists(kFiles.backup));
  CHECK(loadTag(store, tag) == snapshot::Loaded::Primary && tag == 0xA1);
  // Both bad: nothing loads, and nothing is deleted.
  FakeStore broken;
  broken.files[kFiles.path] = Bytes{1, 2, 3};
  broken.files[kFiles.backup] = Bytes{4, 5, 6};
  CHECK(loadTag(broken, tag) == snapshot::Loaded::None);
  CHECK(broken.exists(kFiles.path) && broken.exists(kFiles.backup));
  // Empty card: nothing to load, first save creates the primary only.
  FakeStore empty;
  CHECK(loadTag(empty, tag) == snapshot::Loaded::None);
  const Bytes first = payload(0xD4, 64);
  CHECK(snapshot::save(empty, kFiles, first.data(), first.size()));
  CHECK(empty.exists(kFiles.path) && !empty.exists(kFiles.part) && !empty.exists(kFiles.backup));
  std::puts("PASS corrupt primary: backup promoted; both corrupt: nothing loaded or deleted");
}

void bookRoundTrip() {
  history::Book book;
  CHECK(book.upsert(UINT64_C(0x1234), "Ada", 1));
  CHECK(book.roundWon(UINT64_C(0x1234), "Drei vergessene Backups", 2));
  const Bytes bytes = book.encode();
  FakeStore store;
  CHECK(snapshot::save(store, kFiles, bytes.data(), bytes.size()));
  // A torn second save leaves the first snapshot loadable.
  CHECK(book.upsert(UINT64_C(0x5678), "Bert", 3));
  const Bytes second = book.encode();
  store.budget = 2;  // remove .part, then die halfway through the write
  CHECK(!snapshot::save(store, kFiles, second.data(), second.size()));
  store.budget = 1 << 30;
  history::Book restored;
  const auto loaded =
      snapshot::load(store, kFiles, 1 << 20, [&](const uint8_t* b, size_t n) { return restored.decode(b, n); });
  CHECK(loaded == snapshot::Loaded::Primary);
  CHECK(restored.players() == 1 && restored.player(UINT64_C(0x1234)) && restored.quotes() == 1);
  std::puts("PASS history book survives a torn save");
}

int main() {
  powerLossAtEveryStep();
  corruptPrimaryRecoversBackup();
  bookRoundTrip();
  std::printf("cardparty snapshot: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
