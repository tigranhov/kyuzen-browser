// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/clear_data_warning.h"

#include <memory>
#include <optional>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "base/functional/bind.h"
#include "base/test/bind.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class ClearDataWarningTest : public BrowserWithTestWindowTest {
 protected:
  void TearDown() override {
    SetClearDataWarningAnswerForTesting(std::nullopt);
    settings_page_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

  // A bare WebContents standing in for the tab showing Clear browsing data.
  // AskWhichProfilesToClear only ever reads its browser context and takes a
  // weak pointer to it, so it does not need chrome://settings to actually
  // load -- and a real navigation there would construct SettingsUI's own
  // handlers (SearchEnginesHandler's KeywordEditorController among them),
  // which this bare test profile has none of the keyed services for.
  content::WebContents* settings_page() {
    if (!settings_page_) {
      settings_page_ =
          content::WebContentsTester::CreateTestWebContents(profile(), nullptr);
    }
    return settings_page_.get();
  }

 private:
  std::unique_ptr<content::WebContents> settings_page_;
};

// Nothing to warn about while Default is the only profile: the removal
// runs unchanged, with no dialog in the way.
TEST_F(ClearDataWarningTest, OneProfileMeansNoWarning) {
  // Built explicitly, with no extra profile added, so the check below is
  // the one this test means to exercise (Default alone) rather than the
  // state not existing at all -- settings_page() no longer inserts a tab
  // into a real strip (see its comment), so nothing else would build it.
  ArciumProfileState::GetForBrowserContext(profile());
  EXPECT_FALSE(AskWhichProfilesToClear(settings_page(), base::DoNothing()));
}

TEST_F(ClearDataWarningTest, TheRemovalWaitsForTheAnswerAndThenRuns) {
  ArciumProfileState::GetForBrowserContext(profile())->model()->AddProfile(
      u"Work", 1);
  SetClearDataWarningAnswerForTesting(false);
  bool resumed = false;
  EXPECT_TRUE(AskWhichProfilesToClear(
      settings_page(), base::BindLambdaForTesting([&] { resumed = true; })));
  EXPECT_TRUE(resumed);
}

// The answer is spent once: Chrome's handler is called again to do the
// removal, and that call must go through rather than ask a second time.
TEST_F(ClearDataWarningTest, TheSecondCallIsTheRemovalItselfAndIsNotAsked) {
  ArciumProfileState::GetForBrowserContext(profile())->model()->AddProfile(
      u"Work", 1);
  SetClearDataWarningAnswerForTesting(true);
  content::WebContents* page = settings_page();
  bool resumed = false;
  ASSERT_TRUE(AskWhichProfilesToClear(page, base::BindLambdaForTesting([&] {
                                        resumed = true;
                                        EXPECT_FALSE(AskWhichProfilesToClear(
                                            page, base::DoNothing()));
                                      })));
  EXPECT_TRUE(resumed);
  // And asked again afterwards, because a later clear is a new question.
  EXPECT_TRUE(AskWhichProfilesToClear(page, base::DoNothing()));
}

}  // namespace
}  // namespace arcium
