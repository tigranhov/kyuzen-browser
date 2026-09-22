// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/split_controller.h"

#include <memory>

#include "arcium/browser/loose_page.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/test/space_test_util.h"
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
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    switcher_ =
        std::make_unique<SpaceSwitcher>(strip(), &arcium_model_, &binding_);
    controller_ = std::make_unique<SplitController>(strip(), switcher_.get());
  }

  void TearDown() override {
    controller_.reset();
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

}  // namespace
}  // namespace arcium
