// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/sidebar_tab_model.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/test/bind.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/test_browser_window.h"
#include "components/tabs/public/tab_interface.h"
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

// TestBrowserWindow::Activate() is a no-op, so raising a window leaves no
// trace to assert on. Counting the calls is what makes "activating an entry
// held by another window raises that window" a checkable claim.
class CountingBrowserWindow : public TestBrowserWindow {
 public:
  void Activate() override {
    ++activate_count;
    set_is_active(true);
  }
  int activate_count = 0;
};

class SidebarTabModelTest : public BrowserWithTestWindowTest {
 protected:
  // Browser batches a navigation's UI updates and delivers them from a task
  // it posts 200 ms out (kUIUpdateCoalescingTime,
  // chrome/browser/ui/browser.cc), ending in TabStripModel::TabChangedAt(kAll)
  // — one more notification for the sidebar. RunUntilIdle does not run a task
  // that is not due yet, so a test that counts notifications and does not turn
  // this off is counting whatever the wall clock happened to deliver inside its
  // window: the update lands in the window if the tests before it ran slowly
  // and outside it if they ran fast. That is the whole of the
  // AFolderChangeNotifiesOnce flake, and this is upstream's own seam for it —
  // with the delay at zero the same RunUntilIdle that drains everything else
  // drains this too. Only the tests that count need it; the rest are left on
  // stock timing.
  void MakeBrowserUiUpdatesImmediate() {
    browser()->set_update_ui_immediately_for_testing();
  }

  TabStripModel* strip() { return browser()->tab_strip_model(); }

  std::unique_ptr<BrowserWindow> CreateBrowserWindow() override {
    return std::make_unique<CountingBrowserWindow>();
  }

  // The fixture's own window, which is browser()'s.
  CountingBrowserWindow* counting_window() {
    return static_cast<CountingBrowserWindow*>(window());
  }

  std::unique_ptr<SidebarTabModel> MakeModel() {
    return std::make_unique<SidebarTabModel>(strip(), &arcium_model_,
                                             &binding_);
  }

  ArciumModel arcium_model_;
  TabBinding binding_;
};

// Stage 1 checked that Chromium's own pinned state chose the section. Under
// the merged model it no longer does: Arcium's Pinned section is entries, and
// a Chromium-pinned tab that no entry claims is still a Today tab.
TEST_F(SidebarTabModelTest, RowsFollowTabOrderAndSections) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  // AddTab inserts at index 0 and activates, so order is c, b, a.
  strip()->SetTabPinned(0, true);

  std::unique_ptr<SidebarTabModel> model = MakeModel();
  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(3u, rows.size());
  EXPECT_EQ(SidebarSection::kToday, rows[0].section);
  EXPECT_EQ(GURL("https://c.example/"), rows[0].url);
  EXPECT_EQ(SidebarSection::kToday, rows[1].section);
  EXPECT_EQ(GURL("https://b.example/"), rows[1].url);
  EXPECT_EQ(SidebarSection::kToday, rows[2].section);
  EXPECT_TRUE(rows[0].is_active);
  EXPECT_FALSE(rows[1].is_active);
  EXPECT_FALSE(rows[0].entry_id.is_valid());
}

TEST_F(SidebarTabModelTest, CommandsDriveTheStrip) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  model->ActivateTab(1);
  EXPECT_EQ(1, strip()->active_index());

  model->MoveTab(1, 0);
  EXPECT_EQ(GURL("https://a.example/"), strip()->GetWebContentsAt(0)->GetURL());

  model->CloseTab(0);
  EXPECT_EQ(1, strip()->count());
}

// Stage 1 spelled "kept" as Chromium's pinned state; now it is an entry that
// keeps its tab alive through a Clear.
TEST_F(SidebarTabModelTest, ClearTodayKeepsEntryTabs) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);

  model->ClearToday();
  ASSERT_EQ(1, strip()->count());
  ASSERT_EQ(1u, model->rows().size());
  EXPECT_EQ(SidebarSection::kPinned, model->rows()[0].section);
  EXPECT_FALSE(model->rows()[0].is_cold);
}

