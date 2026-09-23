// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/split_controller.h"

#include <memory>
#include <optional>

#include "arcium/browser/loose_page.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/test/space_test_util.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class SplitControllerTest : public BrowserWithTestWindowTest {
 protected:
  SplitControllerTest()
      : BrowserWithTestWindowTest(
            content::BrowserTaskEnvironment::TimeSource::MOCK_TIME) {}

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    switcher_ =
        std::make_unique<SpaceSwitcher>(strip(), &arcium_model_, &binding_);
    model_ = std::make_unique<SidebarTabModel>(strip(), &arcium_model_,
                                               &binding_, switcher_.get());
    controller_ = std::make_unique<SplitController>(strip(), switcher_.get(),
                                                    model_.get());
  }

  void TearDown() override {
    controller_.reset();
    model_.reset();
    switcher_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

  TabStripModel* strip() { return browser()->tab_strip_model(); }
  SplitController& controller() { return *controller_; }
  SpaceId FirstSpace() const { return arcium_model_.spaces().front().id; }

  tabs::TabInterface* AddTab(const GURL& url, SpaceId space) {
    return arcium::test::AddTabInSpace(strip(), profile(), url, space);
  }

  ArciumModel arcium_model_;
  TabBinding binding_;
  std::unique_ptr<SpaceSwitcher> switcher_;
  std::unique_ptr<SidebarTabModel> model_;
  std::unique_ptr<SplitController> controller_;
};

TEST_F(SplitControllerTest, TwoTabsOfOneSpaceMayShareTheScreen) {
  AddTab(GURL("https://a.test/"), FirstSpace());
  AddTab(GURL("https://b.test/"), FirstSpace());

  EXPECT_TRUE(controller().CanSplit(0, 1));
}

TEST_F(SplitControllerTest, TabsOfDifferentSpacesMayNot) {
  const SpaceId work = arcium_model_.AddSpace(u"Work");
  AddTab(GURL("https://a.test/"), FirstSpace());
  AddTab(GURL("https://b.test/"), work);

  EXPECT_FALSE(controller().CanSplit(0, 1));
}

TEST_F(SplitControllerTest, ATabMayNotShareTheScreenWithItself) {
  AddTab(GURL("https://a.test/"), FirstSpace());

  EXPECT_FALSE(controller().CanSplit(0, 0));
}

TEST_F(SplitControllerTest, ALoosePageMayNotShareTheScreen) {
  AddTab(GURL("https://a.test/"), FirstSpace());
  tabs::TabInterface* const peek =
      AddTab(GURL("https://peek.test/"), FirstSpace());
  SetLoosePageKind(peek->GetContents(), LoosePageKind::kPeek);

  EXPECT_FALSE(controller().CanSplit(0, 1));
}

TEST_F(SplitControllerTest, ATabAlreadySharingMayNotShareAgain) {
  AddTab(GURL("https://a.test/"), FirstSpace());
  AddTab(GURL("https://b.test/"), FirstSpace());
  AddTab(GURL("https://c.test/"), FirstSpace());
  strip()->ActivateTabAt(0);
  ASSERT_TRUE(controller().SplitWithActive(1));

  EXPECT_FALSE(controller().CanSplit(0, 2));
  EXPECT_FALSE(controller().CanSplit(1, 2));
}

TEST_F(SplitControllerTest, SplittingPutsBothTabsInFront) {
  AddTab(GURL("https://a.test/"), FirstSpace());
  AddTab(GURL("https://b.test/"), FirstSpace());
  strip()->ActivateTabAt(0);

  ASSERT_TRUE(controller().SplitWithActive(1));

  EXPECT_EQ(2u, strip()->GetForegroundTabs().size());
  EXPECT_TRUE(controller().ActiveIsSplit());
}

TEST_F(SplitControllerTest, SplittingIsRefusedAcrossSpacesAndChangesNothing) {
  const SpaceId work = arcium_model_.AddSpace(u"Work");
  AddTab(GURL("https://a.test/"), FirstSpace());
  AddTab(GURL("https://b.test/"), work);
  strip()->ActivateTabAt(0);

  EXPECT_FALSE(controller().SplitWithActive(1));

  EXPECT_FALSE(controller().ActiveIsSplit());
  EXPECT_EQ(1u, strip()->GetForegroundTabs().size());
}

