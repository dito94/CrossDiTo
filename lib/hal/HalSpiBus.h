#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class HalSpiBus {
 public:
  class Lock {
   public:
    explicit Lock(bool acquire = true);
    ~Lock();
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    explicit operator bool() const { return acquired; }

   private:
    bool acquired = false;
  };

  static HalSpiBus& getInstance();

  // The e-ink controller does not use SPI while its BUSY pin is asserted.
  // Display busy-wait hooks temporarily yield the shared bus so C3 SD work can
  // proceed, then reacquire it before the controller driver resumes.
  static void beginDisplayBusyWait();
  static void endDisplayBusyWait();

 private:
  HalSpiBus();

  SemaphoreHandle_t mutex = nullptr;
  TaskHandle_t busyWaitOwner = nullptr;
  uint8_t busyWaitDepth = 0;

  friend class Lock;
};