TEST_F(SidebarTabModelTest, ObserverFiresOncePerBurst) {
  MakeBrowserUiUpdatesImmediate();
  AddTab(browser(), GURL("https://a.example/"));
  task_environment()->RunUntilIdle();
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  CountingObserver observer;
  model->AddObserver(&observer);

  // One AddTab produces several strip callbacks (insert, then title and
  // loading state) and the model collapses each run-loop turn's worth of
  // them into one notification. Chromium then hands over the rest of the
  // insertion — the URL and title Browser batched behind
  // ProcessPendingUIUpdates — from a second task, which is a second turn and
  // so a second notification. Two rebuilds per tab opened, not one, and that
  // is Chromium's shape rather than this model's: stock Chromium puts 200 ms
  // between them, which is what made the old expectation of 1 look right.
  AddTab(browser(), GURL("https://b.example/"));
  EXPECT_EQ(0, observer.count);  // Nothing until the task runs.
  task_environment()->RunUntilIdle();
  EXPECT_EQ(2, observer.count);

  // A pinned-state change is one turn's worth on its own.
  strip()->SetTabPinned(0, true);
  task_environment()->RunUntilIdle();
  EXPECT_EQ(3, observer.count);

  model->RemoveObserver(&observer);
  AddTab(browser(), GURL("https://c.example/"));
  task_environment()->RunUntilIdle();
  EXPECT_EQ(3, observer.count);
}

// An ArciumModel mutation coalesces into the same single notification.
TEST_F(SidebarTabModelTest, AModelMutationAlsoFiresOnce) {
  MakeBrowserUiUpdatesImmediate();
  AddTab(browser(), GURL("https://a.example/"));
  task_environment()->RunUntilIdle();
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  CountingObserver observer;
  model->AddObserver(&observer);

  model->PinTab(0);
  EXPECT_EQ(0, observer.count);
  task_environment()->RunUntilIdle();
  EXPECT_EQ(1, observer.count);
  model->RemoveObserver(&observer);
}

TEST_F(SidebarTabModelTest, AColdEntryAppearsWithNoTab) {
  arcium_model_.AddEntry(EntryKind::kPinned, GURL("https://cold.example/"),
                         u"Cold");
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(1u, rows.size());
  EXPECT_TRUE(rows[0].is_cold);
  EXPECT_EQ(-1, rows[0].tab_index);
  EXPECT_EQ(SidebarSection::kPinned, rows[0].section);
  EXPECT_EQ(u"Cold", rows[0].title);
  EXPECT_EQ(GURL("https://cold.example/"), rows[0].url);
}

TEST_F(SidebarTabModelTest, PinningATabMovesItOutOfToday) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  ASSERT_EQ(SidebarSection::kToday, model->rows()[0].section);

  model->PinTab(0);
  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(SidebarSection::kPinned, rows[0].section);
  EXPECT_FALSE(rows[0].is_cold);
  EXPECT_TRUE(rows[0].entry_id.is_valid());
  // The tab is still the same tab, now bound to an entry.
  EXPECT_EQ(0, rows[0].tab_index);
  EXPECT_EQ(1u, arcium_model_.entries().size());
}

TEST_F(SidebarTabModelTest, AddToFavoritesMovesATabToTheGrid) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->AddToFavorites(0);
  ASSERT_EQ(1u, model->rows().size());
  EXPECT_EQ(SidebarSection::kFavorites, model->rows()[0].section);
}

TEST_F(SidebarTabModelTest, ClosingAPinnedEntrysTabLeavesItCold) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;

  strip()->CloseWebContentsAt(0, 0);
  task_environment()->RunUntilIdle();

  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(id, rows[0].entry_id);
  EXPECT_TRUE(rows[0].is_cold);
}

