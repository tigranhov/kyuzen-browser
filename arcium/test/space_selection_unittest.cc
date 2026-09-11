// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/tab_selection.h"

#include <memory>
#include <optional>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/test/space_test_util.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class SpaceSelectionTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  std::unique_ptr<SpaceSwitcher> MakeSwitcher() {
    return std::make_unique<SpaceSwitcher>(strip(), &model_, &binding_);
  }

  // Appends a tab already tagged with `space`, so the strip ends up in the
  // order the test wrote its calls and every tab is in the space it names.
  tabs::TabInterface* AddTabInSpace(const GURL& url, SpaceId space) {
    return arcium::test::AddTabInSpace(strip(), profile(), url, space);
  }

  ArciumModel model_;
  TabBinding binding_;
};

TEST_F(SpaceSelectionTest, ClosingTheActiveTabNeverLandsInAnotherSpace) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0
  AddTabInSpace(GURL("https://w1.example/"), work);   // 1
  AddTabInSpace(GURL("https://a2.example/"), first);  // 2
  strip()->ActivateTabAt(0);
  // Chromium's own pick after closing 0 is index 0, which is the foreign
  // tab once the strip has shifted.
  EXPECT_EQ(1, NextSelectedIndexInSpace(strip(), std::optional<int>(0), 0, 1));
}

TEST_F(SpaceSelectionTest, ChromiumsPickIsKeptWhenItIsInTheSpace) {
  const SpaceId first = model_.default_space_id();
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://a2.example/"), first);
  EXPECT_EQ(0, NextSelectedIndexInSpace(strip(), std::optional<int>(0), 0, 1));
}

// Chromium asks on every removal but reads the answer only when the active
// tab is among the tabs going. A removal that spares it -- a background
// space's tabs closing during a delete -- gets Chromium's own answer back,
// not the result of a search nobody reads.
TEST_F(SpaceSelectionTest, ARemovalThatSparesTheActiveTabKeepsChromiumsAnswer) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0, active
  AddTabInSpace(GURL("https://w1.example/"), work);   // 1
  AddTabInSpace(GURL("https://a2.example/"), first);  // 2
  AddTabInSpace(GURL("https://w2.example/"), work);   // 3
  ASSERT_EQ(0, strip()->active_index());
  // Removing w2; Chromium's answer, index 1, is w1 -- another space's tab,
  // which a search would have replaced with a2.
  EXPECT_EQ(1, NextSelectedIndexInSpace(strip(), std::optional<int>(1), 3, 1));
}

TEST_F(SpaceSelectionTest, ASpaceWithNoOtherTabKeepsChromiumsAnswer) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://a1.example/"), first);
  switcher->SwitchTo(work);
  // A close Arcium did not see, such as a script closing its own popup.
  // Landing on a foreign tab is accepted here, and the switcher then adopts
  // that tab's space.
  EXPECT_EQ(0, NextSelectedIndexInSpace(strip(), std::optional<int>(0), 0, 1));
}

TEST_F(SpaceSelectionTest, AnEmptyAnswerStaysEmpty) {
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), model_.default_space_id());
  EXPECT_FALSE(
      NextSelectedIndexInSpace(strip(), std::nullopt, 0, 1).has_value());
}

TEST_F(SpaceSelectionTest, WithoutASwitcherChromiumsAnswerIsUntouched) {
  AddTab(browser(), GURL("https://a1.example/"));
  AddTab(browser(), GURL("https://a2.example/"));
  AddTab(browser(), GURL("https://a3.example/"));
  EXPECT_EQ(1, NextSelectedIndexInSpace(strip(), std::optional<int>(1), 0, 1));
}

// A group or a split leaves the strip as one block, so every index past it
// moves by the block's length, not by one.
TEST_F(SpaceSelectionTest, ABlockOfTabsShiftsTheStripByItsLength) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0
  AddTabInSpace(GURL("https://b1.example/"), first);  // 1, in the block
  AddTabInSpace(GURL("https://b2.example/"), first);  // 2, in the block
  AddTabInSpace(GURL("https://w1.example/"), work);   // 3
  AddTabInSpace(GURL("https://a2.example/"), first);  // 4
  strip()->ActivateTabAt(1);
  // Chromium's pick is index 1 after the block of two at 1 has gone: the
  // foreign tab now at 3. The space's nearest tab to the right is a2, which
  // lands at 2.
  EXPECT_EQ(2, NextSelectedIndexInSpace(strip(), std::optional<int>(1), 1, 2));
}

// Through the strip's own close, with the Browser's real delegate: the
// window stays on the space it was showing rather than following Chromium's
// pick into another one.
TEST_F(SpaceSelectionTest, ClosingTheActiveTabThroughTheStripStaysInTheSpace) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0
  AddTabInSpace(GURL("https://w1.example/"), work);   // 1
  AddTabInSpace(GURL("https://a2.example/"), first);  // 2
  strip()->ActivateTabAt(0);
  ASSERT_EQ(first, switcher->active_space());

  strip()->CloseWebContentsAt(0, TabCloseTypes::CLOSE_NONE);

  ASSERT_EQ(2, strip()->count());
  EXPECT_EQ(GURL("https://a2.example/"),
            strip()->GetActiveTab()->GetContents()->GetVisibleURL());
  EXPECT_EQ(first, switcher->active_space());
}

}  // namespace
}  // namespace arcium
