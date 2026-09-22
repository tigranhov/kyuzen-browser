// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/update/update_controller.h"

#include <memory>
#include <utility>

#include "arcium/browser/update/update_preference.h"
#include "arcium/browser/update/update_status.h"
#include "arcium/test/fake_updater_backend.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class UpdateControllerTest : public testing::Test {
 protected:
  UpdateControllerTest() { RegisterUpdatePrefs(prefs_.registry()); }

  std::unique_ptr<UpdateController> MakeController() {
    auto backend = std::make_unique<FakeUpdaterBackend>();
    backend_ = backend.get();
    return std::make_unique<UpdateController>(&prefs_, std::move(backend));
  }

  void SetMode(UpdateMode mode) { SetUpdateMode(&prefs_, mode); }

  FakeUpdaterBackend* backend() { return backend_; }

  TestingPrefServiceSimple prefs_;
  raw_ptr<FakeUpdaterBackend> backend_ = nullptr;
};

TEST_F(UpdateControllerTest, AskingLooksButDoesNotFetchByItself) {
  SetMode(UpdateMode::kAsk);
  std::unique_ptr<UpdateController> controller = MakeController();
  EXPECT_TRUE(backend()->checks_automatically());
  EXPECT_FALSE(backend()->downloads_automatically())
      << "asking means the reader decides before anything is fetched";
}

TEST_F(UpdateControllerTest, InstallingQuietlyFetchesWithoutAsking) {
  SetMode(UpdateMode::kAutomatic);
  std::unique_ptr<UpdateController> controller = MakeController();
  EXPECT_TRUE(backend()->checks_automatically());
  EXPECT_TRUE(backend()->downloads_automatically());
}

TEST_F(UpdateControllerTest, NeverCheckingAsksTheFeedForNothing) {
  SetMode(UpdateMode::kOff);
  std::unique_ptr<UpdateController> controller = MakeController();
  EXPECT_FALSE(backend()->checks_automatically());
  EXPECT_EQ(0, backend()->background_checks())
      << "off has to mean no request left this machine at all";
}

TEST_F(UpdateControllerTest, ACheckByHandHappensEvenWhenCheckingIsOff) {
  // Off means "do not go looking", not "refuse to look when asked", which is
  // what the About page's own button asks for.
  SetMode(UpdateMode::kOff);
  std::unique_ptr<UpdateController> controller = MakeController();
  controller->CheckNow();
  EXPECT_EQ(1, backend()->checks_by_hand());
}

TEST_F(UpdateControllerTest, ChangingTheSettingTakesEffectWithoutARelaunch) {
  SetMode(UpdateMode::kOff);
  std::unique_ptr<UpdateController> controller = MakeController();
  ASSERT_FALSE(backend()->checks_automatically());

  SetMode(UpdateMode::kAutomatic);

  EXPECT_TRUE(backend()->checks_automatically());
  EXPECT_TRUE(backend()->downloads_automatically());
}

TEST_F(UpdateControllerTest, AfterShutdownTheSettingIsNoLongerWatched) {
  // The controller outlives the setting's store at shutdown, because Sparkle
  // installs a staged version as the application quits, so it has to let go
  // of the store first.
  SetMode(UpdateMode::kOff);
  std::unique_ptr<UpdateController> controller = MakeController();
  controller->Shutdown();

  SetMode(UpdateMode::kAutomatic);

  EXPECT_FALSE(backend()->checks_automatically());
}

TEST_F(UpdateControllerTest, ItReportsWhatTheUpdaterIsDoing) {
  SetMode(UpdateMode::kAsk);
  std::unique_ptr<UpdateController> controller = MakeController();
  EXPECT_EQ(UpdateStatus::State::kIdle, controller->CurrentState());
  EXPECT_FALSE(controller->RelaunchIsPending());

  backend()->ReportState(UpdateStatus::State::kDownloading);
  EXPECT_EQ(UpdateStatus::State::kDownloading, controller->CurrentState());
  EXPECT_FALSE(controller->RelaunchIsPending());

  backend()->ReportState(UpdateStatus::State::kReadyToRelaunch);
  EXPECT_TRUE(controller->RelaunchIsPending())
      << "a staged version is the only thing that makes relaunching an offer";
}

}  // namespace
}  // namespace arcium
