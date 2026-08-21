#include "ActivityManager.h"

#include <CrossInkHalFrontlight.h>
#include <FontCacheManager.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "OpdsServerStore.h"
#include "SilentRestart.h"
#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "components/TouchRegistry.h"
#include "home/AlertActivity.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "home/HomeActivity.h"
#include "home/RecentBooksActivity.h"
#include "home/RecentBooksGridActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "network/NearbyBookTransferActivity.h"
#include "network/NearbyStatsSyncActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"
#include "util/FrontlightPanelActivity.h"
#include "util/FullScreenMessageActivity.h"

namespace {
constexpr uint32_t FILE_TRANSFER_MODE_MASK = 0xFF;
constexpr uint32_t FILE_TRANSFER_RETURN_TO_READER = 1U << 8;

uint32_t fileTransferBootPayload(const NetworkMode mode, const bool returnToReader) {
  return static_cast<uint32_t>(mode) | (returnToReader ? FILE_TRANSFER_RETURN_TO_READER : 0);
}

void restartToFileTransfer(const NetworkMode mode, const std::string& returnBookPath) {
  silentRestartToNetwork(NetworkBootTarget::FILE_TRANSFER, fileTransferBootPayload(mode, !returnBookPath.empty()));
}
}  // namespace

bool ActivityManager::begin(const uint32_t renderTaskStackBytes) {
  if (!renderingMutex) {
    renderingMutex = xSemaphoreCreateMutex();
    if (!renderingMutex) {
      LOG_ERR("ACT", "Failed to create rendering mutex");
      return false;
    }
  }

#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t renderTaskCore = 1;
#else
  constexpr BaseType_t renderTaskCore = 0;
#endif
  const BaseType_t taskResult = xTaskCreatePinnedToCore(
      &renderTaskTrampoline, "ActivityManagerRender", renderTaskStackBytes,
      this,               // Parameters
      1,                  // Priority
      &renderTaskHandle,  // Task handle
      renderTaskCore      // Keep long renders/cover decodes off CPU 0's idle watchdog when available
  );
  if (taskResult != pdPASS || !renderTaskHandle) {
    renderTaskHandle = nullptr;
    LOG_ERR("ACT", "Failed to create %lu-byte render task (free=%u maxAlloc=%u)",
            static_cast<unsigned long>(renderTaskStackBytes), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return false;
  }
  LOG_DBG("ACT", "Render task started with %lu-byte stack", static_cast<unsigned long>(renderTaskStackBytes));
  return true;
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

void ActivityManager::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Acquire the lock before reading currentActivity to avoid a TOCTOU race
    // where the main task deletes the activity between the null-check and render().
    RenderLock lock;
    TouchRegistry::getInstance().setEnabled(mappedInput.hasTouch());
    TouchRegistry::getInstance().beginFrame();
    if (currentActivity) {
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      currentActivity->render(std::move(lock));
    }
    TouchRegistry::getInstance().publish();
    // Notify any task blocked in requestUpdateAndWait() that the render is done.
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&renderStateMux);
    waiter = waitingTaskHandle;
    waitingTaskHandle = nullptr;
    taskEXIT_CRITICAL(&renderStateMux);
    if (waiter) {
      xTaskNotify(waiter, 1, eIncrement);
    }

    const uint32_t freeStack = uxTaskGetStackHighWaterMark(nullptr);
    if (freeStack < renderTaskMinFreeStack) {
      renderTaskMinFreeStack = freeStack;
      LOG_DBG("ACT", "Render task minimum free stack: %u bytes", static_cast<unsigned>(freeStack));
    }
  }
}

