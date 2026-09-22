// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/split_rows.h"

#include <memory>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/test/space_test_util.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/browser/split_controller.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/tab_interface.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

// The join rule on its own, with no tab strip: rows in, flags out.
SidebarRow MakeRow(SidebarSection section,
                   std::optional<split_tabs::SplitTabId> split) {
  SidebarRow row;
  row.section = section;
  row.split = split;
  return row;
}

TEST(SplitRowsTest, NeighbouringHalvesAreJoined) {
  const split_tabs::SplitTabId id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SidebarRow> rows = {MakeRow(SidebarSection::kToday, id),
                                  MakeRow(SidebarSection::kToday, id)};

  MarkSplitNeighbours(rows);

  EXPECT_TRUE(rows[0].split_joins_next);
  EXPECT_TRUE(rows[1].split_joins_previous);
  EXPECT_FALSE(rows[0].split_joins_previous);
  EXPECT_FALSE(rows[1].split_joins_next);
}

TEST(SplitRowsTest, HalvesInDifferentSectionsAreNotJoined) {
  const split_tabs::SplitTabId id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SidebarRow> rows = {MakeRow(SidebarSection::kPinned, id),
                                  MakeRow(SidebarSection::kToday, id)};

  MarkSplitNeighbours(rows);

  EXPECT_FALSE(rows[0].split_joins_next);
  EXPECT_FALSE(rows[1].split_joins_previous);
}

TEST(SplitRowsTest, HalvesInDifferentFoldersAreNotJoined) {
  const split_tabs::SplitTabId id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SidebarRow> rows = {MakeRow(SidebarSection::kPinned, id),
                                  MakeRow(SidebarSection::kPinned, id)};
  rows[0].folder_id = FolderId::Generate();

  MarkSplitNeighbours(rows);

  EXPECT_FALSE(rows[0].split_joins_next);
  EXPECT_FALSE(rows[1].split_joins_previous);
}

TEST(SplitRowsTest, FavouritesAreNeverJoined) {
  // A grid's neighbours are above and beside, so a bar down one edge would
  // name the wrong pair.
  const split_tabs::SplitTabId id = split_tabs::SplitTabId::GenerateNew();
  std::vector<SidebarRow> rows = {MakeRow(SidebarSection::kFavorites, id),
                                  MakeRow(SidebarSection::kFavorites, id)};

  MarkSplitNeighbours(rows);

  EXPECT_FALSE(rows[0].split_joins_next);
  EXPECT_FALSE(rows[1].split_joins_previous);
}

TEST(SplitRowsTest, TwoDifferentSplitsSideBySideAreNotJoinedToEachOther) {
  std::vector<SidebarRow> rows = {
      MakeRow(SidebarSection::kToday, split_tabs::SplitTabId::GenerateNew()),
      MakeRow(SidebarSection::kToday, split_tabs::SplitTabId::GenerateNew())};

  MarkSplitNeighbours(rows);

  EXPECT_FALSE(rows[0].split_joins_next);
  EXPECT_FALSE(rows[1].split_joins_previous);
}

TEST(SplitRowsTest, RowsInNoSplitAreLeftAlone) {
  std::vector<SidebarRow> rows = {
      MakeRow(SidebarSection::kToday, std::nullopt),
      MakeRow(SidebarSection::kToday, std::nullopt)};

  MarkSplitNeighbours(rows);

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

TEST_F(SplitSidebarRowsTest, BothHalvesOfASplitAreCurrent) {
  AddTab(GURL("https://a.test/"));
  AddTab(GURL("https://b.test/"));
  strip()->ActivateTabAt(0);
  ASSERT_TRUE(controller_->SplitWithActive(1));

  const std::vector<SidebarRow> rows = model_->rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_TRUE(rows[0].is_active);
  EXPECT_TRUE(rows[1].is_active);
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
