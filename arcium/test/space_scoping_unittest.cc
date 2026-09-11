// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Every surface a window draws — the sidebar, the archive sweep, Clear Today
// — must show only the space it is showing, even though the strip beneath it
// holds every space's tabs. This exercises SidebarTabModel and ArchiveService
// wired to a real SpaceSwitcher, over one strip with two spaces interleaved.

#include <memory>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/test/space_test_util.h"
#include "arcium/ui/browser/archive_service.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/browser/space_switcher.h"
#include "arcium/ui/browser/tab_search_service.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/files/scoped_temp_dir.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/segmentation_platform/public/features.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class CountingObserver : public SidebarModel::Observer {
 public:
  void OnSidebarModelChanged() override { ++count; }
  int count = 0;
};

// Mirrors ArchiveServiceTest's constructor (archive_service_unittest.cc): the
// tests below move the mock clock by hours, and the segmentation platform
// parks a task runner in a process-global object under that clock that the
// next test then CHECKs on. Nothing here is about segmentation; turn it off.
class SpaceScopingTest : public BrowserWithTestWindowTest {
 public:
  SpaceScopingTest()
      : BrowserWithTestWindowTest(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    scoped_feature_list_.InitAndDisableFeature(
        segmentation_platform::features::kSegmentationPlatformFeature);
  }

 protected:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(
        archive_store_.Open(temp_dir_.GetPath().AppendASCII("Archive")));
    switcher_ = std::make_unique<SpaceSwitcher>(strip(), &model_, &binding_);
    sidebar_ = std::make_unique<SidebarTabModel>(strip(), &model_, &binding_,
                                                 switcher_.get());
    // Production order: the strip is empty when both are built, and every
    // tab a test adds arrives afterwards, exactly as BrowserSidebarController
    // builds them inside BrowserView's own constructor.
    ASSERT_EQ(0, strip()->count());
    archive_ = std::make_unique<ArchiveService>(
        strip(), &model_, &binding_, &archive_store_,
        base::SequencedTaskRunner::GetCurrentDefault(), /*clock=*/nullptr,
        switcher_.get());
    sidebar_->SetArchiveService(archive_.get());
    search_ = std::make_unique<TabSearchService>(
        strip(), &model_, &binding_, archive_.get(), switcher_.get());
  }

  void TearDown() override {
    // Both observe the strip and the switcher, which are about to be torn
    // down; the switcher goes last because the services above observe it.
    search_.reset();
    archive_.reset();
    sidebar_.reset();
    switcher_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

  TabStripModel* strip() { return browser()->tab_strip_model(); }

  // Appends a tab already tagged with `space`, mirroring how a restored tab
  // arrives. See arcium/test/space_test_util.h for why this cannot be built
  // on BrowserWithTestWindowTest::AddTab.
  tabs::TabInterface* AddTabInSpace(const GURL& url, SpaceId space) {
    return arcium::test::AddTabInSpace(strip(), profile(), url, space);
  }

  // Same, plus a title a search can match on. Titles matter only for tests
  // that need two results to score exactly the same.
  tabs::TabInterface* AddTabInSpaceWithTitle(const GURL& url,
                                             SpaceId space,
                                             const std::u16string& title) {
    tabs::TabInterface* tab = AddTabInSpace(url, space);
    content::WebContentsTester::For(tab->GetContents())->SetTitle(title);
    return tab;
  }

  // The only entry point a UI caller has, so it is the one the tie-break and
  // cross-space tests drive. RunUntilIdle drains both the posted store read
  // and its reply, mirroring TabSearchServiceTest::Search.
  std::vector<SearchResult> Search(const std::u16string& query, int limit) {
    std::vector<SearchResult> out;
    bool ran = false;
    search_->Search(
        query, limit,
        base::BindLambdaForTesting([&](std::vector<SearchResult> results) {
          out = std::move(results);
          ran = true;
        }));
    task_environment()->RunUntilIdle();
    EXPECT_TRUE(ran) << "the search callback never ran";
    return out;
  }

  base::test::ScopedFeatureList scoped_feature_list_;
  base::ScopedTempDir temp_dir_;
  ArciumModel model_;
  TabBinding binding_;
  ArchiveStore archive_store_;
  std::unique_ptr<SpaceSwitcher> switcher_;
  std::unique_ptr<SidebarTabModel> sidebar_;
  std::unique_ptr<ArchiveService> archive_;
  std::unique_ptr<TabSearchService> search_;
};

