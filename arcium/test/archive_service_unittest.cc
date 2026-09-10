// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/archive_service.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "arcium/browser/archive_store.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/common/arcium_features.h"
#include "arcium/test/space_test_util.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/time/clock.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/unload_controller.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/segmentation_platform/public/features.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

// Moved to arcium/test/space_test_util.h, which SpaceSwitcherTest's own
// HoldTabOpen also needs: the production seam a close can be declined at.
using arcium::test::DecliningUnloadHandler;

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
    // Nothing here is about segmentation; turn it off. By the typed constant
    // and by disabling one feature rather than replacing the list: a string
    // goes stale silently the next time upstream renames the feature, and
    // InitFromCommandLine would drop any Arcium feature set on the command
    // line.
    scoped_feature_list_.InitAndDisableFeature(
        segmentation_platform::features::kSegmentationPlatformFeature);
  }

 protected:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(archive_.Open(temp_dir_.GetPath().AppendASCII("Archive")));
    sidebar_model_ =
        std::make_unique<SidebarTabModel>(strip(), &model_, &binding_);
    // Production order, and the only order any test here uses:
    // BrowserSidebarController builds the service from
    // BrowserView::BrowserView, when the strip is still empty. Every tab a test
    // adds arrives afterwards, exactly as session restore, the startup NTP and
    // command-line URLs do.
    ASSERT_EQ(0, strip()->count());
    service_ = std::make_unique<ArchiveService>(
        strip(), &model_, &binding_, &archive_,
        base::SequencedTaskRunner::GetCurrentDefault(), ServiceClock());
  }

  // The clock the service is built with. Null is what every window without
  // --arcium-fake-clock-offset gets: base::Time::Now(), which under this
  // fixture is the mock clock PassTime() drives. The fake-clock fixture at
  // the bottom of this file overrides it.
  virtual const base::Clock* ServiceClock() { return nullptr; }

  void TearDown() override {
    // Both observe the strip, which the base class is about to tear down.
    service_.reset();
    sidebar_model_.reset();
    BrowserWithTestWindowTest::TearDown();
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

  // The real seam for this is content's mojo call from a live renderer;
  // arcium::test::SetBeforeUnloadHandler stands in for it, in
  // space_test_util.h, shared with SpaceSwitcherTest.
  void AppendTabWithBeforeUnloadHandler(const GURL& url) {
    AppendTestTab(url);
    SetBeforeUnloadHandler(strip()->count() - 1, true);
  }

  void SetBeforeUnloadHandler(int index, bool present) {
    arcium::test::SetBeforeUnloadHandler(strip()->GetWebContentsAt(index),
                                         present);
  }

  TabStripModel* strip() { return browser()->tab_strip_model(); }

  tabs::TabHandle HandleAt(int index) {
    return strip()->GetTabAtIndex(index)->GetHandle();
  }

  // What session restore does: the WebContents is created already carrying the
  // last-active time saved in the session, through
  // WebContents::CreateParams::last_active_time (see CreateRestoredTab in
  // chrome/browser/ui/browser_tabrestore.cc), and is then inserted into a strip
  // the window already owns.
  void AppendRestoredTab(const GURL& url, base::Time last_active) {
    std::unique_ptr<content::WebContents> contents =
        content::WebContentsTester::CreateTestWebContents(profile(), nullptr);
    content::WebContentsTester* tester =
        content::WebContentsTester::For(contents.get());
    tester->NavigateAndCommit(url);
    tester->SetLastActiveTime(last_active);
    strip()->AppendWebContents(std::move(contents), /*foreground=*/false);
  }

  base::test::ScopedFeatureList scoped_feature_list_;
  base::ScopedTempDir temp_dir_;
  ArciumModel model_;
  TabBinding binding_;
  ArchiveStore archive_;
  std::unique_ptr<SidebarTabModel> sidebar_model_;
  std::unique_ptr<ArchiveService> service_;
};

