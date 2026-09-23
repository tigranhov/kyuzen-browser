// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/split_rows.h"

#include <memory>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/test/space_test_util.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/browser/split_controller.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/tab_interface.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

// The grouping rule on its own, with no tab strip: rows in, rows out.
SidebarRow MakeRow(SidebarSection section,
                   std::optional<split_tabs::SplitTabId> split,
                   int tab_index = -1) {
  SidebarRow row;
  row.section = section;
  row.split = split;
  row.tab_index = tab_index;
  row.title = base::NumberToString16(tab_index);
  return row;
}

SidebarRow EntryRow(EntryId id, EntryId partner) {
  SidebarRow row = MakeRow(SidebarSection::kPinned, std::nullopt);
  row.entry_id = id;
  row.split_partner = partner;
  row.is_cold = true;
  return row;
}

std::vector<int> TabIndices(const std::vector<SidebarRow>& rows) {
  std::vector<int> indices;
  for (const SidebarRow& row : rows) {
    indices.push_back(row.tab_index);
  }
  return indices;
}

TEST(SplitRowsTest, NeighbouringHalvesAreJoined) {
  const split_tabs::SplitTabId id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SidebarRow> rows = {MakeRow(SidebarSection::kToday, id, 0),
                                  MakeRow(SidebarSection::kToday, id, 1)};

  GroupSplitRows(rows, /*active_tab_index=*/0);

  EXPECT_TRUE(rows[0].split_joins_next);
  EXPECT_TRUE(rows[1].split_joins_previous);
  EXPECT_FALSE(rows[0].split_joins_previous);
  EXPECT_FALSE(rows[1].split_joins_next);
}

TEST(SplitRowsTest, HalvesApartAreBroughtTogetherWhereTheFirstWas) {
  const split_tabs::SplitTabId id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SidebarRow> rows = {MakeRow(SidebarSection::kToday, id, 0),
                                  MakeRow(SidebarSection::kToday, {}, 1),
                                  MakeRow(SidebarSection::kToday, id, 2)};

  GroupSplitRows(rows, /*active_tab_index=*/0);

  EXPECT_EQ((std::vector<int>{0, 2, 1}), TabIndices(rows));
  EXPECT_TRUE(rows[0].split_joins_next);
  EXPECT_TRUE(rows[1].split_joins_previous);
}

TEST(SplitRowsTest, APinnedHalfDrawsItsTodayPartnerInPinnedAndInItsFolder) {
  const split_tabs::SplitTabId id = split_tabs::SplitTabId::GenerateNew();
  const FolderId folder = FolderId::Generate();
  SidebarRow pinned = MakeRow(SidebarSection::kPinned, id, 3);
  pinned.folder_id = folder;
  std::vector<SidebarRow> rows = {pinned,
                                  MakeRow(SidebarSection::kToday, {}, 0),
                                  MakeRow(SidebarSection::kToday, id, 2)};

  GroupSplitRows(rows, /*active_tab_index=*/2);

  // Pane order: the lower strip index is the left pane.
  EXPECT_EQ((std::vector<int>{2, 3, 0}), TabIndices(rows));
  EXPECT_EQ(SidebarSection::kToday, rows[0].section);
  EXPECT_EQ(SidebarSection::kPinned, rows[0].DrawnSection());
  EXPECT_EQ(folder, rows[0].folder_id);
  EXPECT_EQ(SidebarSection::kToday, rows[2].DrawnSection());
}

TEST(SplitRowsTest, OnlyTheFocusedHalfIsCurrent) {
  const split_tabs::SplitTabId id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SidebarRow> rows = {MakeRow(SidebarSection::kToday, id, 0),
                                  MakeRow(SidebarSection::kToday, id, 1)};
  rows[0].is_active = true;
  rows[1].is_active = true;

  GroupSplitRows(rows, /*active_tab_index=*/1);

  EXPECT_FALSE(rows[0].is_active);
  EXPECT_TRUE(rows[1].is_active);
}

TEST(SplitRowsTest, ALinkedPairWithNoTabsIsStillOneRow) {
  const EntryId a = EntryId::Generate();
  const EntryId b = EntryId::Generate();
  const EntryId c = EntryId::Generate();
  std::vector<SidebarRow> rows = {EntryRow(a, b), EntryRow(c, EntryId()),
                                  EntryRow(b, a)};

  GroupSplitRows(rows, /*active_tab_index=*/-1);

  ASSERT_EQ(3u, rows.size());
  EXPECT_EQ(a, rows[0].entry_id);
  EXPECT_EQ(b, rows[1].entry_id);
  EXPECT_EQ(c, rows[2].entry_id);
  EXPECT_TRUE(rows[0].split_joins_next);
  EXPECT_TRUE(rows[1].split_joins_previous);
  EXPECT_FALSE(rows[2].split_joins_previous);
}