TEST_F(SpaceScopingTest, RowsHoldOnlyTheActiveSpace) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://a2.example/"), first);
  AddTabInSpace(GURL("https://w2.example/"), work);

  std::vector<SidebarRow> rows = sidebar_->rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(GURL("https://a1.example/"), rows[0].url);
  EXPECT_EQ(GURL("https://a2.example/"), rows[1].url);

  switcher_->SwitchTo(work);
  rows = sidebar_->rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(GURL("https://w1.example/"), rows[0].url);
  EXPECT_EQ(GURL("https://w2.example/"), rows[1].url);
}

// A pinned entry's tab still physically sits in this window's one strip, but
// it must be drawn only when its own space is on screen: no row here at all
// while `first` is showing, one Pinned row once `work` is.
TEST_F(SpaceScopingTest, APinnedEntryOfAnotherSpaceIsNotDrawnHere) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  const EntryId id = model_.AddEntry(work, EntryKind::kPinned,
                                     GURL("https://w1.example/"), u"W");
  binding_.Bind(id, strip()->GetTabAtIndex(1)->GetHandle());

  ASSERT_EQ(1u, sidebar_->rows().size());
  EXPECT_EQ(GURL("https://a1.example/"), sidebar_->rows().front().url);
  EXPECT_EQ(SidebarSection::kToday, sidebar_->rows().front().section);

  switcher_->SwitchTo(work);
  ASSERT_EQ(1u, sidebar_->rows().size());
  EXPECT_EQ(SidebarSection::kPinned, sidebar_->rows().front().section);
}

// This cannot actually fail: SwitchTo already notifies the sidebar twice over
// (the model's last-active-space change, and the tab activation the switch
// performs), so ArciumModel::Observer or TabStripModelObserver alone would
// already cover it. Kept anyway as a backstop for a switch that changes
// neither the strip nor the model — SpaceSwitcher::Observer is the only thing
// that would notice one.
TEST_F(SpaceScopingTest, TheSidebarRebuildsWhenTheSpaceChanges) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  CountingObserver observer;
  sidebar_->AddObserver(&observer);
  switcher_->SwitchTo(work);
  task_environment()->RunUntilIdle();
  EXPECT_GE(observer.count, 1);
  sidebar_->RemoveObserver(&observer);
}

// Through ArchiveService::ArchiveAllToday, which is the path SidebarTabModel
// takes whenever a window has an archive service — production's only path.
TEST_F(SpaceScopingTest, ClearTodayLeavesOtherSpacesAlone) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  sidebar_->ClearToday();
  task_environment()->RunUntilIdle();
  // a1 was its space's last open tab, so the space's blank tab took its
  // place; w1 is the tab that stayed.
  ASSERT_EQ(2, strip()->count());
  EXPECT_EQ(work, switcher_->SpaceOfTabAt(0));
  EXPECT_EQ(first, switcher_->active_space());
}

// The other path: no archive service, which is SidebarTabModel's own loop —
// the playground and any window over a profile with no archive. Both close
// paths must scope to the window's own space the same way.
TEST_F(SpaceScopingTest, ClearTodayLeavesOtherSpacesAloneWithoutAService) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  sidebar_->SetArchiveService(nullptr);
  sidebar_->ClearToday();
  task_environment()->RunUntilIdle();
  // a1 was its space's last open tab, so the space's blank tab took its
  // place; w1 is the tab that stayed.
  ASSERT_EQ(2, strip()->count());
  EXPECT_EQ(work, switcher_->SpaceOfTabAt(0));
  EXPECT_EQ(first, switcher_->active_space());
}

// D2-2 extended: a background space's landing tab is not archivable, or a
// switch to that space would arrive at a page that had been swept away.
TEST_F(SpaceScopingTest, AnotherSpacesLandingTabIsNeverArchived) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  model_.SetLastActiveTab(work,
                          KeyOf(strip()->GetTabAtIndex(1)->GetContents()));
  EXPECT_FALSE(archive_->MayArchive(strip()->GetTabAtIndex(1)->GetHandle()));
}

// The landing-tab refusal above is scoped to the tab's OWN space, not to
// every space in the model: a tab that used to be a space's landing tab and
// has since moved elsewhere is not held hostage by a memory the space it left
// still carries.
TEST_F(SpaceScopingTest, ATabMovedOutOfASpaceIsNotHeldByThatSpacesLandingRule) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  tabs::TabInterface* w1 = AddTabInSpace(GURL("https://w1.example/"), work);
  model_.SetLastActiveTab(work, KeyOf(w1->GetContents()));

  // w1 leaves `work` for `first` without ever being reactivated — a plain
  // re-tag, exactly like SpaceSwitcher's own adoption rule performs.
  SetSpaceTag(w1->GetContents(), first);

  EXPECT_TRUE(archive_->MayArchive(w1->GetHandle()));
}

