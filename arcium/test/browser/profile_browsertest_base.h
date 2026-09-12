// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_TEST_BROWSER_PROFILE_BROWSERTEST_BASE_H_
#define ARCIUM_TEST_BROWSER_PROFILE_BROWSERTEST_BASE_H_

#include <string>
#include <string_view>

#include "arcium/browser/model/entry_id.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "url/gurl.h"

class TabStripModel;

namespace content {
class WebContents;
}

namespace arcium {

class ArciumModel;
class ArciumProfileState;
class SpaceSwitcher;

namespace test {

// A real browser with the sidebar, a test server answering for every host,
// and a model that has finished loading. Everything about profiles that
// cannot be seen without real storage is tested through this: a partition
// that leaks shows nothing on screen, only a cookie in the wrong place.
class ProfileBrowserTest : public InProcessBrowserTest {
 public:
  ProfileBrowserTest();
  ~ProfileBrowserTest() override;

  void SetUpOnMainThread() override;

 protected:
  ArciumProfileState* state();
  ArciumModel* model();
  SpaceSwitcher* switcher();
  TabStripModel* strip();
  content::WebContents* active();

  // Adds a profile and a space on it, and switches the window there.
  SpaceId AddSpaceOnNewProfile(const std::u16string& name,
                               ProfileId* profile_out);

  // The one test page, served for every host under /arcium/: a link that
  // opens in this tab and one that opens in a new tab with no opener,
  // which is what target=_blank means.
  GURL PageUrl(std::string_view host, std::string_view query);

  // The storage `contents` is using: "" for the default partition.
  std::string PartitionOf(content::WebContents* contents);

  // Sets and reads document.cookie in the page `contents` shows. A session
  // cookie is one with no lifetime, which Chromium keeps only in memory
  // unless the profile restores its session.
  void SetCookie(content::WebContents* contents,
                 std::string_view value,
                 bool session_only = false);
  std::string ReadCookie(content::WebContents* contents);

  // The first tab of this window showing `url`, or null.
  content::WebContents* FindTab(const GURL& url);

  // For a PRE_ test: makes the next launch restore this session, and gives
  // the space tags, the profile ids and the model their chance to reach
  // disk before the browser goes.
  void RestoreSessionAtNextLaunch();
  void FlushSessionAndModel();
};

}  // namespace test
}  // namespace arcium

#endif  // ARCIUM_TEST_BROWSER_PROFILE_BROWSERTEST_BASE_H_