TEST(SplitRowsTest, AFavouriteIsNeverJoinedAndKeepsItsMark) {
  const split_tabs::SplitTabId id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SidebarRow> rows = {MakeRow(SidebarSection::kFavorites, id, 0),
                                  MakeRow(SidebarSection::kToday, id, 1)};

  GroupSplitRows(rows, /*active_tab_index=*/0);

  EXPECT_EQ((std::vector<int>{0, 1}), TabIndices(rows));
  EXPECT_FALSE(rows[0].split_joins_next);
  EXPECT_FALSE(rows[1].split_joins_previous);
  EXPECT_EQ(SidebarSection::kToday, rows[1].DrawnSection());
}

TEST(SplitRowsTest, TwoDifferentSplitsSideBySideAreNotJoinedToEachOther) {
  std::vector<SidebarRow> rows = {
      MakeRow(SidebarSection::kToday, split_tabs::SplitTabId::GenerateNew(), 0),
      MakeRow(SidebarSection::kToday, split_tabs::SplitTabId::GenerateNew(),
              1)};

  GroupSplitRows(rows, /*active_tab_index=*/0);

  EXPECT_FALSE(rows[0].split_joins_next);
  EXPECT_FALSE(rows[1].split_joins_previous);
}

TEST(SplitRowsTest, RowsInNoSplitAreLeftAlone) {
  std::vector<SidebarRow> rows = {MakeRow(SidebarSection::kToday, {}, 0),
                                  MakeRow(SidebarSection::kToday, {}, 1)};
  rows[0].is_active = true;

  GroupSplitRows(rows, /*active_tab_index=*/0);

  EXPECT_EQ((std::vector<int>{0, 1}), TabIndices(rows));
  EXPECT_TRUE(rows[0].is_active);
  EXPECT_FALSE(rows[0].split_joins_next);
  EXPECT_FALSE(rows[1].split_joins_previous);
}

// And the same rows as the sidebar's own model builds them.
class SplitSidebarRowsTest : public BrowserWithTestWindowTest {
 protected:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    model_ =
        std::make_unique<SidebarTabModel>(strip(), &arcium_model_, &binding_);
    controller_ =
        std::make_unique<SplitController>(strip(), nullptr, model_.get());
  }

  void TearDown() override {
    controller_.reset();
    model_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

  TabStripModel* strip() { return browser()->tab_strip_model(); }
  SpaceId FirstSpace() const { return arcium_model_.spaces().front().id; }

  tabs::TabInterface* AddTab(const GURL& url) {
    return arcium::test::AddTabInSpace(strip(), profile(), url, FirstSpace());
  }

  const SidebarRow& RowForUrl(const std::vector<SidebarRow>& rows,
                              const GURL& url) {
    for (const SidebarRow& row : rows) {
      if (row.url == url) {
        return row;
      }
    }
    ADD_FAILURE() << "no row for " << url;
    return rows.front();
  }

  ArciumModel arcium_model_;
  TabBinding binding_;
  std::unique_ptr<SidebarTabModel> model_;
  std::unique_ptr<SplitController> controller_;
};

TEST_F(SplitSidebarRowsTest, ASplitIsOneJoinedPairWithTheFocusedHalfCurrent) {
  AddTab(GURL("https://a.test/"));
  AddTab(GURL("https://b.test/"));
  strip()->ActivateTabAt(0);
  ASSERT_TRUE(controller_->SplitWithActive(1));

  const std::vector<SidebarRow> rows = model_->rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(rows[0].tab_index == strip()->active_index(), rows[0].is_active);
  EXPECT_EQ(rows[1].tab_index == strip()->active_index(), rows[1].is_active);
  EXPECT_NE(rows[0].is_active, rows[1].is_active);
  EXPECT_TRUE(rows[0].split.has_value());
  EXPECT_EQ(rows[0].split, rows[1].split);
  EXPECT_TRUE(rows[0].split_joins_next);
  EXPECT_TRUE(rows[1].split_joins_previous);
}

TEST_F(SplitSidebarRowsTest, ARowOutsideTheSplitIsNotCurrent) {
  AddTab(GURL("https://a.test/"));
  AddTab(GURL("https://b.test/"));
  AddTab(GURL("https://c.test/"));
  strip()->ActivateTabAt(0);
  ASSERT_TRUE(controller_->SplitWithActive(1));

  const std::vector<SidebarRow> rows = model_->rows();
  const SidebarRow& third = RowForUrl(rows, GURL("https://c.test/"));
  EXPECT_FALSE(third.is_active);
  EXPECT_FALSE(third.split.has_value());
}

TEST_F(SplitSidebarRowsTest, EndingASplitClearsBothMarks) {
  AddTab(GURL("https://a.test/"));
  AddTab(GURL("https://b.test/"));
  strip()->ActivateTabAt(0);
  ASSERT_TRUE(controller_->SplitWithActive(1));

  controller_->Unsplit();

  for (const SidebarRow& row : model_->rows()) {
    EXPECT_FALSE(row.split.has_value());
    EXPECT_FALSE(row.split_joins_next);
    EXPECT_FALSE(row.split_joins_previous);
  }
}

}  // namespace
}  // namespace arcium