TEST_F(ArchiveServiceTest, ATabIdleBeyondTheTimeoutIsArchivedAndClosed) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://keep.example/"));  // active, never archived

  PassTime(base::Hours(13));
  EXPECT_EQ(1, strip()->count());
  EXPECT_EQ(1u, archive_.ListRecent(model_.default_space_id(), 10).size());
}

TEST_F(ArchiveServiceTest, TheActiveTabIsNeverArchived) {
  AddTab(browser(), GURL("https://a.example/"));
  // No archivable tab, so no timer at all — without this the thirty-day
  // advance below proves nothing, because there is nothing that could fire.
  EXPECT_EQ(0, service_->live_timer_count_for_testing());
  PassTime(base::Days(30));
  EXPECT_EQ(1, strip()->count());
  EXPECT_EQ(0, service_->live_timer_count_for_testing());
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
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://a.example/"));  // index 0, and active
  ASSERT_TRUE(service_->next_expiry_for_testing().has_value());
  const base::Time first = *service_->next_expiry_for_testing();

  PassTime(base::Hours(1));
  // Visit the background tab and come back. Driven through the strip, because
  // that is the only way the browser ever tells this service about an
  // activation.
  strip()->ActivateTabAt(1);
  strip()->ActivateTabAt(0);
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
  const EntryId id = model_.AddEntryForTesting(
      EntryKind::kPinned, GURL("https://stale.example/"), u"S");
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

// C1. The worst thing this feature can do is eat the tab the user was reading
// a moment ago. A tab's idle clock must start when it stops being visible, not
// when it was activated — and the only way to see that is to drive the strip
// the way the browser does, which is what hid it.
TEST_F(ArchiveServiceTest, SwitchingAwayFromATabRestartsItsIdleClock) {
  AddTab(browser(), GURL("https://reading.example/"));
  // Thirteen hours of reading. It is the active tab, so nothing happens.
  PassTime(base::Hours(13));
  ASSERT_EQ(1, strip()->count());

  // The user opens a new tab. AddTab inserts at 0 and activates, so the tab
  // they were reading is now the background tab at index 1.
  AddTab(browser(), GURL("https://new.example/"));
  task_environment()->RunUntilIdle();
  EXPECT_EQ(2, strip()->count());

  // Its clock restarted just now, so it survives another eleven hours...
  PassTime(base::Hours(11));
  EXPECT_EQ(2, strip()->count());
  // ...and only then does the full timeout run out.
  PassTime(base::Hours(2));
  EXPECT_EQ(1, strip()->count());
}

// C2. BrowserView builds the service inside its own constructor, when the strip
// is still empty; every real tab arrives afterwards. A restored tab must bring
// its own truthful idle time with it, which it does: session restore puts the
// saved last-active time on the WebContents before it is inserted.
TEST_F(ArchiveServiceTest, ARestoredTabTakesItsSavedLastActiveTimeAsItsClock) {
  const base::Time yesterday = base::Time::Now() - base::Hours(20);
  AddTab(browser(), GURL("https://active.example/"));
  AppendRestoredTab(GURL("https://yesterday.example/"), yesterday);
  ASSERT_EQ(2, strip()->count());
  ASSERT_EQ(yesterday, strip()->GetWebContentsAt(1)->GetLastActiveTime());

  // No time passes: twenty hours have already gone by as far as the tab is
  // concerned, so the timer's zero delay is what runs the sweep.
  task_environment()->RunUntilIdle();
  EXPECT_EQ(1, strip()->count());
  EXPECT_EQ(1u, archive_.ListRecent(model_.default_space_id(), 10).size());
}

// The other half of the same rule, and the reason the restart floor could not
// be applied to every inserted tab: a tab the user opens right now gets the
// full timeout. Guard, not a regression.
TEST_F(ArchiveServiceTest, ATabOpenedNowGetsTheFullTimeout) {
  AddTab(browser(), GURL("https://active.example/"));
  AppendTestTab(GURL("https://fresh.example/"));
  ASSERT_EQ(2, strip()->count());

  PassTime(base::Hours(11));
  EXPECT_EQ(2, strip()->count());
  PassTime(base::Hours(2));
  EXPECT_EQ(1, strip()->count());
}

// M1. One timer, fired more than once: archive, reschedule to the next
// earliest expiry, fire again. Nothing before this crossed two expiries.
TEST_F(ArchiveServiceTest, TheOneTimerFiresAgainAfterASweep) {
  AddTab(browser(), GURL("https://active.example/"));
  AppendTestTab(GURL("https://early.example/"));
  PassTime(base::Hours(6));
  AppendTestTab(GURL("https://late.example/"));
  ASSERT_EQ(3, strip()->count());

  // t0 + 13h: `early` is out of time, `late` has been idle seven hours.
  PassTime(base::Hours(7));
  EXPECT_EQ(2, strip()->count());
  EXPECT_EQ(1u, archive_.ListRecent(model_.default_space_id(), 10).size());
  // The same timer, re-aimed at `late` rather than left stopped.
  EXPECT_EQ(1, service_->live_timer_count_for_testing());

  // t0 + 19h: `late` reaches thirteen hours and the timer fires a second time.
  PassTime(base::Hours(6));
  EXPECT_EQ(1, strip()->count());
  EXPECT_EQ(2u, archive_.ListRecent(model_.default_space_id(), 10).size());
  EXPECT_EQ(0, service_->live_timer_count_for_testing());
}

// I2. "The user asked to close it" is not "the tab closed". A close the user
// does not confirm leaves the tab open, and a row written anyway is a row per
// press of Clear, in an archive nothing reads and nothing prunes.
TEST_F(ArchiveServiceTest, AClearTheUserDoesNotConfirmWritesNoRow) {
  auto handler = std::make_unique<DecliningUnloadHandler>();
  DecliningUnloadHandler* handler_ptr = handler.get();
  UnloadController::From(browser())->AddTabUnloadHandler(std::move(handler));
  AddTab(browser(), GURL("https://today.example/"));

  service_->ArchiveAllToday();
  task_environment()->RunUntilIdle();
  ASSERT_EQ(1, strip()->count());  // The confirmation is up; nothing closed.
  EXPECT_TRUE(archive_.ListRecent(model_.default_space_id(), 10).empty());
  EXPECT_EQ(1u, service_->pending_archive_count_for_testing());

  // And pressing Clear again does not stack up a second row for the same tab,
  // nor a second parked one behind it — which is what would let a tab closed
  // once, later, write the archive twice.
  service_->ArchiveAllToday();
  task_environment()->RunUntilIdle();
  ASSERT_EQ(1, strip()->count());
  EXPECT_TRUE(archive_.ListRecent(model_.default_space_id(), 10).empty());
  EXPECT_EQ(1u, service_->pending_archive_count_for_testing());

  // TearDown closes every tab; let them go.
  handler_ptr->set_intercept(false);
}

// I2, the other half — the regression round 1 introduced. ArchiveAndClose
// used to decide synchronously, right after CloseWebContentsAt returned: if
// the tab was still there it concluded the close had been declined and queued
// nothing. That is wrong for every close that completes LATER than the call
// — a beforeunload dialog the user goes on to accept — because by the time
// the tab actually goes there is nothing left to write. The tab vanished and
// the archive stayed empty.
//
// The close here is completed by the strip rather than by answering a
// dialog: UnloadController's confirmed path posts through
// TabInterface::GetBrowserWindowInterface(), which does not complete the
// close in this fixture. What matters for the contract is the shape, and it
// is the same one — the tab is removed strictly after ArchiveAndClose
// returned, and the write must still happen.
TEST_F(ArchiveServiceTest, ARowParkedByClearIsWrittenWhenTheTabActuallyGoes) {
  auto handler = std::make_unique<DecliningUnloadHandler>();
  DecliningUnloadHandler* handler_ptr = handler.get();
  UnloadController::From(browser())->AddTabUnloadHandler(std::move(handler));
  // A pinned tab keeps the strip from emptying when the Today tab goes;
  // pinned tabs are not Today tabs, so Clear leaves it alone.
  AddTab(browser(), GURL("https://today.example/"));
  AddTab(browser(), GURL("https://pinned.example/"));
  sidebar_model_->PinTab(0);
  ASSERT_EQ(2, strip()->count());

  service_->ArchiveAllToday();
  task_environment()->RunUntilIdle();
  ASSERT_EQ(2, strip()->count());  // Held: nothing has closed yet.
  ASSERT_TRUE(archive_.ListRecent(model_.default_space_id(), 10).empty());
  ASSERT_EQ(1u, service_->pending_archive_count_for_testing());

  // The close completes, arbitrarily later than the call that asked for it.
  handler_ptr->set_intercept(false);
  const int today_index = strip()->GetIndexOfTab(HandleAt(1).Get());
  ASSERT_NE(TabStripModel::kNoTab, today_index);
  strip()->CloseWebContentsAt(today_index, TabCloseTypes::CLOSE_USER_GESTURE);
  task_environment()->RunUntilIdle();

  ASSERT_EQ(1, strip()->count());
  const std::vector<ArchivedTab> rows =
      archive_.ListRecent(model_.default_space_id(), 10);
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(GURL("https://today.example/"), rows[0].url);
  EXPECT_EQ(0u, service_->pending_archive_count_for_testing());
}

// I2, the third half. A parked row used to wait against its tab for the rest
// of that tab's life, so a Clear the user declined would be honoured hours
// later by a close that had nothing to do with it: the user cancels the
// dialog, browses on in that tab, closes it by hand — and the row read at the
// Clear press is written, describing a page that is no longer open, landing
// mid-list because ListRecent orders by archived_at. A hand-closed Today tab
// is not archived at all, so the parked row goes when the page does.
TEST_F(ArchiveServiceTest, AClearTheUserDeclinesDoesNotFollowTheTabAround) {
  auto handler = std::make_unique<DecliningUnloadHandler>();
  DecliningUnloadHandler* handler_ptr = handler.get();
  UnloadController::From(browser())->AddTabUnloadHandler(std::move(handler));
  // The coalesced UI update is what carries a navigation to TabChangedAt, and
  // this test asserts on what that delivers rather than on the wall clock.
  browser()->set_update_ui_immediately_for_testing();
  // A pinned tab at index 0 keeps the strip from emptying; the Today tab is
  // appended as a TestWebContents because only that kind can be navigated by
  // hand afterwards.
  AddTab(browser(), GURL("https://pinned.example/"));
  content::WebContentsTester* today =
      AppendTestTab(GURL("https://today.example/"));
  sidebar_model_->PinTab(0);
  ASSERT_EQ(2, strip()->count());

  service_->ArchiveAllToday();
  task_environment()->RunUntilIdle();
  ASSERT_EQ(2, strip()->count());  // The confirmation is up; nothing closed.
  ASSERT_EQ(1u, service_->pending_archive_count_for_testing());

  // The user declines, and goes on using the tab.
  handler_ptr->set_intercept(false);
  today->NavigateAndCommit(GURL("https://later.example/"));
  task_environment()->RunUntilIdle();
  EXPECT_EQ(0u, service_->pending_archive_count_for_testing());

  // Hours later they close it themselves. Nothing asked for this one to be
  // archived, so nothing is written.
  PassTime(base::Hours(3));
  const int today_index = strip()->GetIndexOfTab(HandleAt(1).Get());
  ASSERT_NE(TabStripModel::kNoTab, today_index);
  strip()->CloseWebContentsAt(today_index, TabCloseTypes::CLOSE_USER_GESTURE);
  task_environment()->RunUntilIdle();

  ASSERT_EQ(1, strip()->count());
  EXPECT_TRUE(archive_.ListRecent(model_.default_space_id(), 10).empty());
}

// The other end of the same rule. A row that does survive to be written is
// stamped when the tab actually went, not when Clear was pressed — otherwise
// a close held behind a dialog for hours sorts into ListRecent among the tabs
// the user archived before it, which is not where they left it.
TEST_F(ArchiveServiceTest, AParkedRowIsStampedWhenTheTabActuallyGoes) {
  auto handler = std::make_unique<DecliningUnloadHandler>();
  DecliningUnloadHandler* handler_ptr = handler.get();
  UnloadController::From(browser())->AddTabUnloadHandler(std::move(handler));
  AddTab(browser(), GURL("https://today.example/"));
  AddTab(browser(), GURL("https://pinned.example/"));
  sidebar_model_->PinTab(0);
  ASSERT_EQ(2, strip()->count());

  service_->ArchiveAllToday();
  task_environment()->RunUntilIdle();
  ASSERT_EQ(1u, service_->pending_archive_count_for_testing());

  // Held for hours, then confirmed. Short of the twelve-hour timeout, so the
  // sweep is not what closes it.
  PassTime(base::Hours(5));
  const base::Time confirmed_at = base::Time::Now();
  handler_ptr->set_intercept(false);
  const int today_index = strip()->GetIndexOfTab(HandleAt(1).Get());
  ASSERT_NE(TabStripModel::kNoTab, today_index);
  strip()->CloseWebContentsAt(today_index, TabCloseTypes::CLOSE_USER_GESTURE);
  task_environment()->RunUntilIdle();

  const std::vector<ArchivedTab> rows =
      archive_.ListRecent(model_.default_space_id(), 10);
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(GURL("https://today.example/"), rows[0].url);
  EXPECT_GE(rows[0].archived_at, confirmed_at);
}

// M3. Clear closed tabs two different ways depending on whether the profile
// had an archive: the sidebar's own path set CLOSE_USER_GESTURE and the
// archive service's did not, because each had written its own close-types
// constant. One button must not take two paths. It is a button the user
// pressed, so both say so; the automatic sweep is the one that does not,
// because nothing the user did closed those tabs.
//
// The close is held by the unload handler on purpose: TabStripModel stamps
// the flag before it runs the unload listener, so a tab that stays is a tab
// whose close-types can still be read.
TEST_F(ArchiveServiceTest, ClearIsAUserGestureWithOrWithoutAnArchive) {
  auto handler = std::make_unique<DecliningUnloadHandler>();
  DecliningUnloadHandler* handler_ptr = handler.get();
  UnloadController::From(browser())->AddTabUnloadHandler(std::move(handler));
  AddTab(browser(), GURL("https://today.example/"));

  // No archive: SidebarTabModel closes the Today tabs itself.
  sidebar_model_->ClearToday();
  task_environment()->RunUntilIdle();
  ASSERT_EQ(1, strip()->count());
  EXPECT_TRUE(strip()->GetWebContentsAt(0)->GetClosedByUserGesture());

  handler_ptr->set_intercept(false);
  strip()->CloseAllTabs();
  task_environment()->RunUntilIdle();

  // With an archive: the same button, through ArchiveService, says the same
  // thing about the same gesture.
  handler_ptr->set_intercept(true);
  AddTab(browser(), GURL("https://today.example/"));
  ASSERT_FALSE(strip()->GetWebContentsAt(0)->GetClosedByUserGesture());
  sidebar_model_->SetArchiveService(service_.get());
  sidebar_model_->ClearToday();
  task_environment()->RunUntilIdle();
  ASSERT_EQ(1, strip()->count());
  EXPECT_TRUE(strip()->GetWebContentsAt(0)->GetClosedByUserGesture());

  handler_ptr->set_intercept(false);
}

// And the sweep, which is the case the flag is genuinely wrong for: no
// gesture closed these tabs, so nothing claims one did.
TEST_F(ArchiveServiceTest, TheIdleSweepIsNotAUserGesture) {
  auto handler = std::make_unique<DecliningUnloadHandler>();
  DecliningUnloadHandler* handler_ptr = handler.get();
  UnloadController::From(browser())->AddTabUnloadHandler(std::move(handler));
  AddTab(browser(), GURL("https://idle.example/"));
  AddTab(browser(), GURL("https://active.example/"));  // active, never swept

  PassTime(base::Hours(13));
  ASSERT_EQ(2, strip()->count());  // Held by the handler, not closed.
  const int idle_index = strip()->GetIndexOfTab(HandleAt(1).Get());
  ASSERT_NE(TabStripModel::kNoTab, idle_index);
  EXPECT_FALSE(strip()->GetWebContentsAt(idle_index)->GetClosedByUserGesture());

  handler_ptr->set_intercept(false);
}

// The archive list's read path, end to end and through the real store: the
// model asks, the service posts to the store's sequence, and the rows come
// back converted and newest first. Reads get the same "posted, never inline"
// treatment writes do — a SQLite read whose pages are not in the OS cache is
// a disk seek, and the click that opens the list is a frame the user watches.
TEST_F(ArchiveServiceTest, TheArchiveReadIsPostedAndComesBackNewestFirst) {
  sidebar_model_->SetArchiveService(service_.get());
  AddTab(browser(), GURL("https://old.example/"));
  service_->ArchiveAllToday();
  task_environment()->RunUntilIdle();
  PassTime(base::Hours(1));
  AddTab(browser(), GURL("https://new.example/"));
  service_->ArchiveAllToday();
  task_environment()->RunUntilIdle();

  std::optional<std::vector<ArchivedRow>> got;
  std::optional<bool> readable;
  sidebar_model_->RequestArchivedRows(
      10, base::BindLambdaForTesting(
              [&got, &readable](std::vector<ArchivedRow> rows, bool ok) {
                got = std::move(rows);
                readable = ok;
              }));
  EXPECT_FALSE(got.has_value());

  task_environment()->RunUntilIdle();
  ASSERT_TRUE(got.has_value());
  ASSERT_EQ(2u, got->size());
  EXPECT_EQ(GURL("https://new.example/"), (*got)[0].url);
  EXPECT_EQ(GURL("https://old.example/"), (*got)[1].url);
  EXPECT_FALSE((*got)[0].archived_at.is_null());
  // The store opened in SetUp, so an empty list here would mean "nothing
  // archived", not "could not be read".
  ASSERT_TRUE(readable.has_value());
  EXPECT_TRUE(*readable);
}

// A page that never got a title archives with an empty one, and a blank row
// is unclickable-looking, so the URL stands in. The fallback lives in
// SidebarTabModel::DeliverArchivedRows; the playground cannot reach it,
// because the playground has no SidebarTabModel.
TEST_F(ArchiveServiceTest, AnArchivedRowWithNoTitleFallsBackToItsUrl) {
  sidebar_model_->SetArchiveService(service_.get());
  ArchivedTab row;
  row.url = GURL("https://untitled.example/page");
  row.space_id = model_.default_space_id();
  row.archived_at = base::Time::Now();
  archive_.Add(row);

  std::optional<std::vector<ArchivedRow>> got;
  sidebar_model_->RequestArchivedRows(
      10,
      base::BindLambdaForTesting([&got](std::vector<ArchivedRow> rows,
                                        bool ok) { got = std::move(rows); }));
  task_environment()->RunUntilIdle();
  ASSERT_TRUE(got.has_value());
  ASSERT_EQ(1u, got->size());
  EXPECT_EQ(u"https://untitled.example/page", (*got)[0].title);
}

// The other half of has_archive(). A window can hold a service with no store
// behind it — a context with no archive file — and that has nothing to list
// and never will, so it gets no button. Without this, dropping the
// has_store() half of has_archive() changes no test result at all.
TEST_F(ArchiveServiceTest, AServiceWithNoStoreIsNotAnArchive) {
  ArchiveService storeless(strip(), &model_, &binding_, /*store=*/nullptr,
                           base::SequencedTaskRunner::GetCurrentDefault());
  EXPECT_FALSE(storeless.has_store());
  sidebar_model_->SetArchiveService(&storeless);
  EXPECT_FALSE(sidebar_model_->has_archive());

  // And it still answers, on a later turn, saying the archive is not readable
  // rather than that it is empty.
  std::optional<bool> readable;
  sidebar_model_->RequestArchivedRows(
      10, base::BindLambdaForTesting([&readable](std::vector<ArchivedRow> rows,
                                                 bool ok) { readable = ok; }));
  EXPECT_FALSE(readable.has_value());
  task_environment()->RunUntilIdle();
  ASSERT_TRUE(readable.has_value());
  EXPECT_FALSE(*readable);

  // `storeless` is about to go out of scope; the model must not keep pointing
  // at it into TearDown.
  sidebar_model_->SetArchiveService(nullptr);
}

// A read that genuinely crosses sequences, and a model destroyed while it is
// out there. The reply is bound through SidebarTabModel's WeakPtr, so it is
// dropped on the way back rather than delivered into a dead model — and
// unlike the no-service test in sidebar_tab_model_unittest.cc, this one goes
// through PostTaskAndReplyWithResult on a real thread-pool sequence, which is
// the path the browser actually takes.
TEST_F(ArchiveServiceTest,
       AnArchiveReadCrossingSequencesIsDroppedWhenTheModelGoes) {
  scoped_refptr<base::SequencedTaskRunner> store_runner =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE});
  auto store = std::make_unique<ArchiveStore>();
  store->DetachFromSequence();
  bool store_opened = false;
  base::RunLoop opened;
  store_runner->PostTaskAndReply(
      FROM_HERE, base::BindLambdaForTesting([&] {
        // No gtest assertion here: this runs off the main thread.
        store_opened = store->Open(temp_dir_.GetPath().AppendASCII("Crossing"));
      }),
      opened.QuitClosure());
  opened.Run();
  ASSERT_TRUE(store_opened);

  auto model = std::make_unique<SidebarTabModel>(strip(), &model_, &binding_);
  ArchiveService service(strip(), &model_, &binding_, store.get(),
                         store_runner);
  model->SetArchiveService(&service);

  bool ran = false;
  model->RequestArchivedRows(
      10, base::BindLambdaForTesting(
              [&ran](std::vector<ArchivedRow> rows, bool ok) { ran = true; }));
  // Gone while the read is on the store's sequence.
  model.reset();

  task_environment()->RunUntilIdle();
  EXPECT_FALSE(ran);

  // Tear the store down on its own sequence, as ArciumProfileState does.
  store_runner->DeleteSoon(FROM_HERE, std::move(store));
  task_environment()->RunUntilIdle();
}

