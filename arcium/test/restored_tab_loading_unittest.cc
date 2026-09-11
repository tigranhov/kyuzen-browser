// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/restored_tab_loading.h"

#include <memory>
#include <vector>

#include "arcium/common/arcium_features.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/restore_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

// content::RenderViewHostTestHarness's plain TestBrowserContext does not
// satisfy this binary's ChromeContentBrowserClient (linked in through
// //chrome/browser/ui): it DCHECKs that the BrowserContext is a Profile
// (chrome/browser/profiles/profile.cc). ChromeRenderViewHostTestHarness --
// what session_restore_delegate_unittest.cc itself uses -- supplies a
// TestingProfile instead and is otherwise a drop-in RenderViewHostTestHarness.
class RestoredTabLoadingTest : public ChromeRenderViewHostTestHarness {
 protected:
  // A tab as session restore leaves it: its history is there, and nothing
  // has loaded it. The same construction session_restore_delegate_unittest.cc
  // uses.
  std::unique_ptr<content::WebContents> RestoredTab(const GURL& url) {
    std::unique_ptr<content::WebContents> contents =
        content::WebContentsTester::CreateTestWebContents(browser_context(),
                                                          nullptr);
    std::vector<std::unique_ptr<content::NavigationEntry>> entries;
    entries.push_back(content::NavigationEntry::Create());
    entries.back()->SetURL(url);
    contents->GetController().Restore(0, content::RestoreType::kRestored,
                                      &entries);
    return contents;
  }

  // A favicon request that only records who it was asked for.
  FaviconRequest Recorder(std::vector<content::WebContents*>* asked) {
    return base::BindLambdaForTesting([asked](content::WebContents* contents) {
      asked->push_back(contents);
    });
  }
};

TEST_F(RestoredTabLoadingTest, EveryTabIsAskedForItsFaviconAndNoneLoads) {
  std::unique_ptr<content::WebContents> a =
      RestoredTab(GURL("https://a.example/"));
  std::unique_ptr<content::WebContents> b =
      RestoredTab(GURL("https://b.example/"));
  std::vector<content::WebContents*> asked;

  EXPECT_TRUE(DeferRestoredTabLoads({a.get(), b.get()}, Recorder(&asked)));

  EXPECT_EQ((std::vector<content::WebContents*>{a.get(), b.get()}), asked);
  EXPECT_TRUE(a->GetController().NeedsReload());
  EXPECT_TRUE(b->GetController().NeedsReload());
}

// The comparison run: Chromium's own background loader gets the tabs back.
TEST_F(RestoredTabLoadingTest, WithTheRuleSwitchedOffNothingIsTaken) {
  base::test::ScopedFeatureList off;
  off.InitAndDisableFeature(features::kArciumNoLoadAtLaunch);
  std::unique_ptr<content::WebContents> a =
      RestoredTab(GURL("https://a.example/"));
  std::vector<content::WebContents*> asked;

  EXPECT_FALSE(DeferRestoredTabLoads({a.get()}, Recorder(&asked)));
  EXPECT_TRUE(asked.empty());
}

// A window without the sidebar is stock Chromium, restore included.
TEST_F(RestoredTabLoadingTest, WithTheSidebarSwitchedOffNothingIsTaken) {
  base::test::ScopedFeatureList off;
  off.InitAndDisableFeature(features::kArciumSidebar);
  std::unique_ptr<content::WebContents> a =
      RestoredTab(GURL("https://a.example/"));
  std::vector<content::WebContents*> asked;

  EXPECT_FALSE(DeferRestoredTabLoads({a.get()}, Recorder(&asked)));
  EXPECT_TRUE(asked.empty());
}

// The overload the hook calls asks the tab's favicon driver, which a test
// WebContents does not have. It must take the tabs anyway, not trip on the
// missing driver.
TEST_F(RestoredTabLoadingTest, TheHooksCallTakesTabsWithNoFaviconDriver) {
  std::unique_ptr<content::WebContents> a =
      RestoredTab(GURL("https://a.example/"));
  EXPECT_TRUE(DeferRestoredTabLoads({a.get()}));
  EXPECT_TRUE(a->GetController().NeedsReload());
}

TEST_F(RestoredTabLoadingTest, ARestoredTabIsUnloadedUntilItStartsLoading) {
  std::unique_ptr<content::WebContents> a =
      RestoredTab(GURL("https://a.example/"));
  EXPECT_TRUE(IsTabUnloaded(a.get()));

  a->GetController().LoadIfNecessary();
  EXPECT_FALSE(IsTabUnloaded(a.get()));
}

// Chromium marks a discarded tab with WasDiscarded() and deliberately not
// with NeedsReload() (tab_lifecycle_unit.cc:246), so only reading both finds
// it.
TEST_F(RestoredTabLoadingTest, ADiscardedTabIsUnloadedAndALoadedOneIsNot) {
  std::unique_ptr<content::WebContents> loaded =
      content::WebContentsTester::CreateTestWebContents(browser_context(),
                                                        nullptr);
  content::WebContentsTester::For(loaded.get())
      ->NavigateAndCommit(GURL("https://loaded.example/"));
  EXPECT_FALSE(IsTabUnloaded(loaded.get()));

  loaded->SetWasDiscarded(true);
  EXPECT_TRUE(IsTabUnloaded(loaded.get()));
}

}  // namespace
}  // namespace arcium
