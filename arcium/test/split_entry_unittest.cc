// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A split drawn as one sidebar row, and a pinned split kept as one entry,
// against a real tab strip: when two pinned entries become one, when they
// stop being one, and what the sidebar's commands do to the pair.

#include <memory>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/test/space_test_util.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/browser/split_controller.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class SplitEntryTest : public BrowserWithTestWindowTest {
 protected:
  SplitEntryTest()
      : BrowserWithTestWindowTest(
            content::BrowserTaskEnvironment::TimeSource::MOCK_TIME) {}

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    model_ =
        std::make_unique<SidebarTabModel>(strip(), &arcium_model_, &binding_);
    controller_ =
        std::make_unique<SplitController>(strip(), nullptr, model_.get());
    model_->SetSplitController(controller_.get());
  }

  void TearDown() override {
    model_->SetSplitController(nullptr);
    controller_.reset();
    model_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

  TabStripModel* strip() { return browser()->tab_strip_model(); }
  SpaceId FirstSpace() const { return arcium_model_.spaces().front().id; }

  tabs::TabInterface* AddTab(const char* url) {
    return arcium::test::AddTabInSpace(strip(), profile(), GURL(url),
                                       FirstSpace());
  }

  // Splits the tabs at 0 and 1, the first on screen, and lets the posted
  // link check run.
  void SplitFirstTwo() {
    strip()->ActivateTabAt(0);
    ASSERT_TRUE(controller_->SplitWithActive(1));
    task_environment()->RunUntilIdle();
  }

  std::vector<const TabEntry*> Pinned() {
    return arcium_model_.EntriesForKind(FirstSpace(), EntryKind::kPinned);
  }

  bool Linked(EntryId a, EntryId b) {
    const TabEntry* first = arcium_model_.GetEntry(a);
    const TabEntry* second = arcium_model_.GetEntry(b);
    return first && second && first->split_partner == b &&
           second->split_partner == a;
  }

  ArciumModel arcium_model_;
  TabBinding binding_;
  std::unique_ptr<SidebarTabModel> model_;
  std::unique_ptr<SplitController> controller_;
};

TEST_F(SplitEntryTest, PinningOneTodayHalfPinsBothAsOneEntry) {
  AddTab("https://a.test/");
  AddTab("https://b.test/");
  SplitFirstTwo();

  model_->PinTab(1);

  const std::vector<const TabEntry*> pinned = Pinned();
  ASSERT_EQ(2u, pinned.size());
  EXPECT_TRUE(Linked(pinned[0]->id, pinned[1]->id));
  // Left pane first.
  EXPECT_EQ(GURL("https://a.test/"), pinned[0]->url);
  const std::vector<SidebarRow> rows = model_->rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_TRUE(rows[0].split_joins_next);
  EXPECT_TRUE(rows[1].split_joins_previous);
}

TEST_F(SplitEntryTest, SplittingTwoPinnedEntriesMakesThemOne) {
  AddTab("https://a.test/");
  AddTab("https://b.test/");
  model_->PinTab(0);
  model_->PinTab(1);
  ASSERT_EQ(2u, Pinned().size());
  ASSERT_FALSE(Pinned()[0]->split_partner.is_valid());

  SplitFirstTwo();

  EXPECT_TRUE(Linked(Pinned()[0]->id, Pinned()[1]->id));
}

TEST_F(SplitEntryTest, EndingTheSplitMakesThemTwoAgain) {
  AddTab("https://a.test/");
  AddTab("https://b.test/");
  SplitFirstTwo();
  model_->PinTab(0);
  ASSERT_EQ(2u, Pinned().size());
  const EntryId a = Pinned()[0]->id;
  const EntryId b = Pinned()[1]->id;
  ASSERT_TRUE(Linked(a, b));

  controller_->Unsplit();
  task_environment()->RunUntilIdle();

  EXPECT_FALSE(Linked(a, b));
  EXPECT_EQ(2u, Pinned().size());
}