void ActivityManager::loop() {
  if (currentActivity) {
    mappedInput.setPowerAsConfirmInReaderMode(currentActivity->allowPowerAsConfirmInReaderMode());

    // Frontlight quick panel: top-edge down-swipe on home-key boards, except
    // that the open EPUB reader exposes the same action across the whole page.
    // Pushed, so it returns to whatever was underneath — including mid-book.
    const bool lightPanelGesture = currentActivity->usesFullScreenReaderVerticalSwipes()
                                       ? mappedInput.wasReaderLightPanelGesture()
                                       : mappedInput.wasLightPanelGesture();
    if (Frontlight.present() && currentActivity->name != "FrontlightPanel" &&
        currentActivity->allowFrontlightPanelGesture() && lightPanelGesture) {
      pushActivity(makeUniqueNoThrow<FrontlightPanelActivity>(renderer, mappedInput));
      return;
    }
    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    if (!handleReaderPowerButtonSettingsOverride() && !handleGlobalHomeGesture()) {
      currentActivity->loop();
    }
  } else {
    mappedInput.setPowerAsConfirmInReaderMode(false);
  }

  while (pendingAction != PendingAction::None) {
    if (pendingAction == PendingAction::Pop) {
      RenderLock lock;

      if (!currentActivity) {
        // Should never happen in practice
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction = PendingAction::None;
        continue;
      }

      ActivityResult pendingResult = std::move(currentActivity->result);

      // Destroy the current activity
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackedActivityCount == 0) {
        lock.unlock();  // goHome may acquire its own lock
        goHome();
        continue;  // Will launch goHome immediately

      } else {
        currentActivity = std::move(stackActivities[--stackedActivityCount]);
        // Handle result if necessary
        if (currentActivity->resultHandler) {
          // Move it here to avoid the case where handler calling another startActivityForResult()
          auto handler = std::move(currentActivity->resultHandler);
          currentActivity->resultHandler = nullptr;
          lock.unlock();  // Handler may acquire its own lock
          handler(pendingResult);
        }

        // Queue an update to ensure the popped activity gets re-rendered.
        // Do not block here: result handlers may transiently take RenderLock while
        // reconciling state, and a synchronous wait at this point can trip the
        // deadlock guard even though the queued repaint is sufficient.
        if (pendingAction == PendingAction::None) {
          lock.unlock();
          requestUpdate();
        }

        // Handler may request another pending action, we will handle it in the next loop iteration
        continue;
      }

    } else if (pendingActivity) {
      // Current activity has requested a new activity to be launched
      RenderLock lock;

      if (pendingAction == PendingAction::Replace) {
        // Destroy the current activity
        exitActivity(lock);
        // Clear the stack
        while (stackedActivityCount > 0) {
          auto& stacked = stackActivities[--stackedActivityCount];
          stacked->onExit();
          stacked.reset();
        }
      } else if (pendingAction == PendingAction::Push) {
        if (stackedActivityCount >= stackActivities.size()) {
          LOG_ERR("ACT", "Activity stack is full; dropping %s", pendingActivity->name.c_str());
          pendingActivity.reset();
          pendingAction = PendingAction::None;
          continue;
        }
        // Move current activity to stack
        stackActivities[stackedActivityCount++] = std::move(currentActivity);
      }
      pendingAction = PendingAction::None;
      currentActivity = std::move(pendingActivity);

      lock.unlock();  // onEnter may acquire its own lock
      currentActivity->onEnter();

      // onEnter may request another pending action, we will handle it in the next loop iteration
      continue;
    }
  }

  if (APP_STATE.hasPendingAlert.load(std::memory_order_acquire) && pendingAction == PendingAction::None) {
    APP_STATE.hasPendingAlert.store(false, std::memory_order_relaxed);
    pushActivity(makeUniqueNoThrow<AlertActivity>(renderer, mappedInput));
  }

  if (requestedUpdate.exchange(false)) {
    // Using direct notification to signal the render task to update
    // Increment counter so multiple rapid calls won't be lost
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  }
}

bool ActivityManager::handleGlobalHomeGesture() {
  if (!currentActivity || pendingAction != PendingAction::None || currentActivity->isHomeActivity() ||
      !currentActivity->allowGlobalHomeGesture() || (!mappedInput.hasTouch() && !mappedInput.hasHomeKey())) {
    return false;
  }

  const bool homeGesture = currentActivity->usesFullScreenReaderVerticalSwipes() ? mappedInput.wasReaderHomeGesture()
                                                                                 : mappedInput.wasHomeGesture();
  if (!homeGesture) {
    return false;
  }

  if (currentActivity->handleHomeGesture()) {
    return true;
  }

  goHome();
  return true;
}

