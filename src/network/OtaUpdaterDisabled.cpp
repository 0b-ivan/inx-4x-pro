/**
 * @file OtaUpdaterDisabled.cpp
 * @brief Fail-closed OTA implementation for the experimental X4 Pro port.
 *
 * The upstream Inx updater consumes generic X4/C3 firmware.bin releases. Until
 * X4-Pro-specific image metadata, board validation and rollback are implemented,
 * every firmware-write entry point is compiled out and replaced with these
 * non-writing stubs.
 */

#include "OtaUpdater.h"

bool OtaUpdater::isUpdateNewer() const { return false; }

const std::string& OtaUpdater::getLatestVersion() const {
  static const std::string disabled = "OTA disabled on X4 Pro alpha";
  return disabled;
}

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  updateAvailable = false;
  render = false;
  return NO_UPDATE;
}

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate() {
  render = false;
  return INTERNAL_UPDATE_ERROR;
}

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdateFromSd(const char*) {
  render = false;
  return INTERNAL_UPDATE_ERROR;
}
