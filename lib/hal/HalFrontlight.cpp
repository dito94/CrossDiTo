#include "HalFrontlight.h"

#include <Logging.h>

namespace {
constexpr uint8_t brightnessPercentToLevel(const uint8_t percent) {
  return static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255U + 50U) / 100U);
}

static_assert(brightnessPercentToLevel(0) == 0);
static_assert(brightnessPercentToLevel(100) == 255);
}  // namespace

HalFrontlight HalFrontlight::instance;

void HalFrontlight::begin(const uint8_t brightness, const uint8_t warmth, const bool on) {
  if (!manager.present()) {
    return;
  }
  manager.begin();
  lastBrightness = brightness > 100 ? 100 : brightness;
  manager.setColorTemperature(warmth > 100 ? 100 : warmth);
  lit = on;
  manager.setBrightnessLevel(lit ? brightnessPercentToLevel(lastBrightness) : 0);
  LOG_INF("LIGHT", "Frontlight up: %u%% warm=%u%% %s", lastBrightness, manager.colorTemperature(), lit ? "on" : "off");
}

void HalFrontlight::setBrightness(const uint8_t percent) {
  lastBrightness = percent > 100 ? 100 : percent;
  if (lit) {
    manager.setBrightnessLevel(brightnessPercentToLevel(lastBrightness));
  }
}

void HalFrontlight::setWarmth(const uint8_t warmPercent) {
  manager.setColorTemperature(warmPercent > 100 ? 100 : warmPercent);
}

void HalFrontlight::setOn(const bool on) {
  if (on == lit) {
    return;
  }
  lit = on;
  manager.setBrightnessLevel(lit ? brightnessPercentToLevel(lastBrightness) : 0);
}
