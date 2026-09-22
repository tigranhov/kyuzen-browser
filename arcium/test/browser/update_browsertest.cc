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
#include "arcium/browser/update/update_preference.h"
#include "arcium/browser/update/update_status.h"
#include "arcium/common/product_version.h"
#include "arcium/test/fake_update_status.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/help/version_updater.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
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

// Driven through the settings page itself rather than the preference, because
// what is claimed is that a reader can reach the setting: the row has to be
// in the page, the page has to be allowed to write a setting the whole
// browser shares, and the value has to land where the updater reads it.
IN_PROC_BROWSER_TEST_F(UpdateTest, TheAboutPageCreditsTheChromiumProject) {
  // Renaming the product renamed the project it is built on with it, so the
  // page thanked Kyuzen for Kyuzen. The sentence is about this browser; the
  // link in it is about Chromium.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL("chrome://settings/help")));
  const std::string credit =
      content::EvalJs(web_contents(),
                      "import('chrome://resources/js/load_time_data.js')"
                      ".then(m => m.loadTimeData.getString("
                      "'aboutProductLicense'))")
          .ExtractString();
  EXPECT_NE(std::string::npos, credit.find(">Chromium</a>")) << credit;
}

IN_PROC_BROWSER_TEST_F(UpdateTest, TheSettingCanBeChangedFromTheSettingsPage) {
  ASSERT_EQ(UpdateMode::kAsk, GetUpdateMode(g_browser_process->local_state()));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://settings/help")));

  static constexpr char kFindRow[] = R"(
    (async () => {
      const ui = document.querySelector('settings-ui');
      const main = ui.shadowRoot.querySelector('settings-main');
      const about = main.shadowRoot.querySelector('settings-about-page');
      await about.updateComplete;
      const row = about.shadowRoot.querySelector('#kyuzenUpdateMode');
      if (!row) {
        return 'no row';
      }
      await row.updateComplete;
      const select = row.shadowRoot.querySelector('select');
      // The last option is the element's own disabled "Custom" entry.
      const ours = [...select.options].map(o => o.value)
                       .filter(v => /^[0-9]+$/.test(v));
      return ours.join(',') + ' showing ' + select.value;
    })()
  )";
  EXPECT_EQ("0,1,2 showing 0", content::EvalJs(web_contents(), kFindRow))
      << "three choices, and a new reader is on the first of them";

  static constexpr char kChooseNeverCheck[] = R"(
    (async () => {
      const ui = document.querySelector('settings-ui');
      const main = ui.shadowRoot.querySelector('settings-main');
      const about = main.shadowRoot.querySelector('settings-about-page');
      const row = about.shadowRoot.querySelector('#kyuzenUpdateMode');
      const select = row.shadowRoot.querySelector('select');
      select.value = '2';
      select.dispatchEvent(new Event('change'));
      return true;
    })()
  )";
  ASSERT_EQ(true, content::EvalJs(web_contents(), kChooseNeverCheck));

  // The page writes through an asynchronous call, so wait for the value.
  while (GetUpdateMode(g_browser_process->local_state()) != UpdateMode::kOff) {
    base::RunLoop().RunUntilIdle();
  }
  EXPECT_EQ(UpdateMode::kOff, GetUpdateMode(g_browser_process->local_state()));
}

}  // namespace
}  // namespace arcium::test
