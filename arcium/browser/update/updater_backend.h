// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_UPDATE_UPDATER_BACKEND_H_
#define ARCIUM_BROWSER_UPDATE_UPDATER_BACKEND_H_

#include "arcium/browser/update/update_status.h"
#include "base/functional/callback.h"

namespace arcium {

// The little that the browser asks of whatever actually looks for new
// versions and installs them. Four instructions and one report, so that the
// part deciding when to look can be written and tested without the part that
// downloads code and replaces the application.
class UpdaterBackend {
 public:
  using StateCallback = base::RepeatingCallback<void(UpdateStatus::State)>;

  virtual ~UpdaterBackend() = default;

  // How the backend says what it is doing. Set once, before anything else.
  virtual void SetStateCallback(StateCallback callback) = 0;

  // Whether to look on its own schedule, and whether to fetch what it finds
  // without being asked.
  virtual void SetChecksAutomatically(bool checks) = 0;
  virtual void SetDownloadsAutomatically(bool downloads) = 0;

  // Look now, quietly: nothing is shown if there is nothing to offer.
  virtual void CheckInBackground() = 0;

  // Look now because a reader asked, which means saying so either way.
  virtual void CheckByHand() = 0;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_UPDATE_UPDATER_BACKEND_H_
