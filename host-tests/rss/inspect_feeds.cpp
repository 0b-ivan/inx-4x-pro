// Exercise downloaded XML with the firmware parser and cache, without networking.
#include <HalStorage.h>
#include <RssParser.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "RssCache.h"
#include "RssTime.h"

int main(int argc, char** argv) {
  assert(argc == 4); // XML file, feed URL, simulator SD root
  std::ifstream input(argv[1], std::ios::binary);
  assert(input);
  RssParser parser;
  char chunk[257];
  while (input.read(chunk, sizeof(chunk)) || input.gcount())
    parser.write(reinterpret_cast<const uint8_t*>(chunk), input.gcount());
  parser.flush();
  assert(parser && !parser.getItems().empty());
  size_t dates = 0;
  for (const auto& item : parser.getItems()) {
    time_t epoch;
    if (rsstime::parsePublished(item.published, epoch)) ++dates;
    assert(!item.title.empty());
  }
  assert(dates == parser.getItems().size());
  RssFeed feed{parser.getFeedTitle(), argv[2], "", ""};
  std::vector<RssItem> merged, loaded;
  assert(rsscache::mergeAndSaveFeed(feed, parser.getFeedTitle(), parser.getItems(), merged));
  std::string title;
  assert(rsscache::loadFeed(feed, title, loaded));
  assert(loaded.size() == std::min(parser.getItems().size(), rsscache::MAX_CACHED_ITEMS));
  assert(loaded.front().title == parser.getItems().front().title);
  assert(loaded.front().content == parser.getItems().front().content);
  for (const auto& [path, bytes] : teststorage::files) {
    auto dest = std::filesystem::path(argv[3]) / path.substr(1);
    std::filesystem::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    assert(out);
  }
  std::cout << title << ": parsed=" << parser.getItems().size() << " cached=" << loaded.size()
            << " valid dates=" << dates << " first=" << rsstime::formatPublished(loaded.front().published, 56)
            << " content bytes=" << loaded.front().content.size() << "\n";
}
