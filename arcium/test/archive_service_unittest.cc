// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/archive_service.h"

#include <memory>
#include <vector>

#include "arcium/browser/archive_store.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/model_store.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"  // nogncheck
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/frame/sudden_termination_disabler_type.mojom.h"
#include "url/gurl.h"

namespace arcium {
namespace {

// BrowserWithTestWindowTest::AddTab inserts at index 0 and activates, so the
// tab named last is the one at index 0 and the one the strip calls active.
class ArchiveServiceTest : public BrowserWithTestWindowTest {
 public:
  ArchiveServiceTest()
      : BrowserWithTestWindowTest(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    // These tests move the clock by days. The segmentation platform runs model
    // executions off that clock and parks a task runner in a process-global
    // object, which the next test then CHECKs on ("a previous test leaving a
    // stale task runner in a global object" is base's own wording for it).
    // Nothing here is about segmentation; turn it off.
    scoped_feature_list_.InitFromCommandLine(
        /*enable_features=*/"", /*disable_features=*/"SegmentationPlatform");
  }

 protected:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(archive_.Open(temp_dir_.GetPath().AppendASCII("Archive")));
    sidebar_model_ =
        std::make_unique<SidebarTabModel>(strip(), &model_, &binding_);
    MakeService(/*model_store=*/nullptr);
  }

  void TearDown() override {
    // Both observe the strip, which the base class is about to tear down.
    service_.reset();
    sidebar_model_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

  // The store runs on the main sequence here rather than in the thread pool,
  // so a test can read the archive back without hopping. It is still *posted*:
  // nothing is written inside the call that decides to archive, which is what
  // TheArchiveWriteIsPostedNotDoneInline pins. Production passes a MayBlock
  // pool sequence; see ArciumProfileState.
  void MakeService(ModelStore* model_store) {
    service_.reset();
    service_ = std::make_unique<ArchiveService>(
        strip(), &model_, &binding_, &archive_,
        base::SequencedTaskRunner::GetCurrentDefault(), model_store);
  }

  // AdvanceClock plus a drain, not FastForwardBy. Fast-forwarding steps the
  // harness through every intervening delayed task, and in a full browser
  // fixture there are a great many that are nothing to do with us: half a day
  // of mock time measured 107 seconds of wall clock, and the brief's 30-day
  // waits never finished at all. Jumping the clock and draining what has come
  // due is the same thing for a service whose timer has one expiry, and it is
  // instant.
  void PassTime(base::TimeDelta delta) {
    task_environment()->AdvanceClock(delta);
    task_environment()->RunUntilIdle();
  }

  // A background tab built as a TestWebContents, which is the only kind that
  // can be told about audio or about an unload handler. Appended, so the tab
  // AddTab made stays at index 0 and stays active.
  content::WebContentsTester* AppendTestTab(const GURL& url) {
    std::unique_ptr<content::WebContents> contents =
        content::WebContentsTester::CreateTestWebContents(profile(), nullptr);
    content::WebContentsTester* tester =
        content::WebContentsTester::For(contents.get());
    tester->NavigateAndCommit(url);
    strip()->AppendWebContents(std::move(contents), /*foreground=*/false);
    return tester;
  }

  void AppendAudibleTab(const GURL& url) {
    AppendTestTab(url)->SetIsCurrentlyAudible(true);
  }

  // content exposes no public way to give a test page a beforeunload handler:
  // the only seam is the mojo call a live renderer makes, which lands on
  // RenderFrameHostImpl. The brief's SimulateBeforeUnloadHandlerPresent does
  // not exist in 152, and this is what content's own tests do instead (see
  // render_frame_host_impl_browsertest.cc). The cast is sound because the
  // frame of a TestWebContents really is a TestRenderFrameHost.
  void AppendTabWithBeforeUnloadHandler(const GURL& url) {
    AppendTestTab(url);
    SetBeforeUnloadHandler(strip()->count() - 1, true);
  }

  void SetBeforeUnloadHandler(int index, bool present) {
    static_cast<content::RenderFrameHostImpl*>(
        strip()->GetWebContentsAt(index)->GetPrimaryMainFrame())
        ->SuddenTerminationDisablerChanged(
            present,
            blink::mojom::SuddenTerminationDisablerType::kBeforeUnloadHandler);
  }

  TabStripModel* strip() { return browser()->tab_strip_model(); }

  tabs::TabHandle HandleAt(int index) {
    return strip()->GetTabAtIndex(index)->GetHandle();
  }

  // `model_store_` is a fixture member rather than a local so that it always
  // outlives the service that holds a pointer to it.
  ModelStore* MakeModelStore() {
    model_store_ = std::make_unique<ModelStore>(
        &model_, temp_dir_.GetPath().AppendASCII("Model"));
    return model_store_.get();
  }

  base::test::ScopedFeatureList scoped_feature_list_;
  base::ScopedTempDir temp_dir_;
  ArciumModel model_;
  TabBinding binding_;
  ArchiveStore archive_;
  std::unique_ptr<ModelStore> model_store_;
  std::unique_ptr<SidebarTabModel> sidebar_model_;
  std::unique_ptr<ArchiveService> service_;
};

TEST_F(ArchiveServiceTest, ATabIdleBeyondTheTimeoutIsArchivedAndClosed) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://keep.example/"));  // active, never archived
  service_->OnTabActivated(HandleAt(0));

