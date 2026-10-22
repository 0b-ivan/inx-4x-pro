#pragma once
#include <memory>
#include <Memory.h>
#include <string>

#include "../../activities/Activity.h"
#include "../../util/ButtonNavigator.h"

class MorseCodeActivity final : public Activity {
 public:
  explicit MorseCodeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("MorseCode", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<MorseCodeActivity>(r, m);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { MODE_SELECT, MORSE_ENCODE, MORSE_DECODE, REFERENCE };

  State state = MODE_SELECT;
  int modeIndex = 0;
  ButtonNavigator buttonNavigator;

  std::string inputText;
  std::string morseOutput;

  std::string currentMorseChar;
  std::string decodedText;
  unsigned long lastKeyTime = 0;
  static constexpr unsigned long LONG_PRESS_MS = 400;

  static const char* morseTable[36];
  static const char morseChars[36];

  static std::string textToMorse(const std::string& text);
  static char morseToChar(const std::string& morse);
  void decodePendingChar();

  void renderModeSelect() const;
  void renderEncode() const;
  void renderDecode() const;
  void renderReference() const;
};