bool ActivityManager::handleReaderPowerButtonSettingsOverride() {
  if (!readerPowerButtonOpensSettings()) {
    return false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    if (!currentActivity->openReaderSettingsMenu()) {
      goToSettings();
    }
    return true;
  }

  // Do not let reader activities run configured short/long Power actions while
  // the button is held. Its release is reserved for restoring Settings access.
  return mappedInput.isPressed(MappedInputManager::Button::Power);
}

void ActivityManager::exitActivity(const RenderLock& lock) {
  // Note: lock must be held by the caller
  if (currentActivity) {
    TouchRegistry::getInstance().clear();
    currentActivity->onExit();
    currentActivity.reset();
  }
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  // Note: no lock here, this is usually called by loop() and we may run into deadlock
  if (!newActivity) {
    LOG_ERR("ACT", "OOM: activity replacement allocation failed (free=%u maxAlloc=%u)", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    return;
  }
  if (currentActivity) {
    TouchRegistry::getInstance().clear();
    // Defer launch if we're currently in an activity, to avoid deleting the current activity
    // leading to the "delete this" problem
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    // No current activity, safe to launch immediately
    TouchRegistry::getInstance().clear();
    currentActivity = std::move(newActivity);
    currentActivity->onEnter();
  }
}

void ActivityManager::goToFileTransfer(std::string returnBookPath) {
  replaceActivity(makeUniqueNoThrow<CrossPointWebServerActivity>(renderer, mappedInput, std::move(returnBookPath)));
}

void ActivityManager::goToNearbyBookSend(std::string path, const bool returnToReader) {
  auto activity = makeUniqueNoThrow<NearbyBookTransferActivity>(
      renderer, mappedInput, NearbyBookTransferActivity::Mode::Send, std::move(path), returnToReader);
  if (!activity) {
    LOG_ERR("ACT", "OOM: nearby file sender");
    return;
  }
  replaceActivity(std::move(activity));
}

void ActivityManager::goToNearbyBookReceive() {
  auto activity =
      makeUniqueNoThrow<NearbyBookTransferActivity>(renderer, mappedInput, NearbyBookTransferActivity::Mode::Receive);
  if (!activity) {
    LOG_ERR("ACT", "OOM: nearby file receiver");
    return;
  }
  replaceActivity(std::move(activity));
}

void ActivityManager::goToCalibreWireless(const std::string& returnBookPath) {
  restartToFileTransfer(NetworkMode::CONNECT_CALIBRE, returnBookPath);
}

void ActivityManager::goToJoinNetworkFileTransfer(const std::string& returnBookPath) {
  restartToFileTransfer(NetworkMode::JOIN_NETWORK, returnBookPath);
}

void ActivityManager::goToHotspotFileTransfer(const std::string& returnBookPath) {
  restartToFileTransfer(NetworkMode::CREATE_HOTSPOT, returnBookPath);
}

bool ActivityManager::resumeFileTransferFromNetworkBoot(const uint32_t payload) {
  const uint32_t rawMode = payload & FILE_TRANSFER_MODE_MASK;
  if (rawMode > static_cast<uint32_t>(NetworkMode::CREATE_HOTSPOT)) {
    LOG_ERR("ACT", "Invalid file transfer network boot mode: %lu", static_cast<unsigned long>(rawMode));
    return false;
  }

  std::string returnBookPath;
  if ((payload & FILE_TRANSFER_RETURN_TO_READER) != 0) {
    if (!APP_STATE.openEpubPath.empty() && Storage.exists(APP_STATE.openEpubPath.c_str())) {
      returnBookPath = APP_STATE.openEpubPath;
    } else {
      LOG_ERR("ACT", "File transfer cannot return to missing reader path: %s", APP_STATE.openEpubPath.c_str());
    }
  }

  // The activity must outlive this boot function, so allocate its small control object on the heap; web buffers
  // remain owned and released by the activity lifecycle.
  auto activity = makeUniqueNoThrow<CrossPointWebServerActivity>(
      renderer, mappedInput, static_cast<NetworkMode>(rawMode), std::move(returnBookPath), true);
  if (!activity) {
    LOG_ERR("ACT", "OOM: file transfer after minimal boot (free=%u maxAlloc=%u)", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    return false;
  }

  replaceActivity(std::move(activity));
  return true;
}

void ActivityManager::goToNearbyStatsSync() {
  replaceActivity(makeUniqueNoThrow<NearbyStatsSyncActivity>(renderer, mappedInput));
}

void ActivityManager::goToSettings(const bool dismissOnUpSwipe) {
  replaceActivity(makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, dismissOnUpSwipe));
}

void ActivityManager::goToFileBrowser(std::string path) {
  replaceActivity(makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, std::move(path)));
}

