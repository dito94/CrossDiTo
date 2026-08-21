#include "HalSystem.h"

#include <string>

#include "AppVersion.h"
#include "Arduino.h"
#include "HalStorage.h"
#include "Logging.h"
#include "esp32-hal.h"

namespace {

constexpr size_t MAX_PANIC_BACKTRACE_DEPTH = 32;
constexpr uint32_t PANIC_CAPTURE_MAGIC = 0x43525054;  // "CRPT"
constexpr uint8_t PANIC_FLAG_BACKTRACE_CORRUPT = 1U << 0;
constexpr uint8_t PANIC_FLAG_BACKTRACE_CONTINUES = 1U << 1;

struct PanicCapture {
  uint32_t magic;
  uint32_t pc;
  uint32_t backtrace[MAX_PANIC_BACKTRACE_DEPTH];
  uint8_t backtraceLength;
  int8_t core;
  uint8_t flags;
};

RTC_NOINIT_ATTR char panicMessage[256];
RTC_NOINIT_ATTR PanicCapture panicCapture;

static DRAM_ATTR const char PANIC_REASON_UNKNOWN[] = "(unknown panic reason)";

void IRAM_ATTR clearPanicTrace() {
  panicCapture.pc = 0;
  panicCapture.backtraceLength = 0;
  panicCapture.core = -1;
  panicCapture.flags = 0;
  for (size_t i = 0; i < MAX_PANIC_BACKTRACE_DEPTH; ++i) {
    panicCapture.backtrace[i] = 0;
  }
}

void IRAM_ATTR copyPanicReason(const char* message) {
  if (!message) message = PANIC_REASON_UNKNOWN;
  // IRAM-safe bounded copy (strncpy is not IRAM-safe in panic context).
  size_t i = 0;
  for (; i < sizeof(panicMessage) - 1 && message[i]; ++i) {
    panicMessage[i] = message[i];
  }
  panicMessage[i] = '\0';
}

void IRAM_ATTR panicMemoryBarrier() {
#if defined(__XTENSA__)
  __asm__ __volatile__("memw" ::: "memory");
#else
  __asm__ __volatile__("" ::: "memory");
#endif
}

void IRAM_ATTR commitPanicCapture() {
  // Keep the validity marker strictly after the RTC payload, including across
  // the Xtensa write buffer, so a reboot never accepts a partial backtrace.
  panicMemoryBarrier();
  panicCapture.magic = PANIC_CAPTURE_MAGIC;
  panicMemoryBarrier();
}

void IRAM_ATTR captureArduinoPanic(arduino_panic_info_t* info, void*) {
  // Invalidate first so a reset during capture cannot expose partial RTC data.
  panicCapture.magic = 0;
  panicMemoryBarrier();
  clearPanicTrace();

  if (!info) {
    copyPanicReason(nullptr);
    commitPanicCapture();
    return;
  }

  copyPanicReason(info->reason);
  panicCapture.pc = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info->pc));
  panicCapture.core = static_cast<int8_t>(info->core);
  if (info->backtrace_corrupt) panicCapture.flags |= PANIC_FLAG_BACKTRACE_CORRUPT;

  const size_t sourceLength = info->backtrace_len;
  const size_t copyLength = sourceLength < MAX_PANIC_BACKTRACE_DEPTH ? sourceLength : MAX_PANIC_BACKTRACE_DEPTH;
  for (size_t i = 0; i < copyLength; ++i) {
    panicCapture.backtrace[i] = info->backtrace[i];
  }
  panicCapture.backtraceLength = static_cast<uint8_t>(copyLength);
  if (info->backtrace_continues || sourceLength > copyLength) {
    panicCapture.flags |= PANIC_FLAG_BACKTRACE_CONTINUES;
  }

  commitPanicCapture();
}

}  // namespace

extern "C" {

void __real_panic_abort(const char* message);

void IRAM_ATTR __wrap_panic_abort(const char* message) {
  // panic_abort() can run before HalSystem::begin() installs the Arduino panic
  // callback. Preserve an existing detailed trace, otherwise initialize a
  // reason-only capture.
  if (panicCapture.magic != PANIC_CAPTURE_MAGIC) {
    clearPanicTrace();
  }
  copyPanicReason(message);
  commitPanicCapture();

  __real_panic_abort(message);
}
}

