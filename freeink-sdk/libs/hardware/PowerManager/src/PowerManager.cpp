#include "PowerManager.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <esp_sleep.h>
#include <soc/soc_caps.h>

namespace freeink {
namespace {
int8_t powerPin() { return BoardConfig::ACTIVE.input.power; }
bool powerActiveHigh() { return BoardConfig::ACTIVE.input.powerActiveHigh; }
}  // namespace

void PowerManager::armWakeOnPins(uint64_t gpioMask, bool wakeLow) {
#if SOC_PM_SUPPORT_EXT1_WAKEUP
  // Xtensa (S3/S2, classic ESP32): RTC ext1. Pins must be RTC GPIOs.
  //
  // The classic ESP32 RTC has no "any low" mode — only ESP_EXT1_WAKEUP_ALL_LOW
  // ("wake when ALL selected pins are low"). For a single wake pin (the common
  // power-button case) ALL_LOW and ANY_LOW are identical; a multi-pin low wake on
  // classic ESP32 fires only when every pin is low. S2/S3 expose ANY_LOW directly.
#if defined(CONFIG_IDF_TARGET_ESP32)
  const esp_sleep_ext1_wakeup_mode_t lowMode = ESP_EXT1_WAKEUP_ALL_LOW;
#else
  const esp_sleep_ext1_wakeup_mode_t lowMode = ESP_EXT1_WAKEUP_ANY_LOW;
#endif
  esp_sleep_enable_ext1_wakeup(gpioMask, wakeLow ? lowMode : ESP_EXT1_WAKEUP_ANY_HIGH);
#elif SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  // RISC-V (C3/C6/H2): the deep-sleep "gpio" wakeup source.
  esp_deep_sleep_enable_gpio_wakeup(gpioMask, wakeLow ? ESP_GPIO_WAKEUP_GPIO_LOW : ESP_GPIO_WAKEUP_GPIO_HIGH);
#else
#error "FreeInk PowerManager: target has no supported deep-sleep GPIO wakeup source"
#endif
}

bool PowerManager::armPowerButtonWakeup() {
  const int8_t pin = powerPin();
  if (pin < 0) return false;
  const bool activeHigh = powerActiveHigh();

  // Hold the idle level with the opposite pull so the line is defined in sleep.
  pinMode(pin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  armWakeOnPins(1ULL << pin, /*wakeLow=*/!activeHigh);
  return true;
}

void PowerManager::waitForPowerButtonRelease() {
  const int8_t pin = powerPin();
  if (pin < 0) return;
  const bool activeHigh = powerActiveHigh();

  pinMode(pin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  const int pressedLevel = activeHigh ? HIGH : LOW;
  while (digitalRead(pin) == pressedLevel) {
    delay(50);
  }
}

namespace {
// Drive a rail-enable pin to `level` and latch it so the level survives deep
// sleep (requires gpio_deep_sleep_hold_en(), done in deepSleep()). gpio_hold_dis
// first: a hold left over from a previous cycle would make the writes no-ops.
void holdRailAt(int8_t pin, uint8_t level) {
  if (pin < 0) return;
  const auto g = static_cast<gpio_num_t>(pin);
  gpio_hold_dis(g);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
  gpio_hold_en(g);
}
}  // namespace

void PowerManager::powerDownRailsForSleep(const bool keepTouchPowered) {
  const auto& b = BoardConfig::ACTIVE;
  // Keep RESET defined through deep sleep, but never drive an unpowered panel's
  // input HIGH: on boards with a gated EPD rail (Sticky), that can back-power the
  // controller through its RESET protection diode and turn sleep into a
  // milliamp-level drain. Hold RESET LOW alongside a switched-off rail. Boards
  // whose panel rail remains powered (X4 Pro) keep RESET HIGH so a UC8179 cannot
  // drift out of DSLP and restart its analog booster. EpdBus and XteinkDetect
  // release the hold before issuing a reset pulse on wake.
  const uint8_t resetSleepLevel = b.display.powerEnable >= 0 ? LOW : HIGH;
  holdRailAt(b.display.rst, resetSleepLevel);
  holdRailAt(b.display.powerEnable, LOW);
  // SD enable OFF = the inactive level: LOW for active-high enables, HIGH for the
  // active-low ones (e.g. X4 Pro's GPIO5, which powers the card while held LOW).
  holdRailAt(b.sd.powerEnable, b.sd.powerActiveHigh ? LOW : HIGH);

  // Some boards feed the touch rail from a master latch as well as the dedicated
  // touch enable. X4 Pro is exactly that: GPIO1 must remain HIGH while GPIO2 is
  // held LOW. holdPowerRails() normally leaves latch pins driven but not held;
  // esp_sleep_config_gpio_isolate() would therefore be free to float GPIO1. When
  // touch wake is requested, explicitly latch both master rails HIGH too.
  if (keepTouchPowered) {
    holdRailAt(b.power.latch0, HIGH);
    holdRailAt(b.power.latch1, HIGH);
  }

  // Normally the touch rail is switched off as well. A board that wants a touch
  // IRQ as a wake source must keep the controller alive across deep sleep, and
  // simply skipping this pin is not enough: esp_sleep_config_gpio_isolate() can
  // otherwise leave an unheld enable floating. Explicitly latch the ON level.
  const uint8_t touchSleepLevel = keepTouchPowered ? (b.touch.powerEnableActiveHigh ? HIGH : LOW)
                                                    : (b.touch.powerEnableActiveHigh ? LOW : HIGH);
  holdRailAt(b.touch.powerEnable, touchSleepLevel);

  // The mic enable also carries a polarity flag; OFF is the inactive level.
  holdRailAt(b.mic.enable, b.mic.enableActiveHigh ? LOW : HIGH);
}

void PowerManager::deepSleep() {
  esp_sleep_config_gpio_isolate();
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
  while (true) {
  }  // esp_deep_sleep_start() does not return; satisfy [[noreturn]]
}

void PowerManager::deepSleepUntilPowerButton() {
  waitForPowerButtonRelease();
  armPowerButtonWakeup();
  deepSleep();
}

}  // namespace freeink
