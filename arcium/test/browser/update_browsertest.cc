// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The About page reads Kyuzen's updater and names Kyuzen's version. Both
// tests go through Chromium's own entry points -- VersionUpdater::Create and
// the settings page itself -- rather than the functions patches 0260 and 0262
// call, so that each fails if its hook is not applied.

#include <memory>
#include <string>

#include "arcium/browser/update/browser_update_status.h"
#include "arcium/browser/update/update_status.h"
#include "arcium/common/product_version.h"
#include "arcium/test/fake_update_status.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/help/version_updater.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium::test {
namespace {

class UpdateTest : public InProcessBrowserTest {
 protected:
  void TearDownOnMainThread() override {
    SetBrowserUpdateStatus(nullptr);
    InProcessBrowserTest::TearDownOnMainThread();
  }

  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  // Asks the About page's updater, as the page does when it opens, and
  // returns the last thing it said.
  VersionUpdater::Status AskAsTheAboutPageDoes() {
    std::unique_ptr<VersionUpdater> updater =
        VersionUpdater::Create(web_contents());
    VersionUpdater::Status last = VersionUpdater::FAILED_OFFLINE;
    updater->CheckForUpdate(
        base::BindRepeating(
            [](VersionUpdater::Status* out, VersionUpdater::Status status, int,
               bool, bool, const std::string&, int64_t,
               const std::u16string&) { *out = status; },
            &last),
        base::DoNothing());
    return last;
  }

  FakeUpdateStatus fake_;
};

IN_PROC_BROWSER_TEST_F(UpdateTest, TheAboutPageReportsWhatTheUpdaterKnows) {
  SetBrowserUpdateStatus(&fake_);
  fake_.SetState(UpdateStatus::State::kUpdateAvailable);

  EXPECT_EQ(VersionUpdater::UPDATING, AskAsTheAboutPageDoes());
  EXPECT_EQ(1, fake_.checks()) << "opening the page is a check by hand";
}

IN_PROC_BROWSER_TEST_F(UpdateTest, WithoutAnUpdaterThePageHidesItsUpdateLine) {
  // A test binary carries no Sparkle, like every build but a release, and the
  // page must not go looking for Google's updater instead.
  EXPECT_EQ(VersionUpdater::DISABLED, AskAsTheAboutPageDoes());
}

IN_PROC_BROWSER_TEST_F(UpdateTest, TheAboutPageNamesKyuzensVersion) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://settings/help")));
  const std::string shown =
      content::EvalJs(web_contents(),
                      "import('chrome://resources/js/load_time_data.js')"
                      ".then(m => m.loadTimeData.getString("
                      "'aboutBrowserVersion'))")
          .ExtractString();
  EXPECT_NE(std::string::npos, shown.find(std::string(ProductVersion())))
      << shown;
}

}  // namespace
}  // namespace arcium::test
