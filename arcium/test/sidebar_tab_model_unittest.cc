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
#include "chrome/browser/ui/tabs/tab_enums.h"
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

// R2.4 extended: a Today tab can be named, and the name is not persisted --
// it dies with the tab, because a Today tab is transient and nothing carries
// a name past its life.
TEST_F(SidebarTabModelTest, ATodayTabTakesACustomName) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  ASSERT_EQ(1u, model->rows().size());
  ASSERT_FALSE(model->rows()[0].entry_id.is_valid()) << "must be a Today row";

  model->SetTabTitle(0, GURL("https://a.example/"), u"Reading later");

  EXPECT_EQ(u"Reading later", model->rows()[0].title);
}

// The name follows the tab, not the page: navigating away keeps it, which is
// what makes it useful for a tab you are living in.
TEST_F(SidebarTabModelTest, ACustomTodayNameSurvivesNavigation) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->SetTabTitle(0, GURL("https://a.example/"), u"Reading later");

  NavigateAndCommitActiveTab(GURL("https://a.example/deeper"));

  ASSERT_EQ(1u, model->rows().size());
  EXPECT_EQ(u"Reading later", model->rows()[0].title);
}

// An empty name is a command, not a no-op: it clears the custom name and the
// row goes back to following the page. Without it a Today rename could not be
// undone -- the revert button belongs to pinned URLs.
TEST_F(SidebarTabModelTest, AnEmptyNameClearsACustomTodayName) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  const std::u16string page_title = model->rows()[0].title;
  model->SetTabTitle(0, GURL("https://a.example/"), u"Reading later");
  ASSERT_EQ(u"Reading later", model->rows()[0].title);

  model->SetTabTitle(0, GURL("https://a.example/"), u"");

  // The page's own title exactly, not merely "not the custom one": storing
  // the empty string instead of clearing would also satisfy that.
  EXPECT_EQ(page_title, model->rows()[0].title);
  EXPECT_EQ(0u, model->today_title_count_for_testing());
}

// The index is only true at the instant it is read. The archive service
// closes idle Today tabs without the user touching anything, so the slot a
// posted rename names may hold a different page by the time it runs.
TEST_F(SidebarTabModelTest, ARenameIsDroppedWhenTheSlotHoldsAnotherPage) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  const std::u16string before = model->rows()[0].title;

  model->SetTabTitle(0, GURL("https://somewhere-else.example/"), u"Wrong row");

  EXPECT_EQ(before, model->rows()[0].title)
      << "a rename must not land on the page that took the slot";
}

