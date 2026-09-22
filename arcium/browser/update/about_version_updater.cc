// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/update/about_version_updater.h"

#include <string>
#include <utility>

#include "arcium/browser/update/browser_update_status.h"
#include "arcium/browser/update/update_status.h"
#include "base/functional/bind.h"

namespace arcium {

using State = UpdateStatus::State;

AboutVersionUpdater::AboutVersionUpdater(UpdateStatus* status)
    : status_(status) {}

AboutVersionUpdater::~AboutVersionUpdater() = default;

void AboutVersionUpdater::CheckForUpdate(StatusCallback status_callback,
                                         PromoteCallback promote_callback) {
  status_callback_ = std::move(status_callback);
  if (!status_) {
    // Hides the page's update line rather than claiming to be up to date.
    status_callback_.Run(DISABLED, 0, false, false, std::string(), 0,
                         std::u16string());
    return;
  }
  subscription_ = status_->Subscribe(base::BindRepeating(
      &AboutVersionUpdater::Report, base::Unretained(this)));
  // A version already staged is offered as it is; looking again would only
  // find the same one.
  if (!status_->RelaunchIsPending()) {
    look_pending_ = true;
    status_->CheckNow();
  }
  Report();
}

void AboutVersionUpdater::Report() {
  const State state = status_->CurrentState();
  if (state != State::kIdle) {
    look_pending_ = false;
  }
  Status status = UPDATED;
  std::u16string message;
  switch (state) {
    case State::kIdle:
      status = look_pending_ ? CHECKING : UPDATED;
      break;
    case State::kChecking:
      status = CHECKING;
      break;
    case State::kUpdateAvailable:
    case State::kDownloading:
      status = UPDATING;
      break;
    case State::kReadyToRelaunch:
      status = NEARLY_UPDATED;
      break;
    case State::kFailed:
      status = FAILED;
      message =
          u"Kyuzen could not finish checking for updates. It will try again "
          u"later.";
      break;
  }
  status_callback_.Run(status, 0, false, false, std::string(), 0, message);
}

std::unique_ptr<VersionUpdater> MakeAboutVersionUpdater() {
  return std::make_unique<AboutVersionUpdater>(GetBrowserUpdateStatus());
}

}  // namespace arcium