void ActivityManager::goToRecentBooks() {
  if (SETTINGS.recentBooksView == CrossPointSettings::RECENT_BOOKS_GRID) {
    replaceActivity(makeUniqueNoThrow<RecentBooksGridActivity>(renderer, mappedInput));
  } else {
    replaceActivity(makeUniqueNoThrow<RecentBooksActivity>(renderer, mappedInput));
  }
}

void ActivityManager::goToBrowser() {
  const auto& servers = OPDS_STORE.getServers();
  // Skip the server picker when there's only one server configured
  if (servers.size() == 1) {
    goToOpdsServer(0);
  } else {
    replaceActivity(makeUniqueNoThrow<OpdsServerListActivity>(renderer, mappedInput, true));
  }
}

bool ActivityManager::goToOpdsServer(const uint32_t serverIndex, const bool networkBootReady) {
#ifndef SIMULATOR
  if (!networkBootReady) {
    silentRestartToNetwork(NetworkBootTarget::OPDS, serverIndex);
    return true;
  }
#else
  // The desktop build has no fragmented WiFi heap to clear; keep the preview in-process.
  (void)networkBootReady;
#endif

  OPDS_STORE.loadFromFile();
  const auto* storedServer = OPDS_STORE.getServer(serverIndex);
  if (!storedServer) {
    LOG_ERR("ACT", "OPDS network boot server index out of range: %lu", static_cast<unsigned long>(serverIndex));
    return false;
  }

  OpdsServer server = *storedServer;
  OPDS_STORE.release();
  auto browser = makeUniqueNoThrow<OpdsBookBrowserActivity>(renderer, mappedInput, std::move(server));
  if (!browser) {
    LOG_ERR("ACT", "OOM: OPDS browser after minimal boot (free=%u maxAlloc=%u)", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    return false;
  }
  replaceActivity(std::move(browser));
  return true;
}

void ActivityManager::goToReader(std::string path, const bool suppressBackRelease, const bool allowFastInitialRefresh,
                                 const bool cleanImageBaseOnEntry) {
  // OPDS credentials are unrelated to local reading and may contain several
  // heap-backed strings. Home reloads them lazily when it becomes active.
  OPDS_STORE.release();
  replaceActivity(makeUniqueNoThrow<ReaderActivity>(renderer, mappedInput, std::move(path), suppressBackRelease,
                                                    allowFastInitialRefresh, cleanImageBaseOnEntry));
}

void ActivityManager::goToSleep(bool fromTimeout) {
  const bool canSnapshotOverlay = currentActivity && currentActivity->canSnapshotForSleepOverlay();
  const GfxRenderer::Orientation sleepPopupOrientation = renderer.getOrientation();
  replaceActivity(makeUniqueNoThrow<SleepActivity>(renderer, mappedInput, canSnapshotOverlay, getCurrentBookPath(),
                                                   fromTimeout, sleepPopupOrientation));
  loop();  // Important: sleep screen must be rendered immediately, the caller will go to sleep right after this returns
}

void ActivityManager::goToBoot() { replaceActivity(makeUniqueNoThrow<BootActivity>(renderer, mappedInput)); }

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivity(makeUniqueNoThrow<FullScreenMessageActivity>(renderer, mappedInput, std::move(message), style));
}

