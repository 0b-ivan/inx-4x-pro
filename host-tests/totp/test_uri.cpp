#include <cstdio>

#include "totp/TotpUri.h"

namespace {
int checks = 0;
int failed = 0;

void expect(const bool condition, const char* name) {
  ++checks;
  if (!condition) {
    ++failed;
    std::printf("FAIL totp-uri  %s\n", name);
  }
}
}  // namespace

int main() {
  totp::UriAccount account;

  expect(totp::parseUri(
             "otpauth://totp/Example%3Aalice%40example.com?secret=JBSWY3DPEHPK3PXP&issuer=Example",
             account),
         "parse standard URI");
  expect(account.name == "Example:alice@example.com", "decode account label");
  expect(account.secret == "JBSWY3DPEHPK3PXP", "keep normalized secret");
  expect(account.digits == 6 && account.period == 30, "standard defaults");

  expect(totp::parseUri(
             "otpauth://totp/alice?secret=jbsw%20y3dp-ehpk3pxp&issuer=Example&digits=8&period=60&algorithm=SHA1",
             account),
         "parse configured URI");
  expect(account.name == "Example: alice", "prefix issuer when label has no issuer");
  expect(account.secret == "JBSWY3DPEHPK3PXP", "normalize formatted secret");
  expect(account.digits == 8 && account.period == 60, "parse digits and period");

  expect(!totp::parseUri("otpauth://hotp/alice?secret=JBSWY3DPEHPK3PXP", account), "reject HOTP");
  expect(!totp::parseUri("otpauth://totp/alice?secret=JBSWY3DPEHPK3PXP&algorithm=SHA256", account),
         "reject unsupported algorithm");
  expect(!totp::parseUri("otpauth://totp/alice?secret=JBSWY3DPEHPK3PXP&digits=7", account),
         "reject unsupported digits");
  expect(!totp::parseUri("otpauth://totp/alice?secret=JBSWY3DPEHPK3PXP&period=0", account),
         "reject invalid period");
  expect(!totp::parseUri("otpauth://totp/alice?issuer=Example", account), "reject missing secret");
  expect(!totp::parseUri("otpauth://totp/alice%ZZ?secret=JBSWY3DPEHPK3PXP", account),
         "reject invalid percent encoding");

  std::printf("totp-uri: %d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}