// Reopening puts the page back in Today and takes the row out of the archive,
// so the list is not a place tabs pile up twice. The delete is posted like
// every other archive write.
TEST_F(ArchiveServiceTest, ReopeningAnArchivedRowOpensATabAndDropsTheRow) {
  sidebar_model_->SetArchiveService(service_.get());
  AddTab(browser(), GURL("https://gone.example/"));
  service_->ArchiveAllToday();
  task_environment()->RunUntilIdle();
  ASSERT_EQ(0, strip()->count());
  const std::vector<ArchivedTab> rows =
      archive_.ListRecent(model_.default_space_id(), 10);
  ASSERT_EQ(1u, rows.size());

  sidebar_model_->ReopenArchived(rows[0].url, rows[0].archived_at);
  ASSERT_EQ(1, strip()->count());
  EXPECT_EQ(GURL("https://gone.example/"),
            strip()->GetWebContentsAt(0)->GetVisibleURL());
  EXPECT_EQ(1u, archive_.ListRecent(model_.default_space_id(), 10).size());

  task_environment()->RunUntilIdle();
  EXPECT_TRUE(archive_.ListRecent(model_.default_space_id(), 10).empty());
}

// The reopened tab is a Today tab: it comes back claimed by nothing, so it is
// archivable again on the next sweep rather than pinned by accident.
TEST_F(ArchiveServiceTest, AReopenedTabIsATodayTab) {
  sidebar_model_->SetArchiveService(service_.get());
  AddTab(browser(), GURL("https://gone.example/"));
  service_->ArchiveAllToday();
  task_environment()->RunUntilIdle();
  const std::vector<ArchivedTab> rows =
      archive_.ListRecent(model_.default_space_id(), 10);
  ASSERT_EQ(1u, rows.size());

  sidebar_model_->ReopenArchived(rows[0].url, rows[0].archived_at);
  task_environment()->RunUntilIdle();
  ASSERT_EQ(1u, sidebar_model_->rows().size());
  EXPECT_EQ(SidebarSection::kToday, sidebar_model_->rows()[0].section);
  EXPECT_FALSE(sidebar_model_->rows()[0].entry_id.is_valid());
}