// The map is this window's alone, so a tab leaving the strip must drop its
// name here -- otherwise the entry is unreachable and never cleared.
TEST_F(SidebarTabModelTest, ClosingANamedTabDropsItsName) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  // AddTab inserts at 0, so index 1 is a.example.
  model->SetTabTitle(1, GURL("https://a.example/"), u"Named");
  ASSERT_EQ(u"Named", model->rows()[1].title);

  ASSERT_EQ(1u, model->today_title_count_for_testing());

  strip()->CloseWebContentsAt(1, TabCloseTypes::CLOSE_NONE);

  // The map, not the rows: the closed tab draws nothing either way, so a row
  // assertion here passes whether or not the entry was dropped. A handle is
  // never recycled, so the leak is invisible except by counting.
  EXPECT_EQ(0u, model->today_title_count_for_testing())
      << "the name outlived the tab it belonged to";
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
  arcium_model_.AddEntryForTesting(EntryKind::kPinned,
                                   GURL("https://cold.example/"), u"Cold");
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
  const EntryId id = arcium_model_.AddEntryForTesting(
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
  const EntryId id = arcium_model_.AddEntryForTesting(
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

// The same promise, downward: the entry's tab sits above the gap it was
// dropped in, so lifting it out of the strip shifts that gap up one. The
// position-0 case above never exercises that, which is why deleting the
// correction from MoveTabBeforeStripIndex failed no test.
TEST_F(SidebarTabModelTest, MovingAnEntryDownIntoTodayPutsItsTabInThatGap) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  // The strip is c, b, a; pinning c leaves Today holding b then a, and the
  // entry's tab at strip index 0 — above both of them.
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;
  ASSERT_TRUE(id.is_valid());

  // Between b and a, which is Today position 1.
  model->MoveEntryToSection(id, SidebarSection::kToday, /*position=*/1);

  EXPECT_EQ(GURL("https://b.example/"),
            strip()->GetWebContentsAt(0)->GetVisibleURL());
  EXPECT_EQ(GURL("https://c.example/"),
            strip()->GetWebContentsAt(1)->GetVisibleURL());
  EXPECT_EQ(GURL("https://a.example/"),
            strip()->GetWebContentsAt(2)->GetVisibleURL());
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
  const EntryId id = arcium_model_.AddEntryForTesting(
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
  const EntryId id = arcium_model_.AddEntryForTesting(
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

// The close-out obligation deferred from Task 13. Entries carry a space id and
// rows() emits only the default space's, but IsClaimedByEntry asked the model
// for the entry without caring which space it was in. A tab bound to an entry
// of another space was therefore claimed — kept out of Today — while nothing
// drew it either: an invisible tab, and one MayArchive would refuse to close
// for as long as the browser ran. The same shape as the stale-binding bug the
// predicate was written for, one field further along.
//
// Disabled by Task 3: IsClaimedByEntry lost its space clause, so a tab bound
// to an entry of another space is now claimed, which is the opposite of what
// this test checks. Task 5 replaces it with a test of the new rule.
TEST_F(SidebarTabModelTest,
       DISABLED_ATabClaimedByAnotherSpacesEntryIsStillATodayTab) {
  AddTab(browser(), GURL("https://a.example/"));
  // Two spaces, the entry in the second. Through ReplaceAll because that is
  // the only way a second space exists in Stage 2 — it is what ModelStore
  // hands a model read off disk, which is where a Stage 3 file would arrive
  // from.
  Space first;
  first.id = SpaceId::Generate();
  first.name = u"First";
  Space other;
  other.id = SpaceId::Generate();
  other.name = u"Other";
  TabEntry entry;
  entry.id = EntryId::Generate();
  entry.kind = EntryKind::kPinned;
  entry.space_id = other.id;
  entry.url = GURL("https://a.example/");
  arcium_model_.ReplaceAll({first, other}, {}, {entry});
  ASSERT_EQ(first.id, arcium_model_.default_space_id());
  binding_.Bind(entry.id, strip()->GetTabAtIndex(0)->GetHandle());

  std::unique_ptr<SidebarTabModel> model = MakeModel();
  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(SidebarSection::kToday, rows[0].section);
  EXPECT_FALSE(rows[0].entry_id.is_valid());

  // And it behaves as one: Clear takes it, rather than leaving it behind on
  // the strength of an entry no window in this space can see.
  model->ClearToday();
  EXPECT_EQ(0, strip()->count());
}

// I4, the other half: an entry survives its tab being dragged from one window
// to another. The strip reports that as a removal like any other, and the only
// thing that tells them apart is the reason — kInsertedIntoOtherTabStrip, the
// tab is going somewhere, versus a real close, the tab is going away. Without
// that guard the binding is released on the way out, the entry goes cold in
// both windows, and the row in the window that just received the tab is blank.
// Deleting the guard used to pass the whole suite.
TEST_F(SidebarTabModelTest, AnEntryStaysWarmWhenItsTabMovesToAnotherWindow) {
  AddTab(browser(), GURL("https://stay.example/"));
  AddTab(browser(), GURL("https://pinned.example/"));
  std::unique_ptr<SidebarTabModel> model_a = MakeModel();
  model_a->PinTab(0);
  const EntryId id = model_a->rows()[0].entry_id;
  ASSERT_TRUE(id.is_valid());
  ASSERT_FALSE(model_a->rows()[0].is_cold);

  std::unique_ptr<Browser> browser_b =
      CreateBrowser(profile(), browser()->type(), /*hosted_app=*/false);
  SidebarTabModel model_b(browser_b->tab_strip_model(), &arcium_model_,
                          &binding_);

  // The drag: detached from A for reinsertion, not closed.
  browser_b->tab_strip_model()->InsertDetachedTabAt(
      0, strip()->DetachTabAtForInsertion(0), AddTabTypes::ADD_ACTIVE);
  task_environment()->RunUntilIdle();

  // The entry is still there and still bound, and it is B that now holds it
  // warm — A shows the same entry cold, which is the two-window contract.
  EXPECT_EQ(1u, arcium_model_.entries().size());
  EXPECT_TRUE(binding_.TabForEntry(id).has_value());
  std::vector<SidebarRow> rows_b = model_b.rows();
  ASSERT_FALSE(rows_b.empty());
  EXPECT_EQ(id, rows_b[0].entry_id);
  EXPECT_EQ(SidebarSection::kPinned, rows_b[0].section);
  EXPECT_FALSE(rows_b[0].is_cold);
  EXPECT_EQ(0, rows_b[0].tab_index);

  std::vector<SidebarRow> rows_a = model_a->rows();
  ASSERT_FALSE(rows_a.empty());
  EXPECT_EQ(id, rows_a[0].entry_id);
  EXPECT_TRUE(rows_a[0].is_cold);

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