TEST_F(SplitControllerTest, UnsplittingLeavesBothTabsOpen) {
  AddTab(GURL("https://a.test/"), FirstSpace());
  AddTab(GURL("https://b.test/"), FirstSpace());
  strip()->ActivateTabAt(0);
  ASSERT_TRUE(controller().SplitWithActive(1));

  controller().Unsplit();

  EXPECT_FALSE(controller().ActiveIsSplit());
  EXPECT_EQ(2, strip()->count());
}

TEST_F(SplitControllerTest, SplittingWithAColdEntryOpensItsPageFirst) {
  const EntryId cold = arcium_model_.AddEntryForTesting(
      EntryKind::kPinned, GURL("https://pinned.test/"), u"Pinned");
  AddTab(GURL("https://a.test/"), FirstSpace());
  strip()->ActivateTabAt(0);
  ASSERT_EQ(1, strip()->count());

  EXPECT_TRUE(controller().SplitWithActive(cold));

  EXPECT_EQ(2, strip()->count());
  EXPECT_EQ(2u, strip()->GetForegroundTabs().size());
}

TEST_F(SplitControllerTest, ADroppedRowTakesTheSideItWasDroppedOn) {
  AddTab(GURL("https://a.test/"), FirstSpace());
  tabs::TabInterface* const dragged =
      AddTab(GURL("https://b.test/"), FirstSpace());
  strip()->ActivateTabAt(0);

  // Dropped on the left half, so the dragged page is the left pane -- which
  // is the lower of the two strip indices.
  ASSERT_TRUE(controller().SplitWithActive(1, /*on_right=*/false));

  EXPECT_EQ(0, strip()->GetIndexOfTab(dragged));
}

TEST_F(SplitControllerTest, ADroppedRowTakesTheRightSideToo) {
  AddTab(GURL("https://a.test/"), FirstSpace());
  tabs::TabInterface* const dragged =
      AddTab(GURL("https://b.test/"), FirstSpace());
  strip()->ActivateTabAt(0);

  ASSERT_TRUE(controller().SplitWithActive(1, /*on_right=*/true));

  EXPECT_EQ(1, strip()->GetIndexOfTab(dragged));
}

TEST_F(SplitControllerTest, TheKeyboardSplitsWithTheTabBeforeThisOne) {
  AddTab(GURL("https://a.test/"), FirstSpace());
  AddTab(GURL("https://b.test/"), FirstSpace());
  AddTab(GURL("https://c.test/"), FirstSpace());
  strip()->ActivateTabAt(1);
  strip()->ActivateTabAt(2);  // b is now the tab before this one.

  controller().ToggleSplitWithPrevious();

  EXPECT_TRUE(controller().ActiveIsSplit());
  EXPECT_EQ(2u, strip()->GetForegroundTabs().size());
  EXPECT_TRUE(
      strip()
          ->GetSplitForTab(strip()->GetIndexOfTab(strip()->GetTabAtIndex(1)))
          .has_value());
}

TEST_F(SplitControllerTest, TheKeyboardEndsTheSplitItIsShowing) {
  AddTab(GURL("https://a.test/"), FirstSpace());
  AddTab(GURL("https://b.test/"), FirstSpace());
  strip()->ActivateTabAt(0);
  strip()->ActivateTabAt(1);
  controller().ToggleSplitWithPrevious();
  ASSERT_TRUE(controller().ActiveIsSplit());

  controller().ToggleSplitWithPrevious();

  EXPECT_FALSE(controller().ActiveIsSplit());
  EXPECT_EQ(2, strip()->count());
}

TEST_F(SplitControllerTest, TheKeyboardDoesNothingWithNoTabBeforeThisOne) {
  AddTab(GURL("https://a.test/"), FirstSpace());
  strip()->ActivateTabAt(0);

  controller().ToggleSplitWithPrevious();

  EXPECT_FALSE(controller().ActiveIsSplit());
  EXPECT_EQ(1, strip()->count());
}

