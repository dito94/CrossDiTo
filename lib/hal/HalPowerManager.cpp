#include "HalPowerManager.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <PowerManager.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <soc/soc_caps.h>

#ifdef CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

#include <cassert>

#include "HalGPIO.h"

HalPowerManager powerManager;  // Singleton instance

namespace {
void disableWiFiBeforeDeepSleep() {
  const wifi_mode_t wifiMode = WiFi.getMode();
  if (wifiMode == WIFI_MODE_NULL) {
    return;
  }

  LOG_DBG("PWR", "Disabling WiFi before deep sleep (mode=%d)", static_cast<int>(wifiMode));
  if (wifiMode & WIFI_MODE_AP) {
    WiFi.softAPdisconnect(true);
  }
  if (wifiMode & WIFI_MODE_STA) {
    WiFi.disconnect(true);
  }
  delay(30);
  WiFi.mode(WIFI_OFF);
  delay(30);
}
}  // namespace

#ifdef CONFIG_PM_ENABLE
bool HalPowerManager::setCpuMaxLockLocked(const bool held) {
  if (!automaticPmEnabled || !cpuMaxLock || cpuMaxLockHeld == held) return true;
  const esp_err_t err = held ? esp_pm_lock_acquire(cpuMaxLock) : esp_pm_lock_release(cpuMaxLock);
  if (err != ESP_OK) {
    LOG_ERR("PWR", "Failed to %s CPU-max lock: %d", held ? "acquire" : "release", static_cast<int>(err));
    return false;
  }
  cpuMaxLockHeld = held;
  return true;
}
#endif

bool HalPowerManager::begin() {
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  }
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  if (!modeMutex) {
    LOG_ERR("PWR", "Failed to create power-mode mutex");
    return false;
  }

#ifdef CONFIG_PM_ENABLE
  const esp_pm_config_t pmConfig = {
      .max_freq_mhz = normalFreq,
      .min_freq_mhz = LOW_POWER_FREQ,
      .light_sleep_enable = true,
  };
  esp_err_t err = esp_pm_configure(&pmConfig);
  if (err == ESP_OK) {
    // These IDF handles are created once for the process lifetime. CPU_MAX is
    // the normal active-work guard; NO_LIGHT_SLEEP protects render/storage
    // critical sections while still allowing a lower clock during panel BUSY.
    err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "crossdito-active", &cpuMaxLock);
    if (err == ESP_OK) {
      err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "crossdito-io", &noLightSleepLock);
    }
  }
  if (err == ESP_OK) {
    automaticPmEnabled = true;
    if (!setCpuMaxLockLocked(true)) {
      automaticPmEnabled = false;
      err = ESP_FAIL;
    }
  }
  if (!automaticPmEnabled) {
    LOG_ERR("PWR", "Automatic power management unavailable (%d); using clock-only fallback", static_cast<int>(err));
    if (noLightSleepLock) {
      esp_pm_lock_delete(noLightSleepLock);
      noLightSleepLock = nullptr;
    }
    if (cpuMaxLock) {
      esp_pm_lock_delete(cpuMaxLock);
      cpuMaxLock = nullptr;
    }
    const esp_pm_config_t fallbackConfig = {
        .max_freq_mhz = normalFreq,
        .min_freq_mhz = normalFreq,
        .light_sleep_enable = false,
    };
    esp_pm_configure(&fallbackConfig);
  }
#endif
  return true;
}

void HalPowerManager::setPowerSaving(bool enabled) {
  if (normalFreq <= 0) {
    return;  // invalid state
  }

  if (modeMutex != nullptr) {
    xSemaphoreTake(modeMutex, portMAX_DELAY);
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    // Wifi is active, force disabling power saving
    enabled = false;
  }

  const LockMode mode = currentLockMode;

  if (mode == None && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
#ifdef CONFIG_PM_ENABLE
    if (automaticPmEnabled) {
      if (!setCpuMaxLockLocked(false)) {
        xSemaphoreGive(modeMutex);
        return;
      }
      isLowPower = true;
    } else
#endif
    {
      if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
        LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
        if (modeMutex != nullptr) {
          xSemaphoreGive(modeMutex);
        }
        return;
      }
      isLowPower = true;
    }

  } else if ((!enabled || mode != None) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
#ifdef CONFIG_PM_ENABLE
    if (automaticPmEnabled) {
      if (!setCpuMaxLockLocked(true)) {
        xSemaphoreGive(modeMutex);
        return;
      }
      isLowPower = false;
    } else
#endif
    {
      if (!setCpuFrequencyMhz(normalFreq)) {
        LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
        if (modeMutex != nullptr) {
          xSemaphoreGive(modeMutex);
        }
        return;
      }
      isLowPower = false;
    }
  }

  if (modeMutex != nullptr) {
    xSemaphoreGive(modeMutex);
  }

  // Otherwise, no change needed
}

