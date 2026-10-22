#pragma once
#include <memory>
#include <Memory.h>
#include <cstdint>
#include "../../activities/Activity.h"

class SdEncryptionActivity final : public Activity {
 public:
  explicit SdEncryptionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SdEncryption", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<SdEncryptionActivity>(r, m);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { MENU, PROCESSING, DONE };
  State state = MENU;

  int menuIndex = 0;
  static constexpr int MENU_ITEMS = 3;

  int processedFiles = 0;
  int totalFiles = 0;
  bool lastOpEncrypt = true;
  bool hadError = false;
  char errorMsg[48] = {};

  static constexpr const char* CROSSPLAY_DIR  = "/crossplay";
  static constexpr const char* VERIFY_PATH    = "/crossplay/.encrypt_verify";
  static constexpr const char* MAGIC          = "BENC";
  static constexpr int         MAGIC_LEN      = 4;
  static constexpr int         IV_LEN         = 16;
  static constexpr int         KEY_LEN        = 32;
  static constexpr uint32_t    MAX_FILE_BYTES = 32 * 1024;

  static constexpr int MAX_FILES = 32;
  static constexpr int FNAME_MAX = 128;
  char fileNames[MAX_FILES][FNAME_MAX];

  bool    changePinPhase2 = false;
  uint8_t oldKey[KEY_LEN] = {};

  static void deriveKey(const char* pin, uint8_t key[32]);
  static void computeVerifyHash(const uint8_t key[32], uint8_t out[32]);

  void saveVerifyToken(const uint8_t key[32]);
  bool verifyPin(const uint8_t key[32]) const;

  static bool isEligibleExt(const char* name);

  bool encryptAll(const uint8_t key[32]);
  bool decryptAll(const uint8_t key[32]);

  bool encryptFile(const char* path, const uint8_t key[32]);
  bool decryptFile(const char* path, const uint8_t key[32]);

  int countEligible(bool forEncrypt) const;
  bool hasEncryptedFiles() const;

  unsigned long msgUntilMs = 0;
  char msgBuf[48] = {};
  void showMessage(const char* msg, unsigned long durationMs = 1400);

  void renderMenu() const;
  void renderProcessing() const;
  void renderDone() const;

  void launchChangePinPhase2();
};
