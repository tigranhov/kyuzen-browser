// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// What a launch brings back when nobody has touched the startup setting.
// Every other relaunch test switches session restore on for itself, which is
// how a profile that lost its Today tabs at every quit went unnoticed: this
// one sets nothing, so the default is what it tests, and it reads that
// default through Chromium's own registration (patch 0245), not by calling
// Arcium's function.

#include <string>

#include "arcium/test/browser/profile_browsertest_base.h"
#include "base/strings/strcat.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium::test {
namespace {

using StartupRestoreTest = ProfileBrowserTest;

IN_PROC_BROWSER_TEST_F(StartupRestoreTest,
                       PRE_ATodayTabComesBackWithoutAnyoneAskingForIt) {
  EXPECT_EQ(SessionStartupPref::LAST,
            SessionStartupPref::GetStartupPref(browser()->GetProfile()).type);
  EXPECT_TRUE(browser()
                  ->GetProfile()
                  ->GetPrefs()
                  ->FindPreference(prefs::kRestoreOnStartup)
                  ->IsDefaultValue())
      << "the setting was stored rather than defaulted";

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "today"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  FlushSessionAndModel();
}

IN_PROC_BROWSER_TEST_F(StartupRestoreTest,
                       ATodayTabComesBackWithoutAnyoneAskingForIt) {
  // By host and query: the test server takes a new port on every launch. A
  // restored tab stays unloaded until clicked, so the page is read from its
  // navigation entry rather than from a committed load.
  bool found = false;
  for (int i = 0; i < strip()->count(); ++i) {
    const GURL& url = strip()->GetWebContentsAt(i)->GetVisibleURL();
    found =
        found || base::StrCat({url.host(), "?", url.query()}) == "a.test?today";
  }
  EXPECT_TRUE(found) << "the Today tab did not come back";
}

}  // namespace
}  // namespace arcium::test
