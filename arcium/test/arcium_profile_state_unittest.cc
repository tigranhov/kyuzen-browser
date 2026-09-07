// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/arcium_profile_state.h"

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/model_store.h"
#include "arcium/browser/tab_binding.h"
#include "base/files/file_util.h"
#include "base/test/task_environment.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class ArciumProfileStateTest : public BrowserWithTestWindowTest {
 public:
  ArciumProfileStateTest()
      : BrowserWithTestWindowTest(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
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

  state->model()->AddEntry(EntryKind::kPinned, GURL("https://secret.example/"),
                           u"Secret");
  task_environment()->FastForwardBy(ModelStore::kSaveDelay * 2);
  task_environment()->RunUntilIdle();

  EXPECT_FALSE(base::PathExists(model_path));
}

// The positive control for the test above: the regular profile does persist,
// to that same path, which is precisely why incognito must not.
TEST_F(ArciumProfileStateTest, ARegularProfileHasAStoreAndWrites) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContext(profile());
  ASSERT_TRUE(state);
  ASSERT_TRUE(state->store());

  state->model()->AddEntry(EntryKind::kPinned, GURL("https://a.example/"),
                           u"A");
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

  incognito->model()->AddEntry(EntryKind::kPinned,
                               GURL("https://secret.example/"), u"Secret");
  EXPECT_EQ(1u, incognito->model()->entries().size());
  EXPECT_TRUE(regular->model()->entries().empty());

  task_environment()->FastForwardBy(ModelStore::kSaveDelay * 2);
  task_environment()->RunUntilIdle();
}

TEST_F(ArciumProfileStateTest, TheSameStateComesBackForTheSameContext) {
  EXPECT_EQ(ArciumProfileState::GetForBrowserContext(profile()),
            ArciumProfileState::GetForBrowserContext(profile()));
}

}  // namespace
}  // namespace arcium
