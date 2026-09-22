// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/update/sparkle_event.h"

#include "arcium/browser/update/update_status.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

using State = UpdateStatus::State;

TEST(SparkleEventTest, ACheckThatFindsNothingEndsIdle) {
  State state = StateAfter(State::kIdle, SparkleEvent::kCheckStarted);
  EXPECT_EQ(State::kChecking, state);
  EXPECT_EQ(State::kIdle, StateAfter(state, SparkleEvent::kCycleFinished));
}

TEST(SparkleEventTest, TheEndOfACycleDoesNotHideWhatItFound) {
  // Sparkle finishes its cycle while its window is still offering the
  // update, so the end of the cycle is not the end of the offer.
  EXPECT_EQ(State::kUpdateAvailable,
            StateAfter(State::kUpdateAvailable, SparkleEvent::kCycleFinished));
  EXPECT_EQ(State::kReadyToRelaunch,
            StateAfter(State::kReadyToRelaunch, SparkleEvent::kCycleFinished));
}

TEST(SparkleEventTest, DecliningAnOfferPutsItAway) {
  EXPECT_EQ(State::kIdle,
            StateAfter(State::kUpdateAvailable, SparkleEvent::kUpdateDeclined));
}

TEST(SparkleEventTest, NothingTakesBackAStagedVersion) {
  // Once staged it installs when the browser quits, whatever happens after,
  // so the About page must go on offering the relaunch.
  EXPECT_EQ(State::kReadyToRelaunch,
            StateAfter(State::kReadyToRelaunch, SparkleEvent::kUpdateDeclined));
  EXPECT_EQ(State::kReadyToRelaunch,
            StateAfter(State::kReadyToRelaunch, SparkleEvent::kCycleFailed));
}

TEST(SparkleEventTest, AFailureIsReported) {
  EXPECT_EQ(State::kFailed,
            StateAfter(State::kChecking, SparkleEvent::kCycleFailed));
  EXPECT_EQ(State::kFailed,
            StateAfter(State::kDownloading, SparkleEvent::kDownloadFailed));
}

TEST(SparkleEventTest, TheWholePathToAStagedVersion) {
  State state = State::kIdle;
  state = StateAfter(state, SparkleEvent::kCheckStarted);
  state = StateAfter(state, SparkleEvent::kUpdateFound);
  EXPECT_EQ(State::kUpdateAvailable, state);
  state = StateAfter(state, SparkleEvent::kDownloadStarted);
  EXPECT_EQ(State::kDownloading, state);
  state = StateAfter(state, SparkleEvent::kInstallScheduled);
  EXPECT_EQ(State::kReadyToRelaunch, state);
}

}  // namespace
}  // namespace arcium
