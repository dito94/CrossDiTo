#ifdef SIMULATOR

#include "SimulatorSmokeTest.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <vector>

#include "CrossInkHalFrontlight.h"
#include "CrossPointSettings.h"
#include "DeviceCapabilities.h"
#include "MappedInputManager.h"
#include "SettingsList.h"
#include "activities/ActivityManager.h"
#include "activities/reader/EpubReaderMenuActivity.h"
#include "activities/reader/ReaderOptionsActivity.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "simulator/SimulatorHomeKeyInput.h"
#include "util/FrontlightSchedule.h"

extern ActivityManager activityManager;
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace {

enum class SmokeStep : uint8_t {
  Start,
  Home,
  FileBrowser,
  RecentBooks,
  Settings,
  ReaderOptions,
  ReaderMenu,
  Sleep,
  Reader,
  ReaderInput,
  Done,
};

class SimulatorSmokeTest {
 public:
  void tick() {
    if (!enabled()) return;

    try {
      tickImpl();
    } catch (const std::exception& e) {
      fail("Unhandled exception: %s", e.what());
    } catch (...) {
      fail("Unhandled non-standard exception");
    }
  }

 private:
  enum class ScriptActionType : uint8_t {
    Press,
    Release,
    HomeTap,
    HomeLongPress,
    OpenSmokeBook,
    DisableReaderTouch,
    EnableReaderTouch,
    TouchDown,
    TouchMove,
    TouchRelease,
    AssertActivity,
    Render
  };

  struct ScriptAction {
    ScriptActionType type;
    MappedInputManager::Button button;
    const char* label;
    int settleFrames;
    int x;
    int y;
  };

  SmokeStep step = SmokeStep::Start;
  int settleFrames = 0;
  const char* activeStepName = nullptr;
  std::vector<ScriptAction> inputScript;
  size_t scriptIndex = 0;

  static bool enabled() { return std::getenv("CROSSINK_SIMULATOR_SMOKE_TEST") != nullptr; }

  static int pageTurnCount() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_PAGE_TURNS");
    if (raw == nullptr || raw[0] == '\0') {
      return 2;
    }
    return std::max(0, std::atoi(raw));
  }

  static void applyRequestedTheme() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_THEME");
    if (raw == nullptr || raw[0] == '\0') {
      return;
    }

    const int theme = std::atoi(raw);
    if (theme < 0 || theme >= CrossPointSettings::UI_THEME_COUNT) {
      fail("Invalid smoke test theme index: %d", theme);
    }

    SETTINGS.uiTheme = static_cast<uint8_t>(theme);
    UITheme::getInstance().reload();
    LOG_INF("SMOKE", "Using theme index %d", theme);
  }

  static void verifyGlyphBitmapFastPath() {
    RenderLock renderLock;
    uint8_t* const frameBuffer = renderer.getFrameBuffer();
    const size_t bufferSize = renderer.getBufferSize();
    if (!frameBuffer || bufferSize == 0) {
      fail("Glyph fast-path test has no framebuffer");
    }

    const std::vector<uint8_t> original(frameBuffer, frameBuffer + bufferSize);
    std::vector<uint8_t> fastResult(bufferSize);
    const auto originalOrientation = renderer.getOrientation();
    constexpr int glyphWidth = 11;
    constexpr int glyphHeight = 5;
    constexpr int originX = 13;
    constexpr int originY = 17;
    constexpr uint8_t bitmap[] = {0xB5, 0x2E, 0xD1, 0x79, 0x46, 0xAB, 0xE0};
    constexpr GfxRenderer::Orientation orientations[] = {
        GfxRenderer::LandscapeCounterClockwise,
        GfxRenderer::LandscapeClockwise,
        GfxRenderer::Portrait,
        GfxRenderer::PortraitInverted,
    };

    for (const auto orientation : orientations) {
      renderer.setOrientation(orientation);
      for (const bool black : {false, true}) {
        // Start from the opposite color so every glyph bit must modify the
        // framebuffer; untouched background bits must remain unchanged.
        std::fill_n(frameBuffer, bufferSize, black ? 0xFF : 0x00);
        if (!renderer.drawGlyphBitmap1BitFast(bitmap, glyphWidth, glyphHeight, originX, originY, black)) {
          fail("Glyph fast path rejected an in-bounds glyph (orientation=%u)", static_cast<unsigned>(orientation));
        }
        std::copy_n(frameBuffer, bufferSize, fastResult.begin());

        std::fill_n(frameBuffer, bufferSize, black ? 0xFF : 0x00);
        for (int glyphY = 0; glyphY < glyphHeight; ++glyphY) {
          for (int glyphX = 0; glyphX < glyphWidth; ++glyphX) {
            const int sourcePosition = glyphY * glyphWidth + glyphX;
            if ((bitmap[sourcePosition >> 3] & (0x80U >> (sourcePosition & 7))) != 0) {
              renderer.drawPixel(originX + glyphX, originY + glyphY, black);
            }
          }
        }
        const auto mismatch = std::mismatch(fastResult.begin(), fastResult.end(), frameBuffer);
        if (mismatch.first != fastResult.end()) {
          const size_t byteIndex = static_cast<size_t>(mismatch.first - fastResult.begin());
          fail(
              "Glyph fast path differs from reference pixels (orientation=%u, black=%d, byte=%zu, fast=%u, "
              "reference=%u)",
              static_cast<unsigned>(orientation), black ? 1 : 0, byteIndex, static_cast<unsigned>(*mismatch.first),
              static_cast<unsigned>(*mismatch.second));
        }
      }
    }

    renderer.setOrientation(originalOrientation);
    std::copy(original.begin(), original.end(), frameBuffer);
    if (renderer.drawGlyphBitmap1BitFast(bitmap, glyphWidth, glyphHeight, -1, originY, true)) {
      fail("Glyph fast path accepted a clipped glyph");
    }
    LOG_INF("SMOKE", "Glyph framebuffer fast path matches reference rendering");
  }

  [[noreturn]] static void fail(const char* message) {
    LOG_ERR("SMOKE", "%s", message);
    std::_Exit(2);
  }

  template <typename... Args>
  [[noreturn]] static void fail(const char* format, Args... args) {
    logPrintf("ERR", "SMOKE", format, args...);
    logPrintf("ERR", "SMOKE", "\n");
    std::_Exit(2);
  }

  static void renderCurrentStep(const char* name) {
    LOG_INF("SMOKE", "Rendering %s", name);
    if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) {
      fail("Render was rejected for %s", name);
    }
  }

  void queueStep(const char* name, SmokeStep nextStep, int framesToSettle = 3) {
    activeStepName = name;
    settleFrames = framesToSettle;
    step = nextStep;
  }

  void tickImpl() {
    mappedInputManager.simulatorClearInputFrame();

    if (settleFrames > 0) {
      --settleFrames;
      if (settleFrames == 0 && activeStepName != nullptr) {
        renderCurrentStep(activeStepName);
        activeStepName = nullptr;
      }
      return;
    }

    switch (step) {
      case SmokeStep::Start: {
        LOG_INF("SMOKE", "Starting simulator smoke test");
        if (!CrossPointSettings::verifySleepTimeoutMigrationContract()) {
          fail("Sleep timeout migration contract failed");
        }
        if (!CrossPointSettings::verifySleepScreenMigrationContract()) {
          fail("Sleep screen migration contract failed");
        }
        if (!SimulatorHomeKeyInput::verifyTimingContract()) {
          fail("Simulator Home key timing contract failed");
        }
        verifyGlyphBitmapFastPath();
#ifdef SIMULATOR_DEVICE_X4_PRO
        if (!mappedInputManager.hasTouchHardware()) {
          fail("X4 Pro simulator profile did not expose touch hardware");
        }
        if (!mappedInputManager.hasHomeKey()) {
          fail("X4 Pro simulator profile did not expose its Home key");
        }
        if (!Frontlight.present() || !Frontlight.hasColorTemperature()) {
          fail("X4 Pro simulator profile did not expose its frontlight capabilities");
        }
        if (!FrontlightSchedule::verifyContract()) {
          fail("Frontlight schedule timing contract failed");
        }
        const auto displaySettings = buildGroupedDisplaySettingsList(getSettingsList());
        const auto scheduleSetting =
            std::find_if(displaySettings.begin(), displaySettings.end(),
                         [](const SettingInfo& setting) { return setting.nameId == StrId::STR_FRONTLIGHT_SCHEDULE; });
        if (scheduleSetting == displaySettings.end() ||
            !hasSettingByName(displaySettings, StrId::STR_FRONTLIGHT_SCHEDULE_START) ||
            !hasSettingByName(displaySettings, StrId::STR_FRONTLIGHT_SCHEDULE_END)) {
          fail("Frontlight schedule settings are missing from the Display screen");
        }
        if (std::distance(displaySettings.begin(), scheduleSetting) > 3) {
          fail("Frontlight schedule is below the first Display-settings page");
        }
        if (!deviceHasEdgeSideButtons(gpio)) {
          fail("X4 Pro simulator profile did not expose its edge buttons");
        }
#endif
        applyRequestedTheme();
        activityManager.goHome();
        queueStep("Home", SmokeStep::Home);
        break;
      }

      case SmokeStep::Home:
        activityManager.goToFileBrowser("/books");
        queueStep("File Browser", SmokeStep::FileBrowser);
        break;

      case SmokeStep::FileBrowser:
        activityManager.goToRecentBooks();
        queueStep("Recent Books", SmokeStep::RecentBooks);
        break;

      case SmokeStep::RecentBooks:
        activityManager.goToSettings();
        queueStep("Settings", SmokeStep::Settings);
        break;

      case SmokeStep::Settings:
        activityManager.replaceActivity(makeUniqueNoThrow<ReaderOptionsActivity>(renderer, mappedInputManager));
        queueStep("Reader Options", SmokeStep::ReaderOptions);
        break;

      case SmokeStep::ReaderOptions:
        activityManager.replaceActivity(
            makeUniqueNoThrow<EpubReaderMenuActivity>(renderer, mappedInputManager, "Smoke Test", 1, 1, 0,
                                                      SETTINGS.orientation, false, false, false, false, false, false));
        queueStep("Reader Menu", SmokeStep::ReaderMenu);
        break;

      case SmokeStep::ReaderMenu:
        activityManager.goToSleep();
        queueStep("Sleep", SmokeStep::Sleep);
        break;

      case SmokeStep::Sleep: {
        const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
        if (bookPath == nullptr || bookPath[0] == '\0') {
          LOG_INF("SMOKE", "Skipping Reader step; CROSSINK_SIMULATOR_SMOKE_BOOK is not set");
          step = SmokeStep::Reader;
          break;
        }
        if (!Storage.exists(bookPath)) {
          fail("Smoke test book is missing: %s", bookPath);
        }
        activityManager.goToReader(bookPath, true);
        queueStep("Reader", SmokeStep::Reader, 8);
        break;
      }

      case SmokeStep::Reader:
        buildReaderInputScript();
        step = SmokeStep::ReaderInput;
        break;

      case SmokeStep::ReaderInput:
        runReaderInputScript();
        break;

      case SmokeStep::Done:
        LOG_INF("SMOKE", "Simulator smoke test passed");
        std::_Exit(0);
    }
  }

  static ScriptAction press(MappedInputManager::Button button) {
    return {ScriptActionType::Press, button, nullptr, 0, 0, 0};
  }

  static ScriptAction release(MappedInputManager::Button button) {
    return {ScriptActionType::Release, button, nullptr, 0, 0, 0};
  }

  static ScriptAction homeTap() {
    return {ScriptActionType::HomeTap, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction homeLongPress() {
    return {ScriptActionType::HomeLongPress, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction openSmokeBook() {
    return {ScriptActionType::OpenSmokeBook, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction disableReaderTouch() {
    return {ScriptActionType::DisableReaderTouch, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction enableReaderTouch() {
    return {ScriptActionType::EnableReaderTouch, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction render(const char* label, int framesToSettle = 3) {
    return {ScriptActionType::Render, MappedInputManager::Button::Back, label, framesToSettle, 0, 0};
  }

#if CROSSINK_APP_CAP_TOUCH
  static ScriptAction touchDown(const int x, const int y) {
    return {ScriptActionType::TouchDown, MappedInputManager::Button::Back, nullptr, 0, x, y};
  }
  static ScriptAction touchMove(const int x, const int y) {
    return {ScriptActionType::TouchMove, MappedInputManager::Button::Back, nullptr, 0, x, y};
  }
  static ScriptAction touchRelease(const int x, const int y) {
    return {ScriptActionType::TouchRelease, MappedInputManager::Button::Back, nullptr, 0, x, y};
  }
  static ScriptAction assertActivity(const char* name) {
    return {ScriptActionType::AssertActivity, MappedInputManager::Button::Back, name, 0, 0, 0};
  }
#endif

  void addTap(MappedInputManager::Button button) {
    inputScript.push_back(press(button));
    inputScript.push_back(release(button));
  }

  void buildReaderInputScript() {
    inputScript.clear();
    scriptIndex = 0;

    const int turns = pageTurnCount();
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInputManager.hasTouch()) {
      const int width = renderer.getScreenWidth();
      const int height = renderer.getScreenHeight();
      if (width <= 0 || height <= 0) fail("Touch smoke test has invalid screen dimensions");
      LOG_INF("SMOKE", "Running touch reader input script with %d page turn(s)", turns);
      for (int i = 0; i < turns; ++i) {
        inputScript.push_back(touchDown(width * 5 / 6, height / 2));
        inputScript.push_back(touchRelease(width * 5 / 6, height / 2));
        inputScript.push_back(render("Reader after touch page forward", 4));
      }
      if (mappedInputManager.hasHomeKey()) {
        // X4 Pro reserves the top-edge swipe for its frontlight overlay and
        // moves the reader menu to the bottom edge.
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Frontlight Panel opened from touch gesture", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        inputScript.push_back(touchDown(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
        inputScript.push_back(render("Reader restored after dismissing Frontlight Panel", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(homeLongPress());
        inputScript.push_back(render("Reader Menu opened from simulated Home key hold", 4));
        inputScript.push_back(assertActivity("EpubReaderMenu"));
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Reader restored after top-edge swipe dismisses Reader Menu", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(disableReaderTouch());
        inputScript.push_back(homeLongPress());
        inputScript.push_back(render("Reader Menu opened from Home key hold with touch disabled", 4));
        inputScript.push_back(assertActivity("EpubReaderMenu"));
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Reader restored after Home key menu with touch disabled", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(homeTap());
        inputScript.push_back(render("Home opened from simulated Home key tap", 4));
        inputScript.push_back(assertActivity("Home"));
        inputScript.push_back(enableReaderTouch());
        inputScript.push_back(openSmokeBook());
        inputScript.push_back(render("Reader reopened after simulated Home key tap", 8));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(touchDown(width / 2, height - 8));
        inputScript.push_back(touchMove(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
      } else {
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
      }
      inputScript.push_back(render("Reader Menu opened from touch gesture", 4));
      inputScript.push_back(assertActivity("EpubReaderMenu"));

      // The menu itself registers its tab and list hit areas through the active
      // theme. Exercise both before activating Reader Options from list row 1.
      const auto& metrics = UITheme::getInstance().getMetrics();
      const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, !mappedInputManager.hasTouch(), false);
      const int tabHeight = metrics.tabBarHeight * 2;
      const bool tabsAtBottom = mappedInputManager.hasHomeKey();
      const int tabTop = tabsAtBottom ? safe.y + safe.height - tabHeight
                                      : safe.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight;
      const int listTop = safe.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                          (tabsAtBottom ? 0 : tabHeight) + metrics.verticalSpacing;
      const int rowHeight = uiThemeTokens(makeUiTarget(renderer)).rowHeight;
      if (rowHeight <= 0) fail("Touch smoke test has invalid list row height");

      inputScript.push_back(touchDown(safe.x + safe.width / 2, tabTop + tabHeight / 2));
      inputScript.push_back(touchRelease(safe.x + safe.width / 2, tabTop + tabHeight / 2));
      inputScript.push_back(render("Reader Menu tab touch navigation", 3));
      inputScript.push_back(assertActivity("EpubReaderMenu"));
      inputScript.push_back(touchDown(safe.x + safe.width / 6, tabTop + tabHeight / 2));
      inputScript.push_back(touchRelease(safe.x + safe.width / 6, tabTop + tabHeight / 2));
      inputScript.push_back(render("Reader Menu main tab restored", 3));
      inputScript.push_back(assertActivity("EpubReaderMenu"));

      // Jump to another chapter, then exercise the one-shot return row that
      // appears only after a successful navigation.
      const int chapterRowY = listTop + rowHeight / 2;
      inputScript.push_back(touchDown(safe.x + safe.width / 2, chapterRowY));
      inputScript.push_back(touchRelease(safe.x + safe.width / 2, chapterRowY));
      inputScript.push_back(render("Chapter Selection opened for return-position test", 4));
      inputScript.push_back(assertActivity("EpubReaderChapterSelection"));
      addTap(MappedInputManager::Button::Down);
      inputScript.push_back(render("Chapter Selection advanced", 3));
      addTap(MappedInputManager::Button::Confirm);
      inputScript.push_back(render("Reader after chapter jump", 8));
      inputScript.push_back(assertActivity("EpubReader"));

      inputScript.push_back(touchDown(width / 2, height - 8));
      inputScript.push_back(touchMove(width / 2, height * 3 / 4));
      inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
      inputScript.push_back(render("Reader Menu with previous-position action", 4));
      inputScript.push_back(assertActivity("EpubReaderMenu"));
      const int returnRowY = listTop + rowHeight / 2;
      inputScript.push_back(touchDown(safe.x + safe.width / 2, returnRowY));
      inputScript.push_back(touchRelease(safe.x + safe.width / 2, returnRowY));
      inputScript.push_back(render("Reader after returning to previous position", 8));
      inputScript.push_back(assertActivity("EpubReader"));

      inputScript.push_back(touchDown(width / 2, height - 8));
      inputScript.push_back(touchMove(width / 2, height * 3 / 4));
      inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
      inputScript.push_back(render("Reader Menu after one-shot return consumed", 4));
      inputScript.push_back(assertActivity("EpubReaderMenu"));

      const int readerOptionsY = listTop + rowHeight + rowHeight / 2;
      inputScript.push_back(touchDown(safe.x + safe.width / 2, readerOptionsY));
      inputScript.push_back(touchRelease(safe.x + safe.width / 2, readerOptionsY));
      inputScript.push_back(render("Reader Options opened by touch list activation", 4));
      inputScript.push_back(assertActivity("ReaderOptions"));

      const int optionsListTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
      const int optionsStartY = optionsListTop + rowHeight * 4 + rowHeight / 2;
      inputScript.push_back(touchDown(width / 2, optionsStartY));
      inputScript.push_back(touchMove(width / 2, optionsListTop + rowHeight / 2));
      inputScript.push_back(touchRelease(width / 2, optionsListTop + rowHeight / 2));
      inputScript.push_back(render("Reader Options touch swipe navigation", 3));
      inputScript.push_back(assertActivity("ReaderOptions"));
      return;
    }
#endif
    for (int i = 0; i < turns; i++) {
      addTap(MappedInputManager::Button::PageForward);
      inputScript.push_back(render("Reader after page forward", 4));
    }

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Reader Menu opened from EPUB", 4));

    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("Reader Menu Reader Options selection", 3));

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Reader Options opened from Reader Menu", 4));

    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("Reader Options after navigation", 3));

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Reader Options after toggle", 3));

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader Menu after closing Reader Options", 4));

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader after closing Reader Menu", 4));

    LOG_INF("SMOKE", "Running reader input script with %d page turn(s)", turns);
  }

  void runReaderInputScript() {
    if (scriptIndex >= inputScript.size()) {
      step = SmokeStep::Done;
      return;
    }

    const auto& action = inputScript[scriptIndex++];
    switch (action.type) {
      case ScriptActionType::Press:
        mappedInputManager.simulatorInjectPress(action.button);
        break;
      case ScriptActionType::Release:
        mappedInputManager.simulatorInjectRelease(action.button);
        break;
      case ScriptActionType::HomeTap:
        simulatorHomeKeyInput.injectTap();
        break;
      case ScriptActionType::HomeLongPress:
        simulatorHomeKeyInput.injectLongPress();
        break;
      case ScriptActionType::OpenSmokeBook: {
        const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
        if (bookPath == nullptr || bookPath[0] == '\0') fail("Smoke test book path is missing");
        activityManager.goToReader(bookPath, true);
        break;
      }
      case ScriptActionType::DisableReaderTouch:
        SETTINGS.disableReaderTouchscreen = true;
        break;
      case ScriptActionType::EnableReaderTouch:
        SETTINGS.disableReaderTouchscreen = false;
        break;
      case ScriptActionType::TouchDown:
#if CROSSINK_APP_CAP_TOUCH
        mappedInputManager.simulatorInjectTouchDown(action.x, action.y);
#endif
        break;
      case ScriptActionType::TouchMove:
#if CROSSINK_APP_CAP_TOUCH
        mappedInputManager.simulatorInjectTouchMove(action.x, action.y);
#endif
        break;
      case ScriptActionType::TouchRelease:
#if CROSSINK_APP_CAP_TOUCH
        mappedInputManager.simulatorInjectTouchRelease(action.x, action.y);
#endif
        break;
      case ScriptActionType::AssertActivity:
        if (!activityManager.isCurrentActivityNamed(action.label)) fail("Expected current activity: %s", action.label);
        break;
      case ScriptActionType::Render:
        queueStep(action.label, SmokeStep::ReaderInput, action.settleFrames);
        break;
    }
  }
};

SimulatorSmokeTest smokeTest;

}  // namespace

void runSimulatorSmokeTestTick() { smokeTest.tick(); }

#endif
