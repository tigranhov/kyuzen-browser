// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_TEST_FAKE_UPDATE_STATUS_H_
#define ARCIUM_TEST_FAKE_UPDATE_STATUS_H_

#include <utility>

#include "arcium/browser/update/update_status.h"
#include "base/callback_list.h"

namespace arcium {

// What the About page reads, set by a test instead of by Sparkle.
class FakeUpdateStatus : public UpdateStatus {
 public:
  FakeUpdateStatus() = default;
  ~FakeUpdateStatus() override = default;

  void SetState(State state) {
    state_ = state;
    listeners_.Notify();
  }
  int checks() const { return checks_; }

  // UpdateStatus:
  State CurrentState() const override { return state_; }
  bool RelaunchIsPending() const override {
    return state_ == State::kReadyToRelaunch;
  }
  void CheckNow() override { ++checks_; }
  base::CallbackListSubscription Subscribe(
      base::RepeatingClosure on_change) override {
    return listeners_.Add(std::move(on_change));
  }

 private:
  State state_ = State::kIdle;
  int checks_ = 0;
  base::RepeatingClosureList listeners_;
};

}  // namespace arcium

#endif  // ARCIUM_TEST_FAKE_UPDATE_STATUS_H_