TEST_F(SpaceScopingTest, EachSpaceAgesAgainstItsOwnTimeout) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  model_.SetArchiveTimeout(first, ArchiveTimeout::kTwelveHours);
  model_.SetArchiveTimeout(work, ArchiveTimeout::kNever);
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://a2.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);

  // AdvanceClock plus a drain, not FastForwardBy. See
  // ArchiveServiceTest::PassTime (archive_service_unittest.cc): fast-forwarding
  // steps this shared browser-test fixture through every intervening delayed
  // task, most of them nothing to do with this service, and thirteen hours of
  // those never finishes. Jumping the clock and draining what has come due is
  // the same thing for a service with one expiry, and it is instant.
  task_environment()->AdvanceClock(base::Hours(13));
  task_environment()->RunUntilIdle();

  // The first space's idle tab went; the never-archived space's stayed, and
  // so did the first space's active tab.
  ASSERT_EQ(2, strip()->count());
  EXPECT_EQ(GURL("https://a1.example/"),
            strip()->GetWebContentsAt(0)->GetVisibleURL());
  EXPECT_EQ(GURL("https://w1.example/"),
            strip()->GetWebContentsAt(1)->GetVisibleURL());
}

// The swapped case: the timeout that fires belongs to the BACKGROUND space,
// not the one on screen, and the row the sweep writes is filed under that
// same background space rather than whichever space the window happens to be
// showing. A MakeRow that read the window's active space instead of
// SpaceOfTab would file this row under `first` and this test would find
// nothing at `work`.
TEST_F(SpaceScopingTest, EachSpaceAgesAgainstItsOwnTimeoutAndFilesUnderIt) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  model_.SetArchiveTimeout(first, ArchiveTimeout::kNever);
  model_.SetArchiveTimeout(work, ArchiveTimeout::kTwelveHours);
  AddTabInSpace(GURL("https://a1.example/"), first);
  tabs::TabInterface* w1 = AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://w2.example/"), work);
  // w1 is work's landing tab, so it survives the sweep; w2, idle in the same
  // background space, does not.
  model_.SetLastActiveTab(work, KeyOf(w1->GetContents()));

  task_environment()->AdvanceClock(base::Hours(13));
  task_environment()->RunUntilIdle();

  ASSERT_EQ(2, strip()->count());
  EXPECT_EQ(GURL("https://a1.example/"),
            strip()->GetWebContentsAt(0)->GetVisibleURL());
  EXPECT_EQ(GURL("https://w1.example/"),
            strip()->GetWebContentsAt(1)->GetVisibleURL());

  const std::vector<ArchivedTab> recent = archive_store_.ListRecent(work, 10);
  ASSERT_EQ(1u, recent.size());
  EXPECT_EQ(GURL("https://w2.example/"), recent.front().url);
}

// Tab search finds every space, not only the one the window is showing:
// another space's pinned entry and another space's Today tab are both
// reachable from a query issued while `first` is on screen.
TEST_F(SpaceScopingTest, SearchFindsAnotherSpacesEntryAndItsTodayTab) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://first.example/"), first);
  AddTabInSpace(GURL("https://alpha-today.example/"), work);
  const EntryId pinned_id = model_.AddEntry(
      work, EntryKind::kPinned, GURL("https://alpha-pin.example/"), u"Pin");

  const std::vector<SearchResult> results = Search(u"alpha", 10);

  bool found_today = false;
  bool found_pinned = false;
  for (const SearchResult& result : results) {
    if (result.source == SearchResult::Source::kLiveTab &&
        result.url == GURL("https://alpha-today.example/")) {
      found_today = true;
    }
    if (result.source == SearchResult::Source::kEntry &&
        result.entry_id == pinned_id) {
      found_pinned = true;
    }
  }
  EXPECT_TRUE(found_today) << "another space's Today tab was not found";
  EXPECT_TRUE(found_pinned) << "another space's pinned entry was not found";
}

// The active space is a tie-break, not a filter: two results that score the
// same sort with the active space's result first, and switching spaces flips
// which one that is without changing which results exist.
TEST_F(SpaceScopingTest, SearchTieBreaksTowardTheActiveSpace) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpaceWithTitle(GURL("https://a1.example/"), first, u"Match");
  AddTabInSpaceWithTitle(GURL("https://w1.example/"), work, u"Match");

  std::vector<SearchResult> results = Search(u"match", 10);
  ASSERT_EQ(2u, results.size());
  EXPECT_EQ(GURL("https://a1.example/"), results[0].url);
  EXPECT_EQ(GURL("https://w1.example/"), results[1].url);

  switcher_->SwitchTo(work);
  results = Search(u"match", 10);
  ASSERT_EQ(2u, results.size());
  EXPECT_EQ(GURL("https://w1.example/"), results[0].url);
  EXPECT_EQ(GURL("https://a1.example/"), results[1].url);
}

