#include "../../src/apps_local/util/LocalWebText.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
  using local_web::validFileName;
  using local_web::appendJsonQuoted;
  using local_web::encodeHeaderFileName;

  for (const auto& name : {"a.txt", "report 2026.pdf", "space & symbols.txt",
                           "quote\".txt", ".hidden", "überblick.csv"}) {
    assert(validFileName(name));
  }
  for (const auto& name : {"", ".", "..", "../escape", "a/b", "a\\b",
                           "evil\nname", "evil\rname", "bad\tname"}) {
    assert(!validFileName(name));
  }
  assert(!validFileName(std::string("ok") + '\0' + "bad"));
  assert(validFileName(std::string(120, 'a')));
  assert(!validFileName(std::string(121, 'a')));

  std::string json;
  appendJsonQuoted(json, "A\"\\\n\r\t");
  assert(json == "\"A\\\"\\\\\\u000a\\u000d\\u0009\"");
  json.clear();
  appendJsonQuoted(json, "<img src=x onerror=alert(1)>");
  assert(json == "\"<img src=x onerror=alert(1)>\"");
  json.clear();
  appendJsonQuoted(json, std::string(1, '\x01'));
  assert(json == "\"\\u0001\"");

  assert(encodeHeaderFileName("report 2026.pdf") == "report%202026.pdf");
  assert(encodeHeaderFileName("quote\".txt") == "quote%22.txt");
  assert(encodeHeaderFileName("../escape") == "..%2Fescape");
  assert(encodeHeaderFileName("überblick.csv") == "%C3%BCberblick.csv");
  std::cout << "PASS local web file names, JSON escaping and header encoding\n";
}