TEST_F(SidebarTabModelTest, ActivatingAColdEntryOpensItsUrlAndBindsIt) {
  const EntryId id = arcium_model_.AddEntry(
      EntryKind::kPinned, GURL("https://cold.example/"), u"Cold");
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  ASSERT_TRUE(model->rows()[0].is_cold);

  model->ActivateEntry(id);
  task_environment()->RunUntilIdle();

  EXPECT_EQ(1, strip()->count());
  EXPECT_FALSE(model->rows()[0].is_cold);
  EXPECT_TRUE(binding_.TabForEntry(id).has_value());
}

TEST_F(SidebarTabModelTest,
       ActivatingAWarmEntryFocusesItRatherThanOpeningAgain) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(1);
  const EntryId id = model->rows()[0].entry_id;
  const int count_before = strip()->count();

  model->ActivateEntry(id);
  EXPECT_EQ(count_before, strip()->count());
}

TEST_F(SidebarTabModelTest, UnpinningReturnsTheTabToToday) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;

  model->UnpinEntry(id);
  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(SidebarSection::kToday, rows[0].section);
  EXPECT_TRUE(arcium_model_.entries().empty());
}

TEST_F(SidebarTabModelTest, UnpinningAColdEntryJustRemovesIt) {
  const EntryId id = arcium_model_.AddEntry(
      EntryKind::kPinned, GURL("https://cold.example/"), u"Cold");
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->UnpinEntry(id);
  EXPECT_TRUE(model->rows().empty());
}

// What a drop between sections issues. One command rather than a kind change
// followed by a reorder, so no observer can see the entry half-moved.
TEST_F(SidebarTabModelTest, MoveEntryToSectionChangesKindAndPosition) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  // One entry per tab: AddEntryForTab binds the tab it is given, and handing
  // it the same index twice would rebind and leave the first entry cold.
  model->AddToFavorites(0);
  model->PinTab(1);
  model->PinTab(2);
  // Favourites lead, then pinned in position order.
  ASSERT_EQ(3u, model->rows().size());
  const EntryId favourite = model->rows()[0].entry_id;
  const EntryId second_pinned = model->rows()[2].entry_id;

  model->MoveEntryToSection(second_pinned, SidebarSection::kFavorites,
                            /*position=*/0);

  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(3u, rows.size());
  EXPECT_EQ(second_pinned, rows[0].entry_id);
  EXPECT_EQ(SidebarSection::kFavorites, rows[0].section);
  EXPECT_EQ(favourite, rows[1].entry_id);
  EXPECT_EQ(SidebarSection::kPinned, rows[2].section);
}

TEST_F(SidebarTabModelTest, MoveEntryToSectionReordersWithinASection) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  model->PinTab(1);
  model->PinTab(2);
  const EntryId last = model->rows()[2].entry_id;

  model->MoveEntryToSection(last, SidebarSection::kPinned, /*position=*/0);
  EXPECT_EQ(last, model->rows()[0].entry_id);
}

// The other direction. `position` is where the entry ends up once it has been
// lifted out, not a gap in the section as it stands, and the two readings
// only differ when the entry moves down — which is what every reorder test in
// Task 8 avoided asking.
TEST_F(SidebarTabModelTest, MoveEntryToSectionReordersDownwardsWithinASection) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  model->PinTab(1);
  model->PinTab(2);
  const EntryId first = model->rows()[0].entry_id;
  const EntryId second = model->rows()[1].entry_id;
  const EntryId third = model->rows()[2].entry_id;

  model->MoveEntryToSection(first, SidebarSection::kPinned, /*position=*/2);

  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(3u, rows.size());
  EXPECT_EQ(second, rows[0].entry_id);
  EXPECT_EQ(third, rows[1].entry_id);
  EXPECT_EQ(first, rows[2].entry_id);
}

