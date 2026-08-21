#include "FrontlightSchedule.h"

#include <HalClock.h>

#include <cstdio>

#include "CrossPointSettings.h"

namespace FrontlightSchedule {

void formatTimeSlot(uint8_t slot, const bool use12Hour, char* buffer, const size_t bufferSize) {
  if (!buffer || bufferSize == 0) return;
  slot %= SLOT_COUNT;
  const uint8_t hour24 = slot / SLOTS_PER_HOUR;
  const uint8_t minute = (slot % SLOTS_PER_HOUR) * SLOT_MINUTES;
  if (!use12Hour) {
    snprintf(buffer, bufferSize, "%02u:%02u", static_cast<unsigned>(hour24), static_cast<unsigned>(minute));
    return;
  }

  const bool pm = hour24 >= 12;
  uint8_t hour12 = hour24 % 12;
  if (hour12 == 0) hour12 = 12;
  snprintf(buffer, bufferSize, "%u:%02u %s", static_cast<unsigned>(hour12), static_cast<unsigned>(minute),
           pm ? "PM" : "AM");
}

bool currentState(bool& shouldBeOn) {
  if (SETTINGS.frontlightScheduleEnabled == 0 || SETTINGS.clockHasBeenSynced == 0 || !halClock.isAvailable()) {
    return false;
  }

  uint8_t hour = 0;
  uint8_t minute = 0;
  if (!halClock.getTime(hour, minute)) return false;

  const uint8_t utcOffsetQ = SETTINGS.clockUtcOffsetQ <= 104 ? SETTINGS.clockUtcOffsetQ : 104;
  const int offsetMinutes = (static_cast<int>(utcOffsetQ) - 48) * 15;
  int localMinute = static_cast<int>(hour) * 60 + static_cast<int>(minute) + offsetMinutes;
  localMinute = ((localMinute % (24 * 60)) + (24 * 60)) % (24 * 60);
  shouldBeOn = isActiveAtMinute(static_cast<uint16_t>(localMinute), SETTINGS.frontlightScheduleStartQ,
                                SETTINGS.frontlightScheduleEndQ);
  return true;
}

bool verifyContract() {
  constexpr uint8_t evening = 19 * SLOTS_PER_HOUR;
  constexpr uint8_t morning = 7 * SLOTS_PER_HOUR;
  constexpr uint8_t workStart = 8 * SLOTS_PER_HOUR;
  constexpr uint8_t workEnd = 17 * SLOTS_PER_HOUR;
  return isActiveAtMinute(19 * 60, evening, morning) && isActiveAtMinute(23 * 60 + 59, evening, morning) &&
         isActiveAtMinute(6 * 60 + 59, evening, morning) && !isActiveAtMinute(7 * 60, evening, morning) &&
         !isActiveAtMinute(12 * 60, evening, morning) && isActiveAtMinute(8 * 60, workStart, workEnd) &&
         !isActiveAtMinute(17 * 60, workStart, workEnd) && isActiveAtMinute(12 * 60, workStart, workStart);
}

}  // namespace FrontlightSchedule