  PassTime(base::Hours(13));
  EXPECT_EQ(1, strip()->count());
  EXPECT_EQ(1u, archive_.ListRecent(model_.default_space_id(), 10).size());
}

TEST_F(ArchiveServiceTest, TheActiveTabIsNeverArchived) {
  AddTab(browser(), GURL("https://a.example/"));
  PassTime(base::Days(30));
  EXPECT_EQ(1, strip()->count());
}

TEST_F(ArchiveServiceTest, ATabPlayingAudioIsNeverArchived) {
  AddTab(browser(), GURL("https://active.example/"));
  // Appended by hand, not through AddTab: AddTab goes through chrome's own
  // navigation path, which makes a real WebContentsImpl, and only a
  // TestWebContents can be told it is playing audio. (The brief's
  // WebContentsTester::For on an AddTab tab is a static_cast to a type the
  // object does not have, and crashes.)
  AppendAudibleTab(GURL("https://music.example/"));
  ASSERT_EQ(2, strip()->count());
  ASSERT_TRUE(strip()->GetWebContentsAt(1)->IsCurrentlyAudible());
  service_->OnTabActivated(HandleAt(1));

  PassTime(base::Days(30));
  // ASSERT, not EXPECT: the check below indexes the strip, and if the tab was
  // archived after all there is no index 1 to ask about.
  ASSERT_EQ(2, strip()->count());
  EXPECT_FALSE(service_->MayArchive(HandleAt(1)));
}

TEST_F(ArchiveServiceTest, ATabWithAnUnloadHandlerIsNeverArchived) {
  AddTab(browser(), GURL("https://active.example/"));
  AppendTabWithBeforeUnloadHandler(GURL("https://form.example/"));
  ASSERT_EQ(2, strip()->count());
  ASSERT_TRUE(
      strip()->GetWebContentsAt(1)->NeedToFireBeforeUnloadOrUnloadEvents());
  service_->OnTabActivated(HandleAt(1));

  PassTime(base::Days(30));
  ASSERT_EQ(2, strip()->count());
  EXPECT_FALSE(service_->MayArchive(HandleAt(1)));

  // TearDown closes every tab, and closing one that has an unload handler
  // reaches machinery this fixture does not set up (PerformanceManager
  // CHECKs). The assertions are made; take the handler back off.
  SetBeforeUnloadHandler(1, false);
}

TEST_F(ArchiveServiceTest, PinnedAndFavouriteTabsAreNeverArchived) {
  AddTab(browser(), GURL("https://pinned.example/"));
  AddTab(browser(), GURL("https://active.example/"));
  sidebar_model_->PinTab(1);
  PassTime(base::Days(30));
  EXPECT_EQ(2, strip()->count());
}

TEST_F(ArchiveServiceTest, NeverMeansNoTimerIsEverScheduled) {
  model_.SetArchiveTimeout(model_.default_space_id(), ArchiveTimeout::kNever);
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  EXPECT_FALSE(service_->next_expiry_for_testing().has_value());
  EXPECT_EQ(0, service_->live_timer_count_for_testing());
  PassTime(base::Days(365));
  EXPECT_EQ(2, strip()->count());
}

TEST_F(ArchiveServiceTest, OneTimerServesEveryTab) {
  for (int i = 0; i < 20; ++i) {
    AddTab(browser(), GURL("https://example.com/" + base::NumberToString(i)));
  }
  // The contract is one live timer for the whole browser, not one per tab.
  // It is restarted as tabs come and go; what must never grow is the count.
  EXPECT_EQ(1, service_->live_timer_count_for_testing());
  // And it is aimed at the earliest expiry among archivable tabs.
  ASSERT_TRUE(service_->next_expiry_for_testing().has_value());
}

TEST_F(ArchiveServiceTest, ActivityPushesTheExpiryOut) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  // Index 1, not 0: AddTab activates what it inserts, so index 0 is the active
  // tab, which MayArchive excludes and which therefore has no expiry to push
  // out. (The brief says HandleAt(0), which only works if AddTab appended.)
  service_->OnTabActivated(HandleAt(1));
  const base::Time first = *service_->next_expiry_for_testing();
  PassTime(base::Hours(1));
  service_->OnTabActivated(HandleAt(1));
  EXPECT_GT(*service_->next_expiry_for_testing(), first);
}

