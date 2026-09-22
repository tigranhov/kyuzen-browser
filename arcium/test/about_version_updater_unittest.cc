// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/update/about_version_updater.h"

#include <string>
#include <vector>

#include "arcium/browser/update/update_status.h"
#include "arcium/test/fake_update_status.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

using State = UpdateStatus::State;

// Everything the About page was told, in order.
class Recorder {
 public:
  VersionUpdater::StatusCallback Callback() {
    return base::BindRepeating(&Recorder::Record, base::Unretained(this));
  }
  VersionUpdater::Status last() const { return statuses_.back(); }
  const std::u16string& last_message() const { return last_message_; }
  size_t count() const { return statuses_.size(); }

 private:
  void Record(VersionUpdater::Status status,
              int progress,
              bool rollback,
              bool powerwash,
              const std::string& version,
              int64_t update_size,
              const std::u16string& message) {
    statuses_.push_back(status);
    last_message_ = message;
  }

  std::vector<VersionUpdater::Status> statuses_;
  std::u16string last_message_;
};

TEST(AboutVersionUpdaterTest, WithoutAnUpdaterThePageSaysNothingAboutUpdates) {
  // Every build but a release: the page hides its update line rather than
  // claiming to be up to date.
  AboutVersionUpdater updater(nullptr);
  Recorder recorder;
  updater.CheckForUpdate(recorder.Callback(), base::DoNothing());
  EXPECT_EQ(VersionUpdater::DISABLED, recorder.last());
}

TEST(AboutVersionUpdaterTest, OpeningThePageLooksAndSaysSoAtOnce) {
  FakeUpdateStatus status;
  AboutVersionUpdater updater(&status);
  Recorder recorder;
  updater.CheckForUpdate(recorder.Callback(), base::DoNothing());
  EXPECT_EQ(1, status.checks());
  EXPECT_EQ(VersionUpdater::CHECKING, recorder.last())
      << "the look has been asked for, even before Sparkle says it started";
}

TEST(AboutVersionUpdaterTest, ALookThatFindsNothingEndsUpToDate) {
  FakeUpdateStatus status;
  AboutVersionUpdater updater(&status);
  Recorder recorder;
  updater.CheckForUpdate(recorder.Callback(), base::DoNothing());
  status.SetState(State::kChecking);
  status.SetState(State::kIdle);
  EXPECT_EQ(VersionUpdater::UPDATED, recorder.last());
}

TEST(AboutVersionUpdaterTest, ItFollowsTheUpdaterToARelaunch) {
  FakeUpdateStatus status;
  AboutVersionUpdater updater(&status);
  Recorder recorder;
  updater.CheckForUpdate(recorder.Callback(), base::DoNothing());
  status.SetState(State::kUpdateAvailable);
  EXPECT_EQ(VersionUpdater::UPDATING, recorder.last());
  status.SetState(State::kDownloading);
  EXPECT_EQ(VersionUpdater::UPDATING, recorder.last());
  status.SetState(State::kReadyToRelaunch);
  EXPECT_EQ(VersionUpdater::NEARLY_UPDATED, recorder.last())
      << "which is the state the page offers its Relaunch button in";
}

TEST(AboutVersionUpdaterTest, AStagedVersionIsOfferedTheMomentThePageOpens) {
  FakeUpdateStatus status;
  status.SetState(State::kReadyToRelaunch);
  AboutVersionUpdater updater(&status);
  Recorder recorder;
  updater.CheckForUpdate(recorder.Callback(), base::DoNothing());
  EXPECT_EQ(VersionUpdater::NEARLY_UPDATED, recorder.last());
}

TEST(AboutVersionUpdaterTest, AFailureIsExplained) {
  FakeUpdateStatus status;
  AboutVersionUpdater updater(&status);
  Recorder recorder;
  updater.CheckForUpdate(recorder.Callback(), base::DoNothing());
  status.SetState(State::kFailed);
  EXPECT_EQ(VersionUpdater::FAILED, recorder.last());
  EXPECT_FALSE(recorder.last_message().empty());
}

TEST(AboutVersionUpdaterTest, AClosedPageHearsNothingMore) {
  FakeUpdateStatus status;
  Recorder recorder;
  {
    AboutVersionUpdater updater(&status);
    updater.CheckForUpdate(recorder.Callback(), base::DoNothing());
  }
  const size_t heard = recorder.count();
  status.SetState(State::kDownloading);
  EXPECT_EQ(heard, recorder.count());
}

}  // namespace
}  // namespace arcium
