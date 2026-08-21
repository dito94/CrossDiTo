#include <gtest/gtest.h>

#include "CarouselCoverTouch.h"

TEST(CarouselCoverTouchTest, SideCoverTapSelectsWithoutActivating) {
  CarouselCoverTouch touch;
  touch.begin(/*bookIndex=*/2, /*wasSelected=*/false);

  EXPECT_EQ(touch.releaseTap(/*bookIndex=*/2, /*selectedWithoutDown=*/false), CarouselCoverTouch::TapAction::Select);
}

TEST(CarouselCoverTouchTest, SelectedCoverTapActivates) {
  CarouselCoverTouch touch;
  touch.begin(/*bookIndex=*/1, /*wasSelected=*/true);

  EXPECT_EQ(touch.releaseTap(/*bookIndex=*/1, /*selectedWithoutDown=*/true), CarouselCoverTouch::TapAction::Activate);
}

TEST(CarouselCoverTouchTest, SwipeCancellationDiscardsSideCoverSelection) {
  CarouselCoverTouch touch;
  int selectorIndex = 0;
  constexpr int bookCount = 3;

  touch.begin(/*bookIndex=*/1, /*wasSelected=*/false);
  touch.cancel();  // The release became a left swipe, not a tap.
  selectorIndex = (selectorIndex + 1) % bookCount;

  EXPECT_EQ(selectorIndex, 1);  // Exactly the swipe step; no touch-down step.
}

TEST(CarouselCoverTouchTest, MissingDownStillActivatesAlreadySelectedCover) {
  CarouselCoverTouch touch;

  EXPECT_EQ(touch.releaseTap(/*bookIndex=*/1, /*selectedWithoutDown=*/true), CarouselCoverTouch::TapAction::Activate);
}