TEST_F(ArchiveServiceTest, ArchiveAllTodayLeavesPinnedAndFavouritesAlone) {
  AddTab(browser(), GURL("https://today.example/"));
  AddTab(browser(), GURL("https://pinned.example/"));
  sidebar_model_->PinTab(0);
  service_->ArchiveAllToday();
  EXPECT_EQ(1, strip()->count());
}

// The trap from Task 6, in its second form. ArciumModel::ReplaceAll — which
// ModelStore::Load runs while the window is already interactive — drops
// entries without touching TabBinding. A tab left bound to an entry that is
// gone is a Today tab: Task 6 made it visible again, and this makes it
// mortal. With a raw IsBound() check it would be neither.
TEST_F(ArchiveServiceTest, ATabBoundToAVanishedEntryIsStillArchivable) {
  AddTab(browser(), GURL("https://stale.example/"));
  AddTab(browser(), GURL("https://active.example/"));
  const EntryId id =
      model_.AddEntry(EntryKind::kPinned, GURL("https://stale.example/"), u"S");
  binding_.Bind(id, HandleAt(1));
  ASSERT_TRUE(binding_.IsBound(HandleAt(1)));
  ASSERT_FALSE(service_->MayArchive(HandleAt(1)));

  // What the load's completion does: entries replaced wholesale, binding
  // untouched.
  std::vector<Space> spaces = model_.spaces();
  model_.ReplaceAll(std::move(spaces), {}, {});
  ASSERT_TRUE(binding_.IsBound(HandleAt(1)));

  EXPECT_TRUE(service_->MayArchive(HandleAt(1)));
  PassTime(base::Hours(13));
  EXPECT_EQ(1, strip()->count());
}

// "Never write on the UI thread" starts with never writing inline. The store
// here shares the test's sequence, so the row can only appear once the posted
// task has run; in the browser that sequence is a MayBlock pool sequence.
TEST_F(ArchiveServiceTest, TheArchiveWriteIsPostedNotDoneInline) {
  AddTab(browser(), GURL("https://today.example/"));
  AddTab(browser(), GURL("https://pinned.example/"));
  sidebar_model_->PinTab(0);

  service_->ArchiveAllToday();
  EXPECT_TRUE(archive_.ListRecent(model_.default_space_id(), 10).empty());

  task_environment()->RunUntilIdle();
  EXPECT_EQ(1u, archive_.ListRecent(model_.default_space_id(), 10).size());
}