// The kind change and the reorder are two ArciumModel writes; the sidebar
// must still see one change, or a rebuild lands on the intermediate state.
TEST_F(SidebarTabModelTest, MoveEntryToSectionNotifiesOnce) {
  MakeBrowserUiUpdatesImmediate();
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  task_environment()->RunUntilIdle();
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  model->PinTab(1);
  task_environment()->RunUntilIdle();
  const EntryId id = model->rows()[1].entry_id;

  CountingObserver observer;
  model->AddObserver(&observer);
  model->MoveEntryToSection(id, SidebarSection::kFavorites, /*position=*/0);
  EXPECT_EQ(0, observer.count);  // Nothing until the posted task runs.
  task_environment()->RunUntilIdle();
  EXPECT_EQ(1, observer.count);
  model->RemoveObserver(&observer);
}

// A drop into Favourites or Pinned drew an insertion indicator before it was
// taken. AddToFavorites and PinTab append, which makes that indicator a lie
// wherever it was not drawn at the end, so a drop issues this instead.
TEST_F(SidebarTabModelTest, MoveTabToSectionPutsTheNewEntryWhereTheDropAsked) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  // AddTab inserts at index 0, so the strip is c, b, a.
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  model->PinTab(1);

  model->MoveTabToSection(2, SidebarSection::kPinned, /*position=*/0);

  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(3u, rows.size());
  EXPECT_EQ(GURL("https://a.example/"), rows[0].url);
  EXPECT_EQ(GURL("https://c.example/"), rows[1].url);
  EXPECT_EQ(GURL("https://b.example/"), rows[2].url);
  for (const SidebarRow& row : rows) {
    EXPECT_EQ(SidebarSection::kPinned, row.section);
  }
}

// And appending is the same command with no position asked for, which is what
// the menu items mean.
TEST_F(SidebarTabModelTest, MoveTabToSectionPastTheEndAppends) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);

  model->MoveTabToSection(1, SidebarSection::kPinned, /*position=*/99);

  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(GURL("https://b.example/"), rows[0].url);
  EXPECT_EQ(GURL("https://a.example/"), rows[1].url);
}

// Today's order *is* the tab-strip order, so the insertion line drawn while
// an entry is dragged into Today is a promise about where its tab goes.
TEST_F(SidebarTabModelTest, MovingAnEntryToTodayPutsItsTabWhereTheLineWas) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  // The strip is c, b, a; pinning b leaves Today holding c then a.
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(1);
  const EntryId id = model->rows()[0].entry_id;
  ASSERT_TRUE(id.is_valid());

  model->MoveEntryToSection(id, SidebarSection::kToday, /*position=*/0);

  // First in the strip, which is first in Today.
  EXPECT_EQ(GURL("https://b.example/"),
            strip()->GetWebContentsAt(0)->GetVisibleURL());
  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(3u, rows.size());
  EXPECT_EQ(GURL("https://b.example/"), rows[0].url);
  EXPECT_EQ(GURL("https://c.example/"), rows[1].url);
  EXPECT_EQ(GURL("https://a.example/"), rows[2].url);
  EXPECT_TRUE(arcium_model_.entries().empty());
}

// Dropping a warm entry into Today drops the entry, not the page: nothing
// claims the tab any more, so it falls back into Today.
TEST_F(SidebarTabModelTest, MovingAWarmEntryToTodayLeavesItsTab) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;
  const int count_before = strip()->count();

  model->MoveEntryToSection(id, SidebarSection::kToday, /*position=*/0);

  EXPECT_EQ(count_before, strip()->count());
  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(SidebarSection::kToday, rows[0].section);
  EXPECT_TRUE(arcium_model_.entries().empty());
}