// The window has a service with a store behind it, so the sidebar offers the
// archive. The off-the-record case — no service at all — is in
// sidebar_tab_model_unittest.cc, which is where a model without one lives.
TEST_F(ArchiveServiceTest, AWindowWithAStoreHasAnArchive) {
  EXPECT_TRUE(service_->has_store());
  sidebar_model_->SetArchiveService(service_.get());
  EXPECT_TRUE(sidebar_model_->has_archive());
}

// What --arcium-fake-clock-offset=13h builds: BrowserSidebarController hands
// the window's service a clock reading that far ahead, and nothing else in
// the browser is told a thing. Half a day of waiting is not an acceptance
// step anyone can perform, and moving base::Time::Now() for the whole process
// would move it under the model stores and under the archive's own row
// timestamps — the very things the acceptance pass is checking.
class ArchiveServiceFakeClockTest : public ArchiveServiceTest {
 protected:
  const base::Clock* ServiceClock() override { return &clock_; }

  static constexpr base::TimeDelta kOffset = base::Hours(13);
  features::OffsetClock clock_{kOffset};
};

// The offset shifts what the service considers idle: a tab opened a moment
// ago, with no time passed at all, is already past a twelve-hour timeout.
TEST_F(ArchiveServiceFakeClockTest, TheOffsetMakesAFreshTabIdle) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://keep.example/"));  // active, never archived
  ASSERT_EQ(ArchiveTimeout::kTwelveHours, model_.spaces()[0].archive_timeout);

  // No PassTime: the point is that the offset alone is enough.
  task_environment()->RunUntilIdle();
  EXPECT_EQ(1, strip()->count());
  EXPECT_EQ(1u, archive_.ListRecent(model_.default_space_id(), 10).size());
}

