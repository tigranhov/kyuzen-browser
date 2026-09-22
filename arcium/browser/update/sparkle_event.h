// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_UPDATE_SPARKLE_EVENT_H_
#define ARCIUM_BROWSER_UPDATE_SPARKLE_EVENT_H_

#include "arcium/browser/update/update_status.h"

namespace arcium {

// The moments Sparkle tells its delegate about, reduced to the ones that change
// what the About page should say.
enum class SparkleEvent {
  kCheckStarted,
  kUpdateFound,
  kDownloadStarted,
  kDownloadFailed,
  // Downloaded and set to replace the application when it next quits.
  kInstallScheduled,
  // The reader skipped the offer or put it off.
  kUpdateDeclined,
  // Sparkle's cycle ended with nothing wrong, including finding nothing.
  kCycleFinished,
  kCycleFailed,
};

// Kept apart from the delegate that receives these so that it can be tested
// without the framework, which a unit test cannot load.
UpdateStatus::State StateAfter(UpdateStatus::State current, SparkleEvent event);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_UPDATE_SPARKLE_EVENT_H_
