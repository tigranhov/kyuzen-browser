// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_TEST_FAKE_UPDATER_BACKEND_H_
#define ARCIUM_TEST_FAKE_UPDATER_BACKEND_H_

#include <utility>

#include "arcium/browser/update/update_status.h"
#include "arcium/browser/update/updater_backend.h"

namespace arcium {

// An updater that downloads nothing and replaces nothing: it records what it
// was told and reports whatever a test says it found. Everything above the
// real framework is written against this, because the real one ends by
// replacing the application on disk.
class FakeUpdaterBackend : public UpdaterBackend {
 public:
  FakeUpdaterBackend() = default;
  ~FakeUpdaterBackend() override = default;

  // UpdaterBackend:
  void SetStateCallback(StateCallback callback) override {
    state_callback_ = std::move(callback);
  }
  void SetChecksAutomatically(bool checks) override {
    checks_automatically_ = checks;
  }
  void SetDownloadsAutomatically(bool downloads) override {
    downloads_automatically_ = downloads;
  }
  void CheckInBackground() override { ++background_checks_; }
  void CheckByHand() override { ++checks_by_hand_; }

  void ReportState(UpdateStatus::State state) {
    if (state_callback_) {
      state_callback_.Run(state);
    }
  }

  bool checks_automatically() const { return checks_automatically_; }
  bool downloads_automatically() const { return downloads_automatically_; }
  int background_checks() const { return background_checks_; }
  int checks_by_hand() const { return checks_by_hand_; }

 private:
  StateCallback state_callback_;
  bool checks_automatically_ = false;
  bool downloads_automatically_ = false;
  int background_checks_ = 0;
  int checks_by_hand_ = 0;
};

}  // namespace arcium

#endif  // ARCIUM_TEST_FAKE_UPDATER_BACKEND_H_
