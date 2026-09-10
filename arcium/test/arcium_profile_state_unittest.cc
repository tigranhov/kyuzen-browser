// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/arcium_profile_state.h"

#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/model_store.h"
#include "arcium/browser/tab_binding.h"
#include "base/files/file_util.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/segmentation_platform/public/features.h"
#include "components/tabs/public/tab_interface.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class ArciumProfileStateTest : public BrowserWithTestWindowTest {
 public:
  ArciumProfileStateTest()
      : BrowserWithTestWindowTest(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    // Moving the clock wakes the segmentation platform, which parks a task
    // runner in a process-global object and makes the next test in the binary
    // complain about "a previous test leaving a stale task runner in a global
    // object". Nothing here is about segmentation; turn it off. By the typed
    // constant, so an upstream rename is a build error rather than a silently
    // disarmed guard, and by disabling one feature rather than replacing the
    // whole list.
    scoped_feature_list_.InitAndDisableFeature(
        segmentation_platform::features::kSegmentationPlatformFeature);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

// C1. OffTheRecordProfileImpl::GetPath() returns the *parent* profile's
// path, so an incognito ModelStore would write incognito browsing state into
// the regular profile's file and race the regular profile's own writer for
// it. Incognito gets a model and a binding, and no store at all.
TEST_F(ArciumProfileStateTest, IncognitoGetsNoStoreAndWritesNothing) {
  Profile* otr = profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  ASSERT_TRUE(otr);
  ASSERT_TRUE(otr->IsOffTheRecord());
  // The premise of the bug: same directory, so the same file.
  ASSERT_EQ(profile()->GetPath(), otr->GetPath());
  const base::FilePath model_path =
      ArciumProfileState::ModelPath(otr->GetPath());
  ASSERT_FALSE(base::PathExists(model_path));

  ArciumProfileState* state = ArciumProfileState::GetForBrowserContext(otr);
  ASSERT_TRUE(state);
  EXPECT_FALSE(state->store());

  state->model()->AddEntryForTesting(
      EntryKind::kPinned, GURL("https://secret.example/"), u"Secret");
  task_environment()->FastForwardBy(ModelStore::kSaveDelay * 2);
  task_environment()->RunUntilIdle();

  EXPECT_FALSE(base::PathExists(model_path));
}

// The same trap, for the archive. ArchivePath() is built from the same
// GetPath(), so an incognito ArchiveStore would write incognito tabs into the
// regular profile's `Arcium Archive` — and unlike a live model, a database row
// outlives the window that wrote it. Incognito gets no archive at all, which
// is what makes BrowserSidebarController create no ArchiveService for it.
TEST_F(ArciumProfileStateTest, IncognitoGetsNoArchive) {
  Profile* otr = profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  ASSERT_TRUE(otr);
  const base::FilePath archive_path =
      ArciumProfileState::ArchivePath(otr->GetPath());
  ASSERT_FALSE(base::PathExists(archive_path));

  ArciumProfileState* state = ArciumProfileState::GetForBrowserContext(otr);
  ASSERT_TRUE(state);
  EXPECT_FALSE(state->archive());
  EXPECT_FALSE(state->archive_runner());

  task_environment()->RunUntilIdle();
  EXPECT_FALSE(base::PathExists(archive_path));
}

// The positive control, and the "never write on the UI thread" half of it:
// the regular profile has an archive, and it is not this sequence's to touch.
TEST_F(ArciumProfileStateTest, ARegularProfileHasAnArchiveOffTheUiThread) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContext(profile());
  ASSERT_TRUE(state->archive());
  ASSERT_TRUE(state->archive_runner());
  EXPECT_FALSE(state->archive_runner()->RunsTasksInCurrentSequence());

  task_environment()->RunUntilIdle();
  EXPECT_TRUE(
      base::PathExists(ArciumProfileState::ArchivePath(profile()->GetPath())));
}

// The positive control for the test above: the regular profile does persist,
// to that same path, which is precisely why incognito must not.
TEST_F(ArciumProfileStateTest, ARegularProfileHasAStoreAndWrites) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContext(profile());
  ASSERT_TRUE(state);
  ASSERT_TRUE(state->store());

  state->model()->AddEntryForTesting(EntryKind::kPinned,
                                     GURL("https://a.example/"), u"A");
  task_environment()->FastForwardBy(ModelStore::kSaveDelay * 2);
  task_environment()->RunUntilIdle();

  EXPECT_TRUE(
      base::PathExists(ArciumProfileState::ModelPath(profile()->GetPath())));
}

TEST_F(ArciumProfileStateTest, IncognitoGetsItsOwnIndependentModel) {
  ArciumProfileState* regular =
      ArciumProfileState::GetForBrowserContext(profile());
  Profile* otr = profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  ArciumProfileState* incognito = ArciumProfileState::GetForBrowserContext(otr);

  EXPECT_NE(regular, incognito);
  EXPECT_NE(regular->model(), incognito->model());
  EXPECT_NE(regular->binding(), incognito->binding());

  incognito->model()->AddEntryForTesting(
      EntryKind::kPinned, GURL("https://secret.example/"), u"Secret");
  EXPECT_EQ(1u, incognito->model()->entries().size());
  EXPECT_TRUE(regular->model()->entries().empty());

  task_environment()->FastForwardBy(ModelStore::kSaveDelay * 2);
  task_environment()->RunUntilIdle();
}

TEST_F(ArciumProfileStateTest, TheSameStateComesBackForTheSameContext) {
  EXPECT_EQ(ArciumProfileState::GetForBrowserContext(profile()),
            ArciumProfileState::GetForBrowserContext(profile()));
}

// I3, second half: the state owns both the model and the binding, so it is
// the one place that can drop a binding whose entry has gone away. Once per
// profile rather than once per window.
TEST_F(ArciumProfileStateTest, ABindingWhoseEntryVanishesIsReleased) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContext(profile());
  AddTab(browser(), GURL("https://a.example/"));
  const tabs::TabHandle handle =
      browser()->tab_strip_model()->GetTabAtIndex(0)->GetHandle();

  const EntryId id = state->model()->AddEntryForTesting(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");
  state->binding()->Bind(id, handle);
  ASSERT_TRUE(state->binding()->IsBound(handle));

  // What ModelStore::Load's completion does: entries replaced wholesale,
  // TabBinding untouched.
  std::vector<Space> spaces = state->model()->spaces();
  state->model()->ReplaceAll(std::move(spaces), {}, {});

  EXPECT_FALSE(state->binding()->IsBound(handle));
  EXPECT_FALSE(state->binding()->TabForEntry(id).has_value());

  task_environment()->FastForwardBy(ModelStore::kSaveDelay * 2);
  task_environment()->RunUntilIdle();
}

TEST_F(ArciumProfileStateTest, ALiveEntrysBindingSurvivesAReplaceAll) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContext(profile());
  AddTab(browser(), GURL("https://a.example/"));
  const tabs::TabHandle handle =
      browser()->tab_strip_model()->GetTabAtIndex(0)->GetHandle();

  const EntryId id = state->model()->AddEntryForTesting(
      EntryKind::kPinned, GURL("https://a.example/"), u"A");
  state->binding()->Bind(id, handle);

  std::vector<Space> spaces = state->model()->spaces();
  std::vector<TabEntry> entries = state->model()->entries();
  state->model()->ReplaceAll(std::move(spaces), {}, std::move(entries));

  EXPECT_TRUE(state->binding()->IsBound(handle));

  task_environment()->FastForwardBy(ModelStore::kSaveDelay * 2);
  task_environment()->RunUntilIdle();
}

}  // namespace
}  // namespace arcium
