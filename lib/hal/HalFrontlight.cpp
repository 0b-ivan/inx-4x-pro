/**
 * @file HalFrontlight.cpp
 * @brief X4 Pro frontlight implementation backed by FreeInk FrontlightManager.
 */

#include <HalFrontlight.h>

HalFrontlight frontlight;

void HalFrontlight::begin(const uint8_t brightness, const uint8_t warmth, const bool on) {
  if (!manager.present()) {
    Serial.printf("[%lu] [LIGHT] no frontlight in active board profile\n", millis());
    return;
  }

#ifdef INX_X4PRO_PORT
  // Bench diagnostic from the confirmed X4 Pro wiring: GPIO8 is cool/white and
  // GPIO9 is warm; both are active-high. Test each channel independently before
  // LEDC is attached so a PWM/FrontlightManager problem cannot hide the hardware.
  Serial.printf("[%lu] [LIGHT] raw GPIO8 cool HIGH\n", millis());
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);
  digitalWrite(9, LOW);
  digitalWrite(8, HIGH);
  delay(700);
  digitalWrite(8, LOW);
  delay(250);

  Serial.printf("[%lu] [LIGHT] raw GPIO9 warm HIGH\n", millis());
  digitalWrite(8, LOW);
  digitalWrite(9, HIGH);
  delay(700);
  digitalWrite(9, LOW);
  delay(250);
#endif

  manager.begin();
  lastBrightness = brightness > 100 ? 100 : brightness;
  manager.setColorTemperature(warmth > 100 ? 100 : warmth);

#ifdef INX_X4PRO_PORT
  // Then probe the FreeInk PWM path separately.
  Serial.printf("[%lu] [LIGHT] PWM diagnostic pulse 100%%\n", millis());
  manager.setBrightness(100);
  delay(1200);
  manager.setBrightness(0);
#endif

  lit = on;
  manager.setBrightness(lit ? lastBrightness : 0);

  Serial.printf("[%lu] [LIGHT] ready brightness=%u%% warm=%u%% state=%s\n", millis(), lastBrightness,
                manager.colorTemperature(), lit ? "on" : "off");
}

void HalFrontlight::setBrightness(const uint8_t percent) {
  lastBrightness = percent > 100 ? 100 : percent;
  if (lit) {
    manager.setBrightness(lastBrightness);
  }
}

void HalFrontlight::setWarmth(const uint8_t warmPercent) {
  manager.setColorTemperature(warmPercent > 100 ? 100 : warmPercent);
}

void HalFrontlight::setOn(const bool on) {
  if (!manager.present() || on == lit) {
    return;
  }
  lit = on;
  manager.setBrightness(lit ? lastBrightness : 0);
  Serial.printf("[%lu] [LIGHT] %s brightness=%u%% warm=%u%%\n", millis(), lit ? "on" : "off", lastBrightness,
                manager.colorTemperature());
}
