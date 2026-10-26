#include <cassert>
#include <cstring>
#include <cstdio>

#include "StudyDeckCatalog.h"

int main() {
  trivia::StudyDeckCatalog catalog;
  assert(catalog.count() == 0 && catalog.find("aws-devops") == -1);
  assert(catalog.addAwsFile("DOP-C02.dat"));
  assert(catalog.addFile("linux-admin.dat"));
  assert(catalog.addAwsFile("SAA-C03.dat"));
  assert(catalog.addFile("Networking_101.dat"));
  assert(catalog.count() == 4);
  assert(!catalog.addFile("linux-admin.dat"));
  assert(!catalog.addFile("DOP-C02.dat")); // same name in two folders is ambiguous
  assert(!catalog.addAwsFile("../escape.dat"));
  for (const char* bad : {"../escape.dat", "/foo.dat", "spaces in name.dat",
                          "api.json", "x.dat/../../../", ".dat", "invalid.exe",
                          "foo.dat.bak", "dots.name.dat", "", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA.dat"}) {
    assert(!catalog.addFile(bad));
  }
  char path[90];
  assert(catalog.path(0, ".dat", path, sizeof(path)));
  assert(std::strcmp(path, "/trivia/aws/DOP-C02.dat") == 0);
  assert(catalog.path(0, ".state", path, sizeof(path)));
  assert(std::strcmp(path, "/trivia/aws/DOP-C02.state") == 0);
  assert(catalog.path(2, ".dat", path, sizeof(path)));
  assert(std::strcmp(path, "/trivia/aws/SAA-C03.dat") == 0);
  assert(catalog.path(1, ".dat", path, sizeof(path)));
  assert(std::strcmp(path, "/trivia/decks/linux-admin.dat") == 0);
  assert(catalog.path(1, ".state.id", path, sizeof(path)));
  assert(std::strcmp(path, "/trivia/decks/linux-admin.state.id") == 0);
  assert(catalog.path(1, ".prefs", path, sizeof(path)));
  assert(std::strcmp(path, "/trivia/decks/linux-admin.prefs") == 0);
  assert(catalog.path(1, ".topics", path, sizeof(path)));
  assert(std::strcmp(path, "/trivia/decks/linux-admin.topics") == 0);
  char tiny[5];
  assert(!catalog.path(1, ".dat", tiny, sizeof(tiny)));
  assert(!catalog.path(1, ".txt", path, sizeof(path)));
  assert(catalog.find("linux-admin") == 1);
  for (int i = catalog.count(); i < trivia::kStudyMaxDecks; ++i) {
    char name[32];
    std::snprintf(name, sizeof(name), "DECK%d.dat", i);
    assert(catalog.addFile(name));
  }
  assert(!catalog.addFile("overflow.dat"));
  catalog.clear();
  assert(catalog.count() == 0 && catalog.find("linux-admin") < 0);
  std::puts("PASS study deck catalog: independent paths, safe filenames, bounded count");
}
