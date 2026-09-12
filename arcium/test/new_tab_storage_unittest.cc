// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/new_tab_storage.h"

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

class NewTabStorageTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  ArciumModel model_;
  TabBinding binding_;
};

// The rule TagInsertedTabs applies at insertion, applied at creation:
// a tab's storage is fixed when it is created and can never change, so the
// space has to be known by then.
TEST_F(NewTabStorageTest, ANewTabJoinsItsSourceTabsSpaceNotTheOneOnScreen) {
  const SpaceId work = model_.AddSpace(u"Work");
  SpaceSwitcher switcher(strip(), &model_, &binding_);
  tabs::TabInterface* source = arcium::test::AddTabInSpace(
      strip(), profile(), GURL("https://a.example/"), work);
  switcher.SwitchTo(model_.default_space_id());
  EXPECT_EQ(work,
            SpaceForNewTab(model_, binding_, &switcher, source->GetContents()));
}

TEST_F(NewTabStorageTest, ANewTabFromNowhereJoinsTheSpaceOnScreen) {
  const SpaceId work = model_.AddSpace(u"Work");
  SpaceSwitcher switcher(strip(), &model_, &binding_);
  switcher.SwitchTo(work);
  EXPECT_EQ(work, SpaceForNewTab(model_, binding_, &switcher, nullptr));
}

// --arcium-no-sidebar, and every browser test that does not want one.
TEST_F(NewTabStorageTest, AWindowWithoutASidebarDecidesNothing) {
  EXPECT_FALSE(SpaceForNewTab(model_, binding_, nullptr, nullptr).is_valid());
}

}  // namespace
}  // namespace arcium