void ActivityManager::goHome(HomeMenuItem initialMenuItem, const bool initialFullRefresh) {
  // Text antialiasing leaves intentional intermediate gray charge on the
  // panel. A differential FAST transition can carry that reader text into
  // Home even though controller RAM has the correct logical baseline. Request
  // one absolute HALF clean only for reader-to-Home transitions.
  const bool initialCleanRefresh = !initialFullRefresh && isReaderActivity();
  if (initialMenuItem == HomeMenuItem::NONE && currentActivity) {
    const auto& activityName = currentActivity->name;
    if (activityName == "FileBrowser") {
      initialMenuItem = HomeMenuItem::FILE_BROWSER;
    } else if (activityName == "RecentBooks") {
      initialMenuItem = HomeMenuItem::RECENTS;
    } else if (activityName == "OpdsBookBrowser") {
      initialMenuItem = HomeMenuItem::OPDS_BROWSER;
    } else if (activityName == "CrossPointWebServer") {
      initialMenuItem = HomeMenuItem::FILE_TRANSFER;
    } else if (activityName == "NearbyStatsSync") {
      initialMenuItem = HomeMenuItem::FILE_TRANSFER;
    } else if (activityName == "Settings") {
      initialMenuItem = HomeMenuItem::SETTINGS_MENU;
    }
  }
  replaceActivity(
      makeUniqueNoThrow<HomeActivity>(renderer, mappedInput, initialMenuItem, initialFullRefresh, initialCleanRefresh));
}
void ActivityManager::goToCrashReport() { replaceActivity(makeUniqueNoThrow<CrashActivity>(renderer, mappedInput)); }

bool ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  if (!activity) {
    LOG_ERR("ACT", "OOM: child activity allocation failed (free=%u maxAlloc=%u)", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    return false;
  }
  if (stackedActivityCount >= stackActivities.size()) {
    LOG_ERR("ACT", "Activity stack is full; unable to open %s", activity->name.c_str());
    return false;
  }
  if (pendingAction != PendingAction::None) {
    LOG_ERR("ACT", "Activity transition already pending; unable to open %s", activity->name.c_str());
    return false;
  }
  TouchRegistry::getInstance().clear();
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
  return true;
}

void ActivityManager::popActivity() {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while popActivity is not expected");
    pendingActivity.reset();
  }
  TouchRegistry::getInstance().clear();
  pendingAction = PendingAction::Pop;
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

bool ActivityManager::isHomeActivity() const { return currentActivity && currentActivity->name == "Home"; }

bool ActivityManager::isReaderActivity() const {
  if (currentActivity && currentActivity->isReaderActivity()) {
    return true;
  }

  return std::any_of(stackActivities.begin(), stackActivities.begin() + stackedActivityCount,
                     [](const auto& activity) { return activity && activity->isReaderActivity(); });
}

bool ActivityManager::readerPowerButtonOpensSettings() const {
  return mappedInput.hasTouchHardware() && SETTINGS.disableReaderTouchscreen && currentActivity &&
         currentActivity->handlesReaderPowerSettingsOverride();
}

bool ActivityManager::hasActivityNamed(const char* activityName) const {
  const auto matches = [activityName](const auto& activity) { return activity && activity->name == activityName; };
  if (matches(currentActivity) || matches(pendingActivity)) {
    return true;
  }

  return std::any_of(stackActivities.begin(), stackActivities.begin() + stackedActivityCount, matches);
}

#ifdef SIMULATOR
bool ActivityManager::isCurrentActivityNamed(const char* activityName) const {
  return currentActivity && currentActivity->name == activityName;
}
#endif

bool ActivityManager::canSnapshotForSleepOverlay() const {
  return currentActivity && currentActivity->canSnapshotForSleepOverlay();
}

bool ActivityManager::requestManualReaderRefresh() {
  RenderLock lock;
  if (!currentActivity || !currentActivity->isReaderActivity() || !currentActivity->prepareManualRefresh()) {
    return false;
  }

  lock.unlock();
  requestUpdate(true);
  return true;
}

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