// A cold entry has no tab to leave behind, so one is opened first. Without
// this the drop would delete the entry and everything it stood for, which is
// what an undo would otherwise have to exist to take back.
TEST_F(SidebarTabModelTest, MovingAColdEntryToTodayOpensItsUrlFirst) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = arcium_model_.AddEntry(
      EntryKind::kPinned, GURL("https://cold.example/"), u"Cold");
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  ASSERT_TRUE(model->rows()[0].is_cold);
  const int count_before = strip()->count();

  model->MoveEntryToSection(id, SidebarSection::kToday, /*position=*/0);
  task_environment()->RunUntilIdle();

  EXPECT_EQ(count_before + 1, strip()->count());
  EXPECT_TRUE(arcium_model_.entries().empty());
  // A tab for the URL the entry stood for, and every row now a live Today
  // row: the entry went, the page did not.
  bool found = false;
  for (int i = 0; i < strip()->count(); ++i) {
    found = found || strip()->GetWebContentsAt(i)->GetVisibleURL() ==
                         GURL("https://cold.example/");
  }
  EXPECT_TRUE(found);
  for (const SidebarRow& row : model->rows()) {
    EXPECT_EQ(SidebarSection::kToday, row.section);
    EXPECT_FALSE(row.is_cold);
  }
}

// The drag that issued this began from a snapshot of rows(), which the model
// can outrun.
TEST_F(SidebarTabModelTest, MoveEntryToSectionIgnoresAnUnknownEntry) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->MoveEntryToSection(EntryId::Generate(), SidebarSection::kFavorites, 0);
  model->MoveEntryToSection(EntryId::Generate(), SidebarSection::kToday, 0);
  EXPECT_EQ(1, strip()->count());
  EXPECT_TRUE(arcium_model_.entries().empty());
}

TEST_F(SidebarTabModelTest, NavigatingAwayOffersAReturnToThePinnedUrl) {
  AddTab(browser(), GURL("https://pinned.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  EXPECT_FALSE(model->rows()[0].can_return_to_pinned_url);

  NavigateAndCommitActiveTab(GURL("https://elsewhere.example/"));
  task_environment()->RunUntilIdle();
  EXPECT_TRUE(model->rows()[0].can_return_to_pinned_url);
}

TEST_F(SidebarTabModelTest, ReturningToThePinnedUrlReusesTheBoundTab) {
  AddTab(browser(), GURL("https://pinned.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;
  NavigateAndCommitActiveTab(GURL("https://elsewhere.example/"));

  model->ReturnToPinnedUrl(id);
  task_environment()->RunUntilIdle();
  EXPECT_EQ(1, strip()->count());
  EXPECT_TRUE(binding_.TabForEntry(id).has_value());
}

TEST_F(SidebarTabModelTest, ClosingAnEntryTabKeepsTheEntry) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;

  model->CloseEntryTab(id);
  task_environment()->RunUntilIdle();
  EXPECT_EQ(0, strip()->count());
  ASSERT_EQ(1u, model->rows().size());
  EXPECT_TRUE(model->rows()[0].is_cold);
  EXPECT_EQ(id, model->rows()[0].entry_id);
}

TEST_F(SidebarTabModelTest, SetEntryTitleWinsOverTheLiveTitle) {
  const EntryId id = arcium_model_.AddEntry(
      EntryKind::kPinned, GURL("https://cold.example/"), u"Cold");
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->SetEntryTitle(id, u"Renamed");
  EXPECT_EQ(u"Renamed", model->rows()[0].title);
}

TEST_F(SidebarTabModelTest, TodayTabsStillFollowStripOrder) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(GURL("https://b.example/"), rows[0].url);
  EXPECT_EQ(GURL("https://a.example/"), rows[1].url);
}

// Arcium's Pinned section is entries, so a Chromium-pinned tab no entry
// claims is an ordinary Today tab and Clear Today closes it.
TEST_F(SidebarTabModelTest, ClearTodayClosesAnUnclaimedChromiumPinnedTab) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  strip()->SetTabPinned(0, true);
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  ASSERT_EQ(2, strip()->count());

  model->ClearToday();
  EXPECT_EQ(0, strip()->count());
}

// I3: ModelStore::Load calls ArciumModel::ReplaceAll once the window is
// already interactive, dropping entries without touching TabBinding. A tab
// left bound to a removed entry must fall back into Today, not vanish into
// neither section.
TEST_F(SidebarTabModelTest, ATabWhoseEntryVanishesFallsBackIntoToday) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  ASSERT_EQ(SidebarSection::kPinned, model->rows()[0].section);

  // Exactly what a completed load does to the model.
  std::vector<Space> spaces = arcium_model_.spaces();
  arcium_model_.ReplaceAll(std::move(spaces), {}, {});

  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(SidebarSection::kToday, rows[0].section);
  EXPECT_EQ(0, rows[0].tab_index);
  EXPECT_FALSE(rows[0].entry_id.is_valid());

  // And it is closeable again, which it was not while it was claimed by an
  // entry that no longer existed.
  model->ClearToday();
  EXPECT_EQ(0, strip()->count());
}

