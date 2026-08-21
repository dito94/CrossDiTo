#pragma once

#include <HalGPIO.h>

#include "AppCapabilities.h"

inline bool deviceHasEdgeSideButtons(const HalGPIO& gpio) {
#ifdef SIMULATOR
#ifdef SIMULATOR_DEVICE_X4_PRO
  return true;
#else
  return gpio.deviceIsX3();
#endif
#else
  return gpio.hasEdgeSideButtons();
#endif
}

inline bool deviceUsesSideButtonHintGutters(const HalGPIO& gpio) {
  if (!deviceHasEdgeSideButtons(gpio)) return false;
#if CROSSINK_APP_CAP_TOUCH
  return !gpio.hasTouch();
#else
  return true;
#endif
}