TEST_F(ArchiveServiceTest, TheArchivedRowCarriesTheTabsUrlAndSpace) {
  AddTab(browser(), GURL("https://today.example/"));
  AddTab(browser(), GURL("https://pinned.example/"));
  sidebar_model_->PinTab(0);
  service_->ArchiveAllToday();
  task_environment()->RunUntilIdle();

  const std::vector<ArchivedTab> rows =
      archive_.ListRecent(model_.default_space_id(), 10);
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(GURL("https://today.example/"), rows[0].url);
  EXPECT_EQ(model_.default_space_id(), rows[0].space_id);
  EXPECT_FALSE(rows[0].archived_at.is_null());
}

// Tabs that were already open when the service was made have no idle stamp of
// their own. Their clock starts at the model's last save — the last moment the
// browser knew about them — so a browser closed overnight archives yesterday's
// Today tabs on launch without any per-tab timestamp surviving the quit.
TEST_F(ArchiveServiceTest, RestoredTabsTakeTheModelsLastSaveAsTheirIdleFloor) {
  ModelStore* store = MakeModelStore();
  model_.AddEntry(EntryKind::kPinned, GURL("https://p.example/"), u"P");
  store->SaveNowForTesting();
  task_environment()->RunUntilIdle();
  ASSERT_FALSE(store->last_save_time().is_null());

  // The quit, and the launch: tabs exist before the service does.
  PassTime(base::Hours(13));
  AddTab(browser(), GURL("https://yesterday.example/"));
  AddTab(browser(), GURL("https://active.example/"));
  MakeService(store);

  // No time passes. The floor alone makes the restored tabs expired, and the
  // timer's zero delay is what runs the sweep.
  task_environment()->RunUntilIdle();
  EXPECT_EQ(1, strip()->count());
  EXPECT_EQ(1u, archive_.ListRecent(model_.default_space_id(), 10).size());
}

// ...and before the load has landed there is no last save time, so the floor
// is startup and nothing is archived early. This is the case the async load
// makes real: ModelStore::Load returns long before last_save_time() is set.
TEST_F(ArchiveServiceTest, ATabIsNotArchivedWhileTheLoadIsStillOutstanding) {
  ModelStore* store = MakeModelStore();
  ASSERT_TRUE(store->last_save_time().is_null());

  AddTab(browser(), GURL("https://restored.example/"));
  AddTab(browser(), GURL("https://active.example/"));
  MakeService(store);

  task_environment()->RunUntilIdle();
  EXPECT_EQ(2, strip()->count());
  PassTime(base::Hours(11));
  EXPECT_EQ(2, strip()->count());
  // The floor is the service's own start, so the full timeout still applies.
  PassTime(base::Hours(2));
  EXPECT_EQ(1, strip()->count());
}

// A handle is weak. A tab this strip no longer holds — closed, or moved to
// another window, where it is that window's service's business — must not be
// archived a second time. Guard, not a regression.
TEST_F(ArchiveServiceTest, ATabTheStripNoLongerHoldsIsNotArchivable) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  const tabs::TabHandle handle = HandleAt(1);
  ASSERT_TRUE(service_->MayArchive(handle));
  strip()->CloseWebContentsAt(1, TabCloseTypes::CLOSE_NONE);
  EXPECT_FALSE(service_->MayArchive(handle));
}

TEST_F(ArchiveServiceTest, ChangingTheTimeoutReschedulesTheOneTimer) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  ASSERT_TRUE(service_->next_expiry_for_testing().has_value());
  const base::Time twelve_hours = *service_->next_expiry_for_testing();

  sidebar_model_->SetArchiveTimeout(ArchiveTimeout::kSevenDays);
  ASSERT_TRUE(service_->next_expiry_for_testing().has_value());
  EXPECT_GT(*service_->next_expiry_for_testing(), twelve_hours);
  EXPECT_EQ(1, service_->live_timer_count_for_testing());

  sidebar_model_->SetArchiveTimeout(ArchiveTimeout::kNever);
  EXPECT_FALSE(service_->next_expiry_for_testing().has_value());
  EXPECT_EQ(0, service_->live_timer_count_for_testing());
}

}  // namespace
}  // namespace arcium
