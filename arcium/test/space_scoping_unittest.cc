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
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/files/scoped_temp_dir.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/segmentation_platform/public/features.h"
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
  }

  void TearDown() override {
    // Both observe the strip and the switcher, which are about to be torn
    // down; the switcher goes last because the services above observe it.
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

  base::test::ScopedFeatureList scoped_feature_list_;
  base::ScopedTempDir temp_dir_;
  ArciumModel model_;
  TabBinding binding_;
  ArchiveStore archive_store_;
  std::unique_ptr<SpaceSwitcher> switcher_;
  std::unique_ptr<SidebarTabModel> sidebar_;
  std::unique_ptr<ArchiveService> archive_;
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

  switcher_->SwitchTo(work);
  ASSERT_EQ(1u, sidebar_->rows().size());
  EXPECT_EQ(SidebarSection::kPinned, sidebar_->rows().front().section);
}

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
  ASSERT_EQ(1, strip()->count());
  EXPECT_EQ(work, switcher_->SpaceOfTabAt(0));
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
  ASSERT_EQ(1, strip()->count());
  EXPECT_EQ(work, switcher_->SpaceOfTabAt(0));
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

}  // namespace
}  // namespace arcium
