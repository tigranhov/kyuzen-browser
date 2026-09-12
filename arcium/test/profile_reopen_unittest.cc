// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/profile_reopen.h"

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/browser/restored_tab_loading.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/test/space_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

const ProfileId kWork =
    ProfileId::FromString("22222222-2222-4222-8222-222222222222");

class ProfileReopenTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  // A tab with two pages behind it, tagged with `space`.
  tabs::TabInterface* AddVisitedTab(SpaceId space) {
    tabs::TabInterface* tab = arcium::test::AddTabInSpace(
        strip(), profile(), GURL("https://a.example/one"), space);
    content::WebContentsTester::For(tab->GetContents())
        ->NavigateAndCommit(GURL("https://a.example/two"));
    return tab;
  }

  ArciumModel model_;
  TabBinding binding_;
};

TEST_F(ProfileReopenTest, ThePageAndTheWayBackToItComeWith) {
  tabs::TabInterface* tab = AddVisitedTab(model_.default_space_id());
  ReopenTabInProfile(strip(), strip()->GetIndexOfTab(tab), kWork);

  content::WebContents* reopened = strip()->GetWebContentsAt(0);
  content::NavigationController& controller = reopened->GetController();
  EXPECT_EQ(2, controller.GetEntryCount());
  EXPECT_EQ(1, controller.GetLastCommittedEntryIndex());
  EXPECT_EQ(GURL("https://a.example/two"),
            controller.GetLastCommittedEntry()->GetVirtualURL());
  EXPECT_EQ(GURL("https://a.example/one"),
            controller.GetEntryAtIndex(0)->GetVirtualURL());
}

TEST_F(ProfileReopenTest, TheTabLandsInTheNewProfilesStorage) {
  tabs::TabInterface* tab = AddVisitedTab(model_.default_space_id());
  ReopenTabInProfile(strip(), strip()->GetIndexOfTab(tab), kWork);
  EXPECT_EQ(PartitionDomainForProfile(kWork),
            PartitionDomainOfTab(strip()->GetWebContentsAt(0)));
}

// The swap happens inside the tab, so nothing that hangs off the tab --
// its place, its handle, the entry bound to it -- has to be rebuilt.
TEST_F(ProfileReopenTest, TheTabKeepsItsPlaceItsHandleItsEntryAndItsTag) {
  const SpaceId work_space = model_.AddSpace(u"Work");
  tabs::TabInterface* tab = AddVisitedTab(work_space);
  arcium::test::AddTabInSpace(strip(), profile(), GURL("https://b.example/"),
                              model_.default_space_id());
  const int index = strip()->GetIndexOfTab(tab);
  const tabs::TabHandle handle = tab->GetHandle();
  const TabKey key = KeyOf(tab->GetContents());
  const EntryId entry = model_.AddEntry(work_space, EntryKind::kPinned,
                                        GURL("https://a.example/one"), u"A");
  binding_.Bind(entry, handle);

  ReopenTabInProfile(strip(), index, kWork);

  EXPECT_EQ(index, strip()->GetIndexOfTab(handle.Get()));
  EXPECT_EQ(handle, strip()->GetTabAtIndex(index)->GetHandle());
  content::WebContents* reopened = strip()->GetWebContentsAt(index);
  EXPECT_EQ(work_space, SpaceTagOf(reopened));
  EXPECT_EQ(key, ExistingKeyOf(reopened));
  EXPECT_EQ(handle, binding_.TabForEntry(entry));
}

// R3.9's rule holds through a reopen: only the tab you are looking at
// loads, and it loads at once.
TEST_F(ProfileReopenTest, TheTabOnScreenLoadsAgainAndABackgroundOneWaits) {
  tabs::TabInterface* on_screen = AddVisitedTab(model_.default_space_id());
  tabs::TabInterface* behind = AddVisitedTab(model_.default_space_id());
  strip()->ActivateTabAt(strip()->GetIndexOfTab(on_screen));

  ReopenTabInProfile(strip(), strip()->GetIndexOfTab(behind), kWork);
  EXPECT_TRUE(IsTabUnloaded(strip()->GetWebContentsAt(1)));

  ReopenTabInProfile(strip(), strip()->GetIndexOfTab(on_screen), kWork);
  EXPECT_FALSE(strip()->GetWebContentsAt(0)->GetController().NeedsReload());
}

}  // namespace
}  // namespace arcium
