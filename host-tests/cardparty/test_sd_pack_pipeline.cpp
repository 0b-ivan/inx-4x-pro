// Python-built package -> production reader -> real eight-seat browser adapter.
#include <cassert>
#include <cstdio>
#include <string>
#include "../../src/apps_local/cardparty/CardPartySdPack.h"
#include "../../src/apps_local/cardparty/web/CardPartyBrowserServer.h"

namespace devmode {
void pause() {}
void resume() {}
}

class FileSource final : public cardparty::sdpack::ByteSource {
 public:
  explicit FileSource(const char* path) : file_(std::fopen(path, "rb")) {
    assert(file_);
    assert(std::fseek(file_, 0, SEEK_END) == 0);
    length_ = static_cast<uint32_t>(std::ftell(file_));
  }
  ~FileSource() override { std::fclose(file_); }
  uint32_t size() const override { return length_; }
  bool read(uint32_t offset, void* dst, uint32_t count) override {
    return offset <= length_ && count <= length_ - offset &&
           std::fseek(file_, offset, SEEK_SET) == 0 && std::fread(dst, 1, count, file_) == count;
  }
 private:
  FILE* file_;
  uint32_t length_ = 0;
};

static void snapshots(WebSocketsServer& ws, size_t from) {
  assert(ws.messages.size() == from + cardparty::kPlayers);
  for (size_t i = from; i < ws.messages.size(); ++i) {
    assert(ws.recipients[i] == static_cast<int>(i - from));
    std::printf("SNAPSHOT %s\n", ws.messages[i].c_str());
  }
}

int main(int argc, char** argv) {
  assert(argc == 2);
  FileSource file(argv[1]);
  cardparty::sdpack::Pack pack;
  assert(pack.load(file) && pack.version() == 2);
  assert(pack.deck().promptsCount == 1 && pack.deck().answersCount == 100);
  cardparty::Game game;
  assert(game.configure(pack.deck(), cardparty::Rules{10, 2, true}, 11));
  cardparty::Sessions sessions(game);
  cardparty::web::Server server(game, sessions, pack.deck());
  assert(server.begin(cardparty::web::Server::Mode::Hotspot));
  auto& ws = *WebSocketsServer::instance;
  std::string tokens[cardparty::kPlayers];
  for (uint8_t i = 0; i < cardparty::kPlayers; ++i) {
    ws.event(WStype_CONNECTED, i);
    size_t mark = ws.messages.size();
    ws.event(WStype_TEXT, i, std::string("{\"type\":\"join\",\"name\":\"Player ") + std::to_string(i) + "\"}");
    const std::string prefix = "{\"type\":\"welcome\",\"token\":\"";
    for (size_t j = mark; j < ws.messages.size(); ++j)
      if (ws.recipients[j] == i && ws.messages[j].rfind(prefix, 0) == 0)
        tokens[i] = ws.messages[j].substr(prefix.size(), 32);
    assert(tokens[i].size() == 32);
  }
  size_t mark = ws.messages.size();
  assert(server.hostStart());
  snapshots(ws, mark);
  auto view = game.publicView();
  const uint8_t czar = view.czar;
  for (uint8_t i = 0; i < cardparty::kPlayers; ++i) {
    if (i == czar) continue;
    std::string slots = "0";
    for (uint8_t j = 1; j < view.pick; ++j) slots += "," + std::to_string(j);
    mark = ws.messages.size();
    ws.event(WStype_TEXT, i, "{\"type\":\"submit\",\"token\":\"" + tokens[i] +
             "\",\"revision\":0,\"slots\":[" + slots + "]}");
  }
  assert(game.phase() == cardparty::Phase::Judging);
  snapshots(ws, mark);
  mark = ws.messages.size();
  ws.event(WStype_TEXT, czar, "{\"type\":\"judge\",\"token\":\"" + tokens[czar] +
           "\",\"revision\":0,\"selected\":0}");
  assert(game.phase() == cardparty::Phase::Reveal);
  snapshots(ws, mark);
  assert(server.diagnostics().sendFailures == 0);
  assert(server.hostNext());
  server.stop();
}
