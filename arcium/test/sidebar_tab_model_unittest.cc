// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/sidebar_tab_model.h"

#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class CountingObserver : public SidebarModel::Observer {
 public:
  void OnSidebarModelChanged() override { ++count; }
  int count = 0;
};

class SidebarTabModelTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }
};

TEST_F(SidebarTabModelTest, RowsFollowTabOrderAndSections) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  // AddTab inserts at index 0 and activates, so order is c, b, a.
  strip()->SetTabPinned(0, true);

  SidebarTabModel model(strip());
  std::vector<SidebarRow> rows = model.rows();
  ASSERT_EQ(3u, rows.size());
  EXPECT_EQ(SidebarSection::kPinned, rows[0].section);
  EXPECT_EQ(GURL("https://c.example/"), rows[0].url);
  EXPECT_EQ(SidebarSection::kToday, rows[1].section);
  EXPECT_EQ(GURL("https://b.example/"), rows[1].url);
  EXPECT_EQ(SidebarSection::kToday, rows[2].section);
  EXPECT_TRUE(rows[0].is_active);
  EXPECT_FALSE(rows[1].is_active);
}

TEST_F(SidebarTabModelTest, CommandsDriveTheStrip) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  SidebarTabModel model(strip());

  model.ActivateTab(1);
  EXPECT_EQ(1, strip()->active_index());

  model.MoveTab(1, 0);
  EXPECT_EQ(GURL("https://a.example/"), strip()->GetWebContentsAt(0)->GetURL());

  model.CloseTab(0);
  EXPECT_EQ(1, strip()->count());
}

TEST_F(SidebarTabModelTest, ClearTodayKeepsPinnedTabs) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  strip()->SetTabPinned(0, true);
  SidebarTabModel model(strip());

  model.ClearToday();
  ASSERT_EQ(1, strip()->count());
  EXPECT_TRUE(strip()->IsTabPinned(0));
}

TEST_F(SidebarTabModelTest, ObserverFiresOncePerBurst) {
  AddTab(browser(), GURL("https://a.example/"));
  task_environment()->RunUntilIdle();
  SidebarTabModel model(strip());
  CountingObserver observer;
  model.AddObserver(&observer);

  // One AddTab produces several strip callbacks (insert, title, loading
  // state); the model must deliver exactly one notification for the burst.
  AddTab(browser(), GURL("https://b.example/"));
  EXPECT_EQ(0, observer.count);  // Nothing until the task runs.
  task_environment()->RunUntilIdle();
  EXPECT_EQ(1, observer.count);

  strip()->SetTabPinned(0, true);
  task_environment()->RunUntilIdle();
  EXPECT_EQ(2, observer.count);

  model.RemoveObserver(&observer);
  AddTab(browser(), GURL("https://c.example/"));
  task_environment()->RunUntilIdle();
  EXPECT_EQ(2, observer.count);
}

}  // namespace
}  // namespace arcium
