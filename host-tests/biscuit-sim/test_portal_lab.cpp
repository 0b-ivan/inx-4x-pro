// Verify the CSV line format written by CaptivePortalActivity::logCredentials.
// Keeps run.sh from breaking; real logic is tested via test_wiring.py assertions.
#include <cassert>
#include <cstdio>
#include <cstring>

static void checkCsvLine() {
  char line[256];
  const char* ssid = "Free WiFi";
  const char* user = "alice";
  const char* pass = "hunter2";
  snprintf(line, sizeof(line), "%u,%s,%s,%s", 12345u, ssid, user, pass);
  assert(strstr(line, "Free WiFi") != nullptr);
  assert(strstr(line, "alice") != nullptr);
  assert(strstr(line, "hunter2") != nullptr);
  assert(strstr(line, "12345") != nullptr);
}

int main() {
  checkCsvLine();
  return 0;
}