void HalPowerManager::beginDisplayBusyWait() {
  if (!modeMutex || normalFreq <= 0) return;
  xSemaphoreTake(modeMutex, portMAX_DELAY);

  if (displayBusyDepth == UINT8_MAX) {
    LOG_ERR("PWR", "Display BUSY wait nesting overflow");
    xSemaphoreGive(modeMutex);
    return;
  }
  ++displayBusyDepth;
  if (displayBusyDepth == 1 && !isLowPower && WiFi.getMode() == WIFI_MODE_NULL) {
#ifdef CONFIG_PM_ENABLE
    if (automaticPmEnabled) {
      if (setCpuMaxLockLocked(false)) {
        isLowPower = true;
        displayBusyLoweredClock = true;
      }
    } else
#endif
    {
      if (setCpuFrequencyMhz(LOW_POWER_FREQ)) {
        isLowPower = true;
        displayBusyLoweredClock = true;
      } else {
        LOG_DBG("PWR", "Failed to lower CPU frequency during display BUSY wait");
      }
    }
  }

  xSemaphoreGive(modeMutex);
}

void HalPowerManager::endDisplayBusyWait() {
  if (!modeMutex) return;
  xSemaphoreTake(modeMutex, portMAX_DELAY);

  if (displayBusyDepth == 0) {
    LOG_ERR("PWR", "Unbalanced display BUSY wait end");
    xSemaphoreGive(modeMutex);
    return;
  }
  --displayBusyDepth;
  if (displayBusyDepth == 0 && displayBusyLoweredClock) {
#ifdef CONFIG_PM_ENABLE
    if (automaticPmEnabled) {
      if (setCpuMaxLockLocked(true)) {
        isLowPower = false;
      }
    } else
#endif
    {
      if (setCpuFrequencyMhz(normalFreq)) {
        isLowPower = false;
      } else {
        LOG_DBG("PWR", "Failed to restore CPU frequency after display BUSY wait");
      }
    }
    displayBusyLoweredClock = false;
  }

  xSemaphoreGive(modeMutex);
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio) const {
  disableWiFiBeforeDeepSleep();

  // Tear down HWCDC so the host sees a clean disconnect and the peripheral
  // doesn't hold power domains that interfere with USB-powered GPIO wake.
  endLogSerialTransport();

#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  // Release every configured battery latch. BoardConfig owns the pin mapping;
  // the collision guard prevents a stale/mismatched profile from driving a
  // display or SD bus pin low and holding it through sleep.
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0 || BoardConfig::latchConflictsWithBus(pin)) continue;
    const auto latch = static_cast<gpio_num_t>(pin);
    gpio_set_direction(latch, GPIO_MODE_OUTPUT);
    gpio_set_level(latch, 0);
    gpio_hold_en(latch);
  }
#endif

  // Cut the gated peripheral rails (touch/SD/EPD on boards like the Sticky) and
  // hold the enables off through deep sleep — otherwise the GT911 and SD card
  // stay powered all through "off" and drain the battery. No-op on boards with
  // no switched rails (X4/X3). Trade-off: no touch-to-wake; wake is the power
  // button. Must run after display.deepSleep() so the panel controller gets its
  // deep-sleep command while its rail is still up (enterDeepSleep() in main.cpp
  // guarantees that ordering).
  freeink::PowerManager::powerDownRailsForSleep();

  // The SDK convenience helper currently isolates every GPIO after arming the
  // wake source. On the ESP32-C3 that overwrites the power pin's sleep input
  // configuration, so short presses can be missed. Isolate first, then restore
  // and arm the board-configured power pin immediately before sleeping.
  freeink::PowerManager::waitForPowerButtonRelease();
  esp_sleep_config_gpio_isolate();
  freeink::PowerManager::armPowerButtonWakeup();
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
}

uint16_t HalPowerManager::getBatteryPercentage() const {
  static const BatteryMonitor battery;
  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    const unsigned long now = millis();
    if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) {
      return _batteryCachedPercent;
    }

    _batteryLastPollMs = now;
    uint16_t percent = 0;
    if (!battery.readPercentageChecked(percent)) {
      return _batteryCachedPercent;
    }
    _batteryCachedPercent = percent;
    return _batteryCachedPercent;
  }

  // smooth the battery %.
  if (_batteryCachedPercent == 0) {
    _batteryCachedPercent = 10 * battery.readPercentage();
  } else {
    _batteryCachedPercent = (_batteryCachedPercent * 9 + battery.readPercentage() * 10) / 10;
  }
  return _batteryCachedPercent / 10;
}

HalPowerManager::Lock::Lock() {
  if (!powerManager.modeMutex) {
    LOG_ERR("PWR", "Power-mode mutex is unavailable");
    return;
  }
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Current limitation: only one lock at a time
  if (powerManager.currentLockMode != None) {
    LOG_ERR("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
#ifdef CONFIG_PM_ENABLE
    if (powerManager.automaticPmEnabled && powerManager.noLightSleepLock) {
      const esp_err_t err = esp_pm_lock_acquire(powerManager.noLightSleepLock);
      if (err == ESP_OK) {
        noLightSleepHeld = true;
      } else {
        LOG_ERR("PWR", "Failed to acquire I/O sleep lock: %d", static_cast<int>(err));
      }
    }
#endif
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  if (!powerManager.modeMutex) return;
#ifdef CONFIG_PM_ENABLE
  if (noLightSleepHeld && powerManager.noLightSleepLock) {
    const esp_err_t err = esp_pm_lock_release(powerManager.noLightSleepLock);
    if (err != ESP_OK) {
      LOG_ERR("PWR", "Failed to release I/O sleep lock: %d", static_cast<int>(err));
    }
    noLightSleepHeld = false;
  }
#endif
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode = None;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