namespace HalSystem {

void begin() {
  // This is mostly for the first boot, we need to initialize the panic info and logs to empty state
  // If we reboot from a panic state, we want to keep the panic info until we successfully dump it to the SD card, use
  // `clearPanic()` to clear it after dumping
  if (!isRebootFromPanic()) {
    clearPanic();
  } else {
    if (panicCapture.magic != PANIC_CAPTURE_MAGIC) {
      panicMessage[0] = '\0';
      clearPanicTrace();
      panicCapture.magic = PANIC_CAPTURE_MAGIC;
    }
    // Panic reboot: preserve logs and panic info, but clamp logHead in case the
    // panic occurred before begin() ever ran (e.g. in a static constructor).
    // If logHead was out of range, logMessages is also garbage — clear it so
    // getLastLogs() does not dump corrupt data into the crash report.
    if (sanitizeLogHead()) {
      clearLastLogs();
    }
  }

  // Arduino-ESP32 3.x walks the Xtensa exception frame before invoking this
  // callback. Copy only fixed-size data into RTC memory; never allocate or log
  // from panic context.
  set_arduino_panic_handler(captureArduinoPanic, nullptr);
}

void checkPanic() {
  if (isRebootFromPanic()) {
    auto panicInfo = getPanicInfo(true);
    auto file = Storage.open("/crash_report.txt", O_WRITE | O_CREAT | O_TRUNC);
    if (file) {
      file.write(panicInfo.c_str(), panicInfo.size());
      file.close();
      LOG_INF("SYS", "Dumped panic info to SD card");
    } else {
      LOG_ERR("SYS", "Failed to open crash_report.txt for writing");
    }
  }
}

void clearPanic() {
  panicMessage[0] = '\0';
  clearPanicTrace();
  panicCapture.magic = PANIC_CAPTURE_MAGIC;
  clearLastLogs();
}

std::string getPanicInfo(bool full) {
  if (!full) {
    return panicMessage;
  } else {
    std::string info;

    info += CROSSDITO_PRODUCT_NAME " version: " CROSSINK_VERSION;
    info += "\nUpstream base: " CROSSDITO_UPSTREAM_PRODUCT_NAME " " CROSSDITO_UPSTREAM_VERSION;
    info += "\n" CROSSDITO_PRODUCT_NAME " device type: " CROSSINK_FIRMWARE_DEVICE_TYPE;
    info += "\n\nPanic reason: ";
    info += panicMessage[0] ? panicMessage : "(not captured)";
    char summary[64];
    snprintf(summary, sizeof(summary), "\nPanic core: %d\nPanic PC: 0x%08X", static_cast<int>(panicCapture.core),
             panicCapture.pc);
    info += summary;
    info += "\n\nLast logs:\n" + getLastLogs();
    info += "\n\nBacktrace:\n";

    auto toHex = [](uint32_t value) {
      char buffer[9];
      snprintf(buffer, sizeof(buffer), "%08X", value);
      return std::string(buffer);
    };
    for (size_t i = 0; i < panicCapture.backtraceLength; ++i) {
      info += "0x" + toHex(panicCapture.backtrace[i]);
      info += (i + 1) % 8 == 0 ? "\n" : " ";
    }
    if (panicCapture.backtraceLength == 0 || panicCapture.backtraceLength % 8 != 0) info += "\n";
    if ((panicCapture.flags & PANIC_FLAG_BACKTRACE_CORRUPT) != 0) info += "[backtrace corrupt]\n";
    if ((panicCapture.flags & PANIC_FLAG_BACKTRACE_CONTINUES) != 0) info += "[backtrace truncated]\n";

    return info;
  }
}

bool isRebootFromPanic() {
  const auto resetReason = esp_reset_reason();
  return resetReason == ESP_RST_PANIC || resetReason == ESP_RST_CPU_LOCKUP || resetReason == ESP_RST_INT_WDT ||
         resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT;
}

}  // namespace HalSystem
