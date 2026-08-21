#pragma once

#include <cstdint>

// Tracks tap intent without changing the selected carousel book on touch-down.
// Selection is deferred until release so the same contact can instead become a
// one-step swipe without first selecting the cover under the finger.
class CarouselCoverTouch {
 public:
  enum class TapAction : uint8_t { Select, Activate };

  constexpr void begin(const int bookIndex, const bool wasSelected) {
    downIndex = bookIndex;
    downWasSelected = wasSelected;
  }

  constexpr TapAction releaseTap(const int bookIndex, const bool selectedWithoutDown) {
    const bool activate = (downIndex == bookIndex && downWasSelected) || (downIndex < 0 && selectedWithoutDown);
    cancel();
    return activate ? TapAction::Activate : TapAction::Select;
  }

  constexpr void cancel() {
    downIndex = -1;
    downWasSelected = false;
  }

 private:
  int downIndex = -1;
  bool downWasSelected = false;
};

