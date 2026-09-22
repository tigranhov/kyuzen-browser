// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/update/sparkle_event.h"

namespace arcium {

using State = UpdateStatus::State;

State StateAfter(State current, SparkleEvent event) {
  // A staged version installs when the browser quits whatever Sparkle does
  // next, so nothing it reports afterwards makes the relaunch less true.
  if (current == State::kReadyToRelaunch) {
    return current;
  }
  switch (event) {
    case SparkleEvent::kCheckStarted:
      return State::kChecking;
    case SparkleEvent::kUpdateFound:
      return State::kUpdateAvailable;
    case SparkleEvent::kDownloadStarted:
      return State::kDownloading;
    case SparkleEvent::kDownloadFailed:
    case SparkleEvent::kCycleFailed:
      return State::kFailed;
    case SparkleEvent::kInstallScheduled:
      return State::kReadyToRelaunch;
    case SparkleEvent::kUpdateDeclined:
      return State::kIdle;
    case SparkleEvent::kCycleFinished:
      // The cycle ends while Sparkle's window may still be offering what it
      // found, so only a look that found nothing goes back to idle.
      return current == State::kChecking ? State::kIdle : current;
  }
}

}  // namespace arcium
