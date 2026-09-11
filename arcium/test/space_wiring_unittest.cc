// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A window's switcher, sidebar model and archive service, wired the way
// BrowserSidebarController wires them. Closing the active space's last open
// tab from the sidebar -- its row, a pinned row's close, Clear -- has to leave
// the space on screen with a blank tab, exactly as Cmd+W does. And the
// archive service reaches the switcher by itself, so no wiring can forget to
// hand it over or forget to take it back.

#include <memory>

#include "arcium/browser/archive_store.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/test/space_test_util.h"
#include "arcium/ui/browser/archive_service.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/files/scoped_temp_dir.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class SpaceWiringTest : public BrowserWithTestWindowTest {
 protected:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(
        archive_store_.Open(temp_dir_.GetPath().AppendASCII("Archive")));
    // Production order: the switcher first, since both of the others hold a
    // pointer to it, and the strip still empty when all three are built.
    switcher_ = std::make_unique<SpaceSwitcher>(strip(), &model_, &binding_);
    sidebar_ = std::make_unique<SidebarTabModel>(strip(), &model_, &binding_,
                                                 switcher_.get());
    ASSERT_EQ(0, strip()->count());
    archive_ = std::make_unique<ArchiveService>(
        strip(), &model_, &binding_, &archive_store_,
        base::SequencedTaskRunner::GetCurrentDefault(), /*clock=*/nullptr,
        switcher_.get());
    sidebar_->SetArchiveService(archive_.get());
  }

  void TearDown() override {
    // The reverse of SetUp: the switcher goes last because both of the
    // others hold a pointer to it.
    archive_.reset();
    sidebar_.reset();
    switcher_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

  TabStripModel* strip() { return browser()->tab_strip_model(); }

  tabs::TabInterface* AddTabInSpace(const GURL& url, SpaceId space) {
    return arcium::test::AddTabInSpace(strip(), profile(), url, space);
  }

  // The window is still on `space`, and what it shows there is a blank tab.
  void ExpectABlankTabOnScreenIn(SpaceId space) {
    EXPECT_EQ(space, switcher_->active_space());
    EXPECT_EQ(space, switcher_->SpaceOfTabAt(strip()->active_index()));
    EXPECT_TRUE(strip()->GetActiveWebContents()->GetVisibleURL().is_empty());
  }

  base::ScopedTempDir temp_dir_;
  ArciumModel model_;
  TabBinding binding_;
  ArchiveStore archive_store_;
  std::unique_ptr<SpaceSwitcher> switcher_;
  std::unique_ptr<SidebarTabModel> sidebar_;
  std::unique_ptr<ArchiveService> archive_;
};

// The row's close button, and the Today menu's Close.
TEST_F(SpaceWiringTest, ClosingTheSpacesLastTabFromItsRowLeavesABlankOne) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  tabs::TabInterface* w1 = AddTabInSpace(GURL("https://w1.example/"), work);
  switcher_->SwitchTo(work);
  ASSERT_EQ(strip()->GetIndexOfTab(w1), strip()->active_index());

  sidebar_->CloseTab(strip()->GetIndexOfTab(w1));
  task_environment()->RunUntilIdle();

  EXPECT_EQ(2, strip()->count());  // a1, and the blank tab.
  ExpectABlankTabOnScreenIn(work);
}

// A pinned row's "Close tab": the entry stays and turns cold, and the space
// it is in keeps the screen.
TEST_F(SpaceWiringTest, ClosingThePinnedRowsTabLeavesABlankOne) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  tabs::TabInterface* p1 = AddTabInSpace(GURL("https://p1.example/"), work);
  const EntryId pin = model_.AddEntry(work, EntryKind::kPinned,
                                      GURL("https://p1.example/"), u"P1");
  binding_.Bind(pin, p1->GetHandle());
  switcher_->SwitchTo(work);
  ASSERT_EQ(strip()->GetIndexOfTab(p1), strip()->active_index());

  sidebar_->CloseEntryTab(pin);
  task_environment()->RunUntilIdle();

  EXPECT_EQ(2, strip()->count());
  ExpectABlankTabOnScreenIn(work);
  EXPECT_TRUE(model_.GetEntry(pin));
}

TEST_F(SpaceWiringTest, ClearingTheSpacesTodayLeavesABlankOne) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://w2.example/"), work);
  switcher_->SwitchTo(work);

  sidebar_->ClearToday();
  task_environment()->RunUntilIdle();

  EXPECT_EQ(2, strip()->count());
  ExpectABlankTabOnScreenIn(work);
}

// SidebarTabModel's own loop, for a window with no archive service.
TEST_F(SpaceWiringTest, ClearingWithoutAnArchiveLeavesABlankOneToo) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  switcher_->SwitchTo(work);
  sidebar_->SetArchiveService(nullptr);

  sidebar_->ClearToday();
  task_environment()->RunUntilIdle();

  EXPECT_EQ(2, strip()->count());
  ExpectABlankTabOnScreenIn(work);
}

// Nothing here hands the service to the switcher: the service does that
// itself, so a space delete drops the space's archived rows with it.
TEST_F(SpaceWiringTest, ADeletedSpacesArchivedRowsGoWithIt) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  switcher_->SwitchTo(work);
  sidebar_->ClearToday();
  task_environment()->RunUntilIdle();
  ASSERT_EQ(1u, archive_store_.ListRecent(work, 10).size());

  switcher_->DeleteSpace(work);
  task_environment()->RunUntilIdle();
  EXPECT_TRUE(archive_store_.ListRecent(work, 10).empty());
}

// The switcher outlives the service, so the service takes itself back when
// it goes; a delete after that has no service to reach.
TEST_F(SpaceWiringTest, ADeleteAfterTheServiceIsGoneDoesNotReachIt) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  sidebar_->SetArchiveService(nullptr);
  archive_.reset();

  switcher_->DeleteSpace(work);
  task_environment()->RunUntilIdle();
  EXPECT_EQ(1u, model_.spaces().size());
}

}  // namespace
}  // namespace arcium
