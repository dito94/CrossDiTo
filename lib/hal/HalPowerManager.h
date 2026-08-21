#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <Logging.h>
#include <Wire.h>
#include <freertos/semphr.h>
#include <sdkconfig.h>

#ifdef CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

#include <cassert>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

class HalPowerManager {
  int normalFreq = 0;  // MHz
  bool isLowPower = false;

  // I2C fuel gauge configuration for X3 battery monitoring
  bool _batteryUseI2C = false;            // True if using I2C fuel gauge (X3), false for ADC (X4)
  mutable int _batteryCachedPercent = 0;  // Last read battery percentage * 10 (0-1000); callers divide by 10 (ADC/X4
                                          // path only — I2C/X3 path stores 0-100 directly)
  mutable unsigned long _batteryLastPollMs = 0;  // Timestamp of last battery read in milliseconds

  enum LockMode { None, NormalSpeed };
  LockMode currentLockMode = None;
  SemaphoreHandle_t modeMutex = nullptr;  // Protect access to currentLockMode
  uint8_t displayBusyDepth = 0;
  bool displayBusyLoweredClock = false;
#ifdef CONFIG_PM_ENABLE
  // ESP-IDF owns the small lock objects; they are allocated once at startup
  // because its public API has no caller-provided/static-storage variant.
  esp_pm_lock_handle_t cpuMaxLock = nullptr;
  esp_pm_lock_handle_t noLightSleepLock = nullptr;
  bool automaticPmEnabled = false;
  bool cpuMaxLockHeld = false;
  bool setCpuMaxLockLocked(bool held);
#endif

 public:
#if defined(BOARD_HAS_PSRAM)
  static constexpr int LOW_POWER_FREQ = 80;  // MHz
#else
  static constexpr int LOW_POWER_FREQ = 10;  // MHz
#endif
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 300;  // ms
  static constexpr unsigned long BATTERY_POLL_MS = 1500;      // ms

  bool begin();

  // Control CPU frequency for power saving
  void setPowerSaving(bool enabled);

  // A panel refresh spends most of its time waiting on the BUSY pin. These
  // paired hooks lower CPU frequency during that wait even while a render lock
  // is active, then restore normal speed before display I/O resumes.
  void beginDisplayBusyWait();
  void endDisplayBusyWait();

  // Setup wake up GPIO and enter deep sleep
  // Should be called inside main loop() to handle the currentLockMode
  void startDeepSleep(HalGPIO& gpio) const;

  // Get battery percentage (range 0-100)
  uint16_t getBatteryPercentage() const;

  // RAII helper class to manage power saving locks
  // Usage: create an instance of Lock in a scope to disable power saving, for example when running a task that needs
  // full performance. When the Lock instance is destroyed (goes out of scope), power saving will be re-enabled.
  class Lock {
    friend class HalPowerManager;
    bool valid = false;
#ifdef CONFIG_PM_ENABLE
    bool noLightSleepHeld = false;
#endif

   public:
    explicit Lock();
    ~Lock();

    // Non-copyable and non-movable
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;
  };
};
