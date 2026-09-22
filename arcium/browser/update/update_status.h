// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_UPDATE_UPDATE_STATUS_H_
#define ARCIUM_BROWSER_UPDATE_UPDATE_STATUS_H_

namespace arcium {

// What the browser knows about newer versions of itself, and the one thing a
// reader can ask it to do. Kept apart from whatever does the work so that
// everything above it can be written and tested without a framework, a
// network or a feed -- which matters here more than usual, because the thing
// underneath eventually replaces the application on disk.
class UpdateStatus {
 public:
  enum class State {
    // Nothing happening, and nothing waiting.
    kIdle,
    kChecking,
    // A newer version exists and has not been fetched yet.
    kUpdateAvailable,
    kDownloading,
    // Fetched and staged: the next launch is the new one.
    kReadyToRelaunch,
    // The last attempt did not finish. The installed copy is untouched.
    kFailed,
  };

  virtual ~UpdateStatus() = default;

  virtual State CurrentState() const = 0;

  // True once a newer version is staged, which is the moment the About page
  // has something to offer beyond a check.
  virtual bool RelaunchIsPending() const = 0;

  // Look now, whatever the setting says. Asking is not the same as being
  // looked for, so this works even when automatic checking is off.
  virtual void CheckNow() = 0;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_UPDATE_UPDATE_STATUS_H_
