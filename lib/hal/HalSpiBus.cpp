#include "HalSpiBus.h"

#include <Logging.h>

HalSpiBus::HalSpiBus() {
  mutex = xSemaphoreCreateRecursiveMutex();
  if (mutex == nullptr) {
    LOG_ERR("SPI", "Failed to create SPI bus mutex - bus is unusable");
  }
}

HalSpiBus& HalSpiBus::getInstance() {
  static HalSpiBus spiBus;
  return spiBus;
}

void HalSpiBus::beginDisplayBusyWait() {
  auto& bus = getInstance();
  if (!bus.mutex) return;

  const TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
  if (bus.busyWaitDepth > 0) {
    if (bus.busyWaitOwner == currentTask && bus.busyWaitDepth < UINT8_MAX) {
      ++bus.busyWaitDepth;
    } else {
      LOG_ERR("SPI", "Unexpected overlapping display BUSY wait");
    }
    return;
  }

  if (xSemaphoreGetMutexHolder(bus.mutex) != currentTask) return;
  if (xSemaphoreGiveRecursive(bus.mutex) != pdTRUE) {
    LOG_ERR("SPI", "Failed to yield SPI bus during display BUSY wait");
    return;
  }
  bus.busyWaitOwner = currentTask;
  bus.busyWaitDepth = 1;
}

void HalSpiBus::endDisplayBusyWait() {
  auto& bus = getInstance();
  if (bus.busyWaitDepth == 0) return;

  const TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
  if (bus.busyWaitOwner != currentTask) {
    LOG_ERR("SPI", "Display BUSY wait ended on a different task");
    return;
  }
  if (--bus.busyWaitDepth > 0) return;

  if (xSemaphoreTakeRecursive(bus.mutex, portMAX_DELAY) != pdTRUE) {
    LOG_ERR("SPI", "Failed to reacquire SPI bus after display BUSY wait");
  }
  bus.busyWaitOwner = nullptr;
}

HalSpiBus::Lock::Lock(const bool acquire) {
  if (!acquire) return;

  auto& bus = HalSpiBus::getInstance();
  if (bus.mutex == nullptr) {
    LOG_ERR("SPI", "SPI bus mutex not initialized, skipping lock");
    return;
  }
  const BaseType_t takeResult = xSemaphoreTakeRecursive(bus.mutex, portMAX_DELAY);
  if (takeResult != pdTRUE) {
    LOG_ERR("SPI", "Failed to acquire SPI bus mutex");
    return;
  }
  acquired = true;
}

HalSpiBus::Lock::~Lock() {
  if (!acquired) return;
  xSemaphoreGiveRecursive(HalSpiBus::getInstance().mutex);
}