unsigned long ActivityManager::nextLoopWakeDelayMs() const {
  return currentActivity ? currentActivity->nextLoopWakeDelayMs() : 250UL;
}

std::string ActivityManager::getCurrentBookPath() const {
  if (currentActivity) {
    const std::string path = currentActivity->getCurrentBookPath();
    if (!path.empty()) {
      return path;
    }
  }

  for (size_t i = stackedActivityCount; i > 0; --i) {
    if (stackActivities[i - 1]) {
      const std::string path = stackActivities[i - 1]->getCurrentBookPath();
      if (!path.empty()) {
        return path;
      }
    }
  }

  return {};
}

ScreenshotInfo ActivityManager::getScreenshotInfo() const {
  if (currentActivity) {
    const ScreenshotInfo info = currentActivity->getScreenshotInfo();
    if (info.readerType != ScreenshotInfo::ReaderType::None) {
      return info;
    }
  }

  // Reader overlays such as the dictionary are pushed above the book activity.
  // Keep their visible framebuffer, but inherit the book's screenshot filename.
  for (size_t i = stackedActivityCount; i > 0; --i) {
    if (stackActivities[i - 1]) {
      const ScreenshotInfo info = stackActivities[i - 1]->getScreenshotInfo();
      if (info.readerType != ScreenshotInfo::ReaderType::None) {
        return info;
      }
    }
  }

  return {};
}

void ActivityManager::requestUpdate(bool immediate) {
  if (immediate) {
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  } else {
    // Deferring the update until current loop is finished
    // This is to avoid multiple updates being requested in the same loop
    requestedUpdate = true;
  }
}
RequestUpdateResult ActivityManager::requestUpdateAndWait() {
  if (!renderTaskHandle) {
    return RequestUpdateResult::Rejected;
  }

  // Atomic section to perform checks
  taskENTER_CRITICAL(&renderStateMux);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  auto mutexHolder = xSemaphoreGetMutexHolder(renderingMutex);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
  bool holdingRenderLock = (mutexHolder == currTaskHandler);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&renderStateMux);

  if (isRenderTask) {
    LOG_ERR("ACT", "requestUpdateAndWait() called from render task; rejecting sync update");
    return RequestUpdateResult::Rejected;
  }

  if (alreadyWaiting) {
    LOG_ERR("ACT", "requestUpdateAndWait() called while another task is waiting; rejecting sync update");
    return RequestUpdateResult::Rejected;
  }

  // Cannot call while holding RenderLock or it will cause a deadlock
  if (holdingRenderLock) {
    LOG_ERR("ACT", "requestUpdateAndWait() called while holding RenderLock; rejecting sync update");
    return RequestUpdateResult::Rejected;
  }

  // This synchronous paint includes every state change that led to an older
  // deferred request. Consume that request before notifying the render task;
  // a genuinely new request made while we wait remains set for loop() to send.
  requestedUpdate.store(false, std::memory_order_release);
  xTaskNotify(renderTaskHandle, 1, eIncrement);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  return RequestUpdateResult::Rendered;
}

// RenderLock

RenderLock::RenderLock() {
  if (!activityManager.renderingMutex) {
    LOG_ERR("ACT", "Rendering mutex is unavailable");
    return;
  }
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::RenderLock([[maybe_unused]] Activity&) {
  if (!activityManager.renderingMutex) {
    LOG_ERR("ACT", "Rendering mutex is unavailable");
    return;
  }
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::~RenderLock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

void RenderLock::unlock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

/**
 * Checks if renderingMutex is held by any task, including the calling task.
 *
 * @return true if renderingMutex has an owner (any task), false otherwise.
 *
 * @note Must not be called from ISR context — xSemaphoreGetMutexHolder is not ISR-safe.
 */
bool RenderLock::peek() {
  return activityManager.renderingMutex && xSemaphoreGetMutexHolder(activityManager.renderingMutex) != nullptr;
}
