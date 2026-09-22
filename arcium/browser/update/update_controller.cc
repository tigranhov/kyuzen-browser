// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/update/update_controller.h"

#include <utility>

#include "arcium/browser/update/update_preference.h"
#include "base/functional/bind.h"

namespace arcium {

UpdateController::UpdateController(PrefService* local_state,
                                   std::unique_ptr<UpdaterBackend> backend)
    : local_state_(local_state), backend_(std::move(backend)) {
  backend_->SetStateCallback(base::BindRepeating(
      &UpdateController::OnStateChanged, weak_factory_.GetWeakPtr()));
  registrar_.Init(local_state_);
  // Watched rather than read once, because a reader who turns checking on
  // expects it on, not on after the next launch.
  registrar_.Add(prefs::kUpdateMode,
                 base::BindRepeating(&UpdateController::ApplySetting,
                                     base::Unretained(this)));
  ApplySetting();
}

UpdateController::~UpdateController() = default;

UpdateStatus::State UpdateController::CurrentState() const {
  return state_;
}

bool UpdateController::RelaunchIsPending() const {
  return state_ == State::kReadyToRelaunch;
}

void UpdateController::CheckNow() {
  // Asked for, so it happens whatever the setting says: never checking means
  // never going to look unprompted, not refusing to look when prompted.
  backend_->CheckByHand();
}

base::CallbackListSubscription UpdateController::Subscribe(
    base::RepeatingClosure on_change) {
  return listeners_.Add(std::move(on_change));
}

void UpdateController::Shutdown() {
  registrar_.RemoveAll();
}

void UpdateController::ApplySetting() {
  const UpdateMode mode = GetUpdateMode(local_state_);
  backend_->SetChecksAutomatically(mode != UpdateMode::kOff);
  backend_->SetDownloadsAutomatically(mode == UpdateMode::kAutomatic);
  if (mode == UpdateMode::kOff) {
    return;
  }
  // The first look of this run. Quiet, because nothing should appear from a
  // browser that has just started and has nothing to report.
  backend_->CheckInBackground();
}

void UpdateController::OnStateChanged(State state) {
  state_ = state;
  listeners_.Notify();
}

}  // namespace arcium