// The sidebar model's own half of the space bar -- reporting every
// space with its counts already totalled, and switching between them.
// AddTabInSpace's first call lands in the empty strip and is auto-activated
// (tab_strip_model.cc's own rule for an empty strip's first insert), so `a1`
// goes into the first space before `work` gets any tabs -- otherwise `work`'s
// first tab would be the one auto-activated, and spaces[0].is_active would
// not hold before the switch below.
TEST_F(SpaceScopingTest, TheSidebarModelReportsAndSwitchesSpaces) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://w2.example/"), work);
  model_.AddEntry(work, EntryKind::kPinned, GURL("https://w3.example/"), u"W3");

  const std::vector<SidebarSpace> spaces = sidebar_->spaces();
  ASSERT_EQ(2u, spaces.size());
  EXPECT_TRUE(spaces[0].is_active);
  EXPECT_FALSE(spaces[1].is_active);
  EXPECT_EQ(work, spaces[1].id);
  EXPECT_EQ(2, spaces[1].open_tab_count);
  EXPECT_EQ(1, spaces[1].entry_count);

  sidebar_->SwitchToSpace(work);
  EXPECT_EQ(work, switcher_->active_space());
  EXPECT_FALSE(sidebar_->spaces()[0].is_active);
  EXPECT_TRUE(sidebar_->spaces()[1].is_active);
}

// A row says which space it is drawn in, the way the fake's rows do, so a
// view that reads it gets the same answer in the browser as in a view test.
// Both kinds of row are here: a Today tab and a pinned entry.
TEST_F(SpaceScopingTest, EveryRowCarriesTheSpaceItIsDrawnIn) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  model_.AddEntry(first, EntryKind::kPinned, GURL("https://a2.example/"),
                  u"A2");
  model_.AddEntry(work, EntryKind::kPinned, GURL("https://w2.example/"), u"W2");

  std::vector<SidebarRow> rows = sidebar_->rows();
  ASSERT_EQ(2u, rows.size());
  for (const SidebarRow& row : rows) {
    EXPECT_EQ(first, row.space) << row.url;
  }

  sidebar_->SwitchToSpace(work);
  rows = sidebar_->rows();
  ASSERT_EQ(2u, rows.size());
  for (const SidebarRow& row : rows) {
    EXPECT_EQ(work, row.space) << row.url;
  }
}

// A new space is somewhere you are put, not a dot that appears while you stay
// where you were. It has no tab of its own, so the switch lands on a blank one
// opened in it.
TEST_F(SpaceScopingTest, AddingASpaceSwitchesIntoItOntoABlankTab) {
  AddTabInSpace(GURL("https://a1.example/"), model_.default_space_id());

  sidebar_->AddSpace(u"New");

  const std::vector<SidebarSpace> spaces = sidebar_->spaces();
  ASSERT_EQ(2u, spaces.size());
  const SpaceId added = spaces[1].id;
  EXPECT_EQ(u"New", spaces[1].name);
  EXPECT_TRUE(spaces[1].is_active);
  EXPECT_EQ(added, switcher_->active_space());
  ASSERT_EQ(2, strip()->count());
  const int active = strip()->active_index();
  EXPECT_EQ(1, active);
  EXPECT_EQ(added, switcher_->SpaceOfTabAt(active));
  EXPECT_TRUE(strip()->GetWebContentsAt(active)->GetVisibleURL().is_empty());
}

// With no switcher -- the playground's wiring, and a window built without a
// sidebar -- there is no screen for the move to follow, so moving an entry is
// the model change and nothing else.
TEST_F(SpaceScopingTest, MovingAnEntryWithoutASwitcherMovesItInTheModel) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  const EntryId id = model_.AddEntry(first, EntryKind::kPinned,
                                     GURL("https://p1.example/"), u"P1");
  SidebarTabModel plain(strip(), &model_, &binding_);

  plain.MoveEntryToSpace(id, work);

  ASSERT_TRUE(model_.GetEntry(id));
  EXPECT_EQ(work, model_.GetEntry(id)->space_id);
  const std::vector<SidebarSpace> spaces = plain.spaces();
  ASSERT_EQ(2u, spaces.size());
  EXPECT_EQ(0, spaces[0].entry_count);
  EXPECT_EQ(1, spaces[1].entry_count);
}

}  // namespace
}  // namespace arcium
