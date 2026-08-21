#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <Logging.h>

#include "HalSpiBus.h"

// Global HalDisplay instance
HalDisplay display;

#define SD_SPI_MISO 7

namespace {
void beginDisplayBusyWait() {
  // Change the clock while the display still owns shared SPI. A waiting SD task
  // can only start after the new frequency is stable, avoiding a clock change
  // in the middle of an SPI transaction on C3/Sticky boards.
  powerManager.beginDisplayBusyWait();
  HalSpiBus::beginDisplayBusyWait();
}

void endDisplayBusyWait() {
  // Finish any SD transaction that borrowed shared SPI before changing the
  // clock again. Full speed is restored before the panel driver resumes work.
  HalSpiBus::endDisplayBusyWait();
  powerManager.endDisplayBusyWait();
}
}  // namespace

HalDisplay::HalDisplay() : einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY) {}

HalDisplay::~HalDisplay() {}

bool HalDisplay::begin(bool seamless) {
  HalSpiBus::Lock spiLock;
  if (!spiLock) {
    LOG_ERR("EPD", "SPI bus lock is unavailable");
    return false;
  }

  // Set X3-specific panel mode before initializing.
  if (gpio.deviceIsX3()) {
    einkDisplay.setDisplayX3();
  }

  einkDisplay.setBusyWaitHooks(beginDisplayBusyWait, endDisplayBusyWait);
  einkDisplay.begin();

  if (!einkDisplay.getFrameBuffer()) {
    LOG_ERR("EPD", "Framebuffer allocation failed (free=%u maxAlloc=%u)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#if defined(BOARD_HAS_PSRAM)
    LOG_ERR("EPD", "PSRAM state: free=%u maxAlloc=%u", ESP.getFreePsram(), ESP.getMaxAllocPsram());
#endif
    return false;
  }

  if (seamless) {
    // Defuse the SDK's X3 _x3InitialFullSyncsRemaining counter (no-op on X4)
    // so the first paint isn't promoted to FULL (~770ms). Skips the wakeup-
    // gated requestResync() below for the same reason.
    einkDisplay.skipInitialResync();
    return true;
  }
  // Request resync after specific wakeup events to ensure clean display state.
  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton || wakeupReason == HalGPIO::WakeupReason::AfterFlash ||
      wakeupReason == HalGPIO::WakeupReason::Other) {
    einkDisplay.requestResync();
  }
  return true;
}

void HalDisplay::clearScreen(uint8_t color) const { einkDisplay.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  einkDisplay.drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  einkDisplay.drawImageTransparent(imageData, x, y, w, h, fromProgmem);
}

EInkDisplay::RefreshMode convertRefreshMode(HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return EInkDisplay::FULL_REFRESH;
    case HalDisplay::HALF_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  HalSpiBus::Lock spiLock;

  if (gpio.deviceIsX3() && mode == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.displayBuffer(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::displayBufferAsync(HalDisplay::RefreshMode mode) {
  HalSpiBus::Lock spiLock;

  if (gpio.deviceIsX3() && mode == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.displayBufferAsyncNoShadow(convertRefreshMode(mode));
}

void HalDisplay::waitRefreshComplete() {
  HalSpiBus::Lock spiLock;
  einkDisplay.waitRefreshComplete();
}

bool HalDisplay::supportsAsyncRefresh() const { return einkDisplay.supportsAsyncRefresh(); }

bool HalDisplay::supportsAsyncGrayscaleBase() const { return !gpio.deviceIsX3() && einkDisplay.supportsAsyncRefresh(); }

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  HalSpiBus::Lock spiLock;

  if (gpio.deviceIsX3() && mode == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::deepSleep() {
  HalSpiBus::Lock spiLock;
  einkDisplay.deepSleep();
}

uint8_t* HalDisplay::getFrameBuffer() const { return einkDisplay.getFrameBuffer(); }

uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) {
  HalSpiBus::Lock spiLock;
  return einkDisplay.lendBuildStorage(sizeOut);
}

void HalDisplay::returnFrameBufferStorage() {
  HalSpiBus::Lock spiLock;
  einkDisplay.returnBuildStorage();
}

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  HalSpiBus::Lock spiLock;
  einkDisplay.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void HalDisplay::displayGrayscaleBase(RefreshMode fallback, bool turnOffScreen) {
  HalSpiBus::Lock spiLock;

  // X3: a HALF or FULL fallback means the caller wants a clean base (e.g. the
  // sleep cover, a full-screen swap from arbitrary prior content). Without
  // this, the X3 grayscale base takes its gentle differential happy path and
  // the prior home/reader frame ghosts through the soft aa_pre_bw_mid
  // waveform. Forcing a resync makes displayGrayscaleBase clear first,
  // matching displayBuffer(HALF)/displayBuffer(FULL).
  if (gpio.deviceIsX3() && fallback != RefreshMode::FAST_REFRESH) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.displayGrayscaleBase(convertRefreshMode(fallback), turnOffScreen);
}

void HalDisplay::preconditionGrayscale() {
  HalSpiBus::Lock spiLock;
  einkDisplay.preconditionGrayscale();
}

void HalDisplay::preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  HalSpiBus::Lock spiLock;
  einkDisplay.preconditionGrayscale(x, y, w, h);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  HalSpiBus::Lock spiLock;
  einkDisplay.copyGrayscaleLsbBuffers(lsbBuffer);
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  HalSpiBus::Lock spiLock;
  einkDisplay.copyGrayscaleMsbBuffers(msbBuffer);
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  HalSpiBus::Lock spiLock;
  einkDisplay.cleanupGrayscaleBuffers(bwBuffer);
}

void HalDisplay::displayGrayBuffer(bool turnOffScreen) {
  HalSpiBus::Lock spiLock;
  einkDisplay.displayGrayBuffer(turnOffScreen);
}

void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows) {
  HalSpiBus::Lock spiLock;
  einkDisplay.writeGrayscalePlaneStrip(lsbPlane ? EInkDisplay::GRAY_PLANE_LSB : EInkDisplay::GRAY_PLANE_MSB, rows,
                                       yStart, numRows);
}

bool HalDisplay::supportsStripGrayscale() const { return einkDisplay.supportsStripGrayscale(); }

uint16_t HalDisplay::getDisplayWidth() const { return einkDisplay.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return einkDisplay.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return einkDisplay.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return einkDisplay.getBufferSize(); }
