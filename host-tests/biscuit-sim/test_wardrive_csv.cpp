#include "../../src/apps_local/wardriving/WardriveCsv.h"
#include <cassert>
#include <iostream>
int main() {
  using wardrive_csv::escapeField;
  assert(escapeField("Home") == "\"Home\"");
  assert(escapeField("Cafe, WiFi") == "\"Cafe, WiFi\"");
  assert(escapeField("AP\"Test") == "\"AP\"\"Test\"");
  assert(escapeField("Line\nBreak\rHere") == "\"Line Break Here\"");
  assert(escapeField("=HYPERLINK(A1)") == "\"'=HYPERLINK(A1)\"");
  assert(escapeField("  +SUM(1,2)") == "\"  '+SUM(1,2)\"");
  assert(escapeField("@cmd") == "\"'@cmd\"");
  assert(escapeField(std::string("a\0b", 3)) == "\"ab\"");
  std::cout << "PASS wardriving CSV escaping and formula neutralization\n";
}