// I4: one model and one binding per profile, two windows over them. The
// entry shows in both, warm where its tab lives and cold in the other.
TEST_F(SidebarTabModelTest, TwoWindowsOverOneModelBothShowTheEntry) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model_a = MakeModel();
  model_a->PinTab(0);
  const EntryId id = model_a->rows()[0].entry_id;

  std::unique_ptr<Browser> browser_b =
      CreateBrowser(profile(), browser()->type(), /*hosted_app=*/false);
  SidebarTabModel model_b(browser_b->tab_strip_model(), &arcium_model_,
                          &binding_);

  std::vector<SidebarRow> rows_a = model_a->rows();
  ASSERT_EQ(1u, rows_a.size());
  EXPECT_EQ(id, rows_a[0].entry_id);
  EXPECT_FALSE(rows_a[0].is_cold);

  std::vector<SidebarRow> rows_b = model_b.rows();
  ASSERT_EQ(1u, rows_b.size());
  EXPECT_EQ(id, rows_b[0].entry_id);
  EXPECT_EQ(SidebarSection::kPinned, rows_b[0].section);
  EXPECT_TRUE(rows_b[0].is_cold);

  browser_b->tab_strip_model()->CloseAllTabs();
}

// I2: activating an entry from the window that does not hold its tab must
// raise the window that does, not open a second tab and steal the entry.
TEST_F(SidebarTabModelTest, ActivatingAnEntryHeldByAnotherWindowDoesNotSteal) {
  AddTab(browser(), GURL("https://other.example/"));
  AddTab(browser(), GURL("https://pinned.example/"));
  std::unique_ptr<SidebarTabModel> model_a = MakeModel();
  model_a->PinTab(0);
  const EntryId id = model_a->rows()[0].entry_id;
  const tabs::TabHandle pinned_tab = strip()->GetTabAtIndex(0)->GetHandle();
  strip()->ActivateTabAt(1);
  ASSERT_EQ(1, strip()->active_index());
  ASSERT_EQ(0, counting_window()->activate_count);

  std::unique_ptr<Browser> browser_b =
      CreateBrowser(profile(), browser()->type(), /*hosted_app=*/false);
  AddTab(browser_b.get(), GURL("https://b.example/"));
  SidebarTabModel model_b(browser_b->tab_strip_model(), &arcium_model_,
                          &binding_);
  const int b_count_before = browser_b->tab_strip_model()->count();

  model_b.ActivateEntry(id);
  task_environment()->RunUntilIdle();

  // A's tab is now A's active tab, and A's window was raised rather than left
  // behind B's...
  EXPECT_EQ(0, strip()->active_index());
  EXPECT_EQ(2, strip()->count());
  EXPECT_EQ(1, counting_window()->activate_count);
  EXPECT_TRUE(window()->IsActive());
  // ...B gained nothing, and the entry still points at A's tab.
  EXPECT_EQ(b_count_before, browser_b->tab_strip_model()->count());
  ASSERT_TRUE(binding_.TabForEntry(id).has_value());
  EXPECT_EQ(pinned_tab, binding_.TabForEntry(id).value());

  browser_b->tab_strip_model()->CloseAllTabs();
}