TEST_F(SplitEntryTest, TheMenusEndSplitDoesTheSame) {
  AddTab("https://a.test/");
  AddTab("https://b.test/");
  SplitFirstTwo();
  model_->PinTab(0);
  ASSERT_EQ(2u, Pinned().size());
  const EntryId a = Pinned()[0]->id;
  const EntryId b = Pinned()[1]->id;

  model_->EndSplit(model_->rows()[0]);
  task_environment()->RunUntilIdle();

  EXPECT_FALSE(Linked(a, b));
  EXPECT_FALSE(controller_->ActiveIsSplit());
}

TEST_F(SplitEntryTest, ClosingOneHalfKeepsThePairAsOneEntry) {
  AddTab("https://a.test/");
  AddTab("https://b.test/");
  SplitFirstTwo();
  model_->PinTab(0);
  ASSERT_EQ(2u, Pinned().size());
  const EntryId a = Pinned()[0]->id;
  const EntryId b = Pinned()[1]->id;

  model_->CloseEntryTab(b);
  task_environment()->RunUntilIdle();

  EXPECT_TRUE(Linked(a, b));
  const std::vector<SidebarRow> rows = model_->rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_TRUE(rows[0].split_joins_next);
  EXPECT_TRUE(rows[1].is_cold);
}

TEST_F(SplitEntryTest, ClosingBothKeepsThePairCold) {
  AddTab("https://a.test/");
  AddTab("https://b.test/");
  AddTab("https://c.test/");
  SplitFirstTwo();
  model_->PinTab(0);
  ASSERT_EQ(2u, Pinned().size());
  const EntryId a = Pinned()[0]->id;
  const EntryId b = Pinned()[1]->id;

  model_->CloseSplit(model_->rows()[0]);
  task_environment()->RunUntilIdle();

  EXPECT_EQ(1, strip()->count());
  EXPECT_TRUE(Linked(a, b));
}

TEST_F(SplitEntryTest, UnpinningOneHalfUnpinsBoth) {
  AddTab("https://a.test/");
  AddTab("https://b.test/");
  SplitFirstTwo();
  model_->PinTab(0);
  ASSERT_EQ(2u, Pinned().size());

  model_->UnpinEntry(Pinned()[1]->id);

  EXPECT_TRUE(Pinned().empty());
  EXPECT_EQ(2, strip()->count());
  // Still sharing the screen, now as two Today tabs drawn as one row.
  EXPECT_TRUE(controller_->ActiveIsSplit());
}

TEST_F(SplitEntryTest, ClickingAColdPairPutsBothPagesOnScreen) {
  const EntryId a = arcium_model_.AddEntryForTesting(
      EntryKind::kPinned, GURL("https://a.test/"), u"A");
  const EntryId b = arcium_model_.AddEntryForTesting(
      EntryKind::kPinned, GURL("https://b.test/"), u"B");
  ASSERT_TRUE(arcium_model_.LinkSplitEntries(a, b));
  AddTab("https://elsewhere.test/");

  model_->ActivateEntry(b);

  EXPECT_EQ(3, strip()->count());
  ASSERT_TRUE(controller_->ActiveIsSplit());
  // In the pair's own order, whichever half was clicked.
  ASSERT_EQ(2u, strip()->GetForegroundTabs().size());
  EXPECT_LT(strip()->GetIndexOfTab(binding_.TabForEntry(a)->Get()),
            strip()->GetIndexOfTab(binding_.TabForEntry(b)->Get()));
  EXPECT_EQ(binding_.TabForEntry(b)->Get(), strip()->GetActiveTab());
}

TEST_F(SplitEntryTest, ATodayTabSplitWithAPinnedEntryIsDrawnBesideIt) {
  AddTab("https://a.test/");
  AddTab("https://b.test/");
  model_->PinTab(1);
  SplitFirstTwo();

  const std::vector<SidebarRow> rows = model_->rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(SidebarSection::kPinned, rows[0].DrawnSection());
  EXPECT_EQ(SidebarSection::kPinned, rows[1].DrawnSection());
  EXPECT_TRUE(rows[0].split_joins_next);
  // Not linked: a Today tab is not an entry until it is pinned.
  ASSERT_EQ(1u, Pinned().size());
  EXPECT_FALSE(Pinned()[0]->split_partner.is_valid());
}

}  // namespace
}  // namespace arcium