TEST_F(SplitControllerTest, EndingASplitByIndexWorksFromEitherHalf) {
  AddTab(GURL("https://a.test/"), FirstSpace());
  AddTab(GURL("https://b.test/"), FirstSpace());
  strip()->ActivateTabAt(0);
  ASSERT_TRUE(controller().SplitWithActive(1));

  // The half that is not the active tab, which is what a move out of the
  // space names.
  controller().EndSplitFor(1);

  EXPECT_FALSE(strip()->GetSplitForTab(0).has_value());
  EXPECT_FALSE(strip()->GetSplitForTab(1).has_value());
  EXPECT_EQ(2, strip()->count());
}

// The same window with the model a split is written into, which the tests
// above leave out so that nothing they do is also a save.
class SplitRecordTest : public SplitControllerTest,
                        public ArciumModel::Observer {
 protected:
  void SetUp() override {
    SplitControllerTest::SetUp();
    controller_ = std::make_unique<SplitController>(
        strip(), switcher_.get(), model_.get(), &arcium_model_);
    arcium_model_.AddObserver(this);
  }

  void TearDown() override {
    arcium_model_.RemoveObserver(this);
    SplitControllerTest::TearDown();
  }

  // ArciumModel::Observer: every change is also a save scheduled and a
  // sidebar rebuilt, which is what makes the count worth keeping.
  void OnArciumModelChanged() override { ++changes_; }

  const std::optional<SpaceSplit>& Recorded() const {
    return arcium_model_.GetSpace(FirstSpace())->split;
  }

  split_tabs::SplitTabId SplitOfFirstTwo() {
    AddTab(GURL("https://a.test/"), FirstSpace());
    AddTab(GURL("https://b.test/"), FirstSpace());
    strip()->ActivateTabAt(0);
    CHECK(controller().SplitWithActive(1));
    return strip()->GetTabAtIndex(0)->GetSplit().value();
  }

  int changes_ = 0;
};

TEST_F(SplitRecordTest, ASplitIsWrittenDownAsSoonAsItForms) {
  SplitOfFirstTwo();

  ASSERT_TRUE(Recorded().has_value());
  EXPECT_EQ(0.5, Recorded()->ratio);
}

// Dragging the divider reports every step of the drag. Writing each one down
// rebuilt the sidebar and scheduled a save per step, and only where the
// divider is let go is worth keeping -- which is also all Chromium's own
// session file keeps.
TEST_F(SplitRecordTest, TheDividerIsWrittenDownOnceItIsLetGo) {
  const split_tabs::SplitTabId id = SplitOfFirstTwo();
  changes_ = 0;

  strip()->UpdateSplitRatio(id, 0.4, /*is_intermediate=*/true);
  strip()->UpdateSplitRatio(id, 0.35, /*is_intermediate=*/true);
  strip()->UpdateSplitRatio(id, 0.3, /*is_intermediate=*/true);

  EXPECT_EQ(0, changes_);
  EXPECT_EQ(0.5, Recorded()->ratio);

  // Let go where the last step already was: Chromium drops that update as a
  // change of nothing, so the pause is what writes the drag down.
  strip()->UpdateSplitRatio(id, 0.3, /*is_intermediate=*/false);
  task_environment()->FastForwardBy(SplitController::kDividerSettle);

  EXPECT_EQ(1, changes_);
  EXPECT_EQ(0.3, Recorded()->ratio);
}

TEST_F(SplitRecordTest, LettingGoSomewhereNewIsWrittenDownAtOnce) {
  const split_tabs::SplitTabId id = SplitOfFirstTwo();
  strip()->UpdateSplitRatio(id, 0.4, /*is_intermediate=*/true);

  strip()->UpdateSplitRatio(id, 0.35, /*is_intermediate=*/false);

  EXPECT_EQ(0.35, Recorded()->ratio);
  changes_ = 0;
  task_environment()->FastForwardBy(SplitController::kDividerSettle);
  EXPECT_EQ(0, changes_) << "the drag was written down a second time";
}

}  // namespace
}  // namespace arcium