// M7: the affordance must not offer to "return" to a URL the tab is on.
TEST_F(SidebarTabModelTest, AnHttpsUpgradeIsNotANavigationAway) {
  AddTab(browser(), GURL("http://pinned.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  ASSERT_FALSE(model->rows()[0].can_return_to_pinned_url);

  NavigateAndCommitActiveTab(GURL("https://pinned.example/"));
  task_environment()->RunUntilIdle();
  EXPECT_FALSE(model->rows()[0].can_return_to_pinned_url);
}

TEST_F(SidebarTabModelTest, ATrailingSlashRedirectIsNotANavigationAway) {
  AddTab(browser(), GURL("https://pinned.example/docs"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  ASSERT_FALSE(model->rows()[0].can_return_to_pinned_url);

  NavigateAndCommitActiveTab(GURL("https://pinned.example/docs/"));
  task_environment()->RunUntilIdle();
  EXPECT_FALSE(model->rows()[0].can_return_to_pinned_url);
}

TEST_F(SidebarTabModelTest, AFragmentIsNotANavigationAway) {
  AddTab(browser(), GURL("https://pinned.example/docs"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);

  NavigateAndCommitActiveTab(GURL("https://pinned.example/docs#section"));
  task_environment()->RunUntilIdle();
  EXPECT_FALSE(model->rows()[0].can_return_to_pinned_url);
}

// The control for the three above: a real navigation still sets the flag.
// A differing query is a different page, not a redirect shape.
TEST_F(SidebarTabModelTest, ADifferentQueryStillOffersTheReturn) {
  AddTab(browser(), GURL("https://pinned.example/docs"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);

  NavigateAndCommitActiveTab(GURL("https://pinned.example/docs?page=2"));
  task_environment()->RunUntilIdle();
  EXPECT_TRUE(model->rows()[0].can_return_to_pinned_url);
}

// A window with no ArchiveService is what an off-the-record window is:
// BrowserSidebarController builds one only when the profile has an archive.
// The sidebar must then show no archive affordance at all.
TEST_F(SidebarTabModelTest, WithoutAServiceThereIsNoArchive) {
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  EXPECT_FALSE(model->has_archive());
}

// The interface promises the answer never arrives inline, in every branch —
// including this one, where there is nothing to read and the temptation to
// answer on the spot is greatest. A caller with two possible orders of events
// is a caller that gets one of them wrong.
TEST_F(SidebarTabModelTest, AnArchiveRequestIsNeverAnsweredInline) {
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  std::optional<std::vector<ArchivedRow>> got;
  std::optional<bool> readable;
  model->RequestArchivedRows(
      10, base::BindLambdaForTesting(
              [&got, &readable](std::vector<ArchivedRow> rows, bool ok) {
                got = std::move(rows);
                readable = ok;
              }));

  EXPECT_FALSE(got.has_value());
  task_environment()->RunUntilIdle();
  ASSERT_TRUE(got.has_value());
  EXPECT_TRUE(got->empty());
  // No service means no archive to be empty, so this empty answer must not
  // read as "nothing archived yet" either.
  ASSERT_TRUE(readable.has_value());
  EXPECT_FALSE(*readable);
}

// The reply is bound through the model's WeakPtr, so a read still in flight
// when the window closes is dropped rather than delivered into a dead model.
// This is the no-service branch; the posted-to-another-sequence branch is
// AnArchiveReadCrossingSequencesIsDroppedWhenTheModelGoes in
// archive_service_unittest.cc.
TEST_F(SidebarTabModelTest, AnArchiveRequestOutlivedByItsModelIsDropped) {
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  bool ran = false;
  model->RequestArchivedRows(
      10, base::BindLambdaForTesting(
              [&ran](std::vector<ArchivedRow> rows, bool ok) { ran = true; }));
  model.reset();

  task_environment()->RunUntilIdle();
  EXPECT_FALSE(ran);
}

}  // namespace
}  // namespace arcium