// The guards still hold. The offset makes tabs look idle; it does not make
// them archivable, which is a different question and the one that decides
// whether the acceptance pass proves anything.
TEST_F(ArchiveServiceFakeClockTest, TheOffsetDoesNotOverrideTheGuards) {
  AddTab(browser(), GURL("https://pinned.example/"));
  AddTab(browser(), GURL("https://active.example/"));
  sidebar_model_->PinTab(1);
  task_environment()->RunUntilIdle();
  EXPECT_EQ(2, strip()->count());
}

// The row's own timestamp is real time. An archive written under the switch
// must still say when it was written, or the archive list the acceptance pass
// reads back is dated thirteen hours into the future.
TEST_F(ArchiveServiceFakeClockTest, TheOffsetDoesNotReachTheArchivedTimestamp) {
  const base::Time before = base::Time::Now();
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://keep.example/"));

  task_environment()->RunUntilIdle();
  const std::vector<ArchivedTab> rows =
      archive_.ListRecent(model_.default_space_id(), 10);
  ASSERT_EQ(1u, rows.size());
  EXPECT_GE(rows[0].archived_at, before);
  EXPECT_LE(rows[0].archived_at, base::Time::Now());
  EXPECT_LT(rows[0].archived_at, before + kOffset);
}

}  // namespace
}  // namespace arcium
