// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/tab_space.h"

#include <map>
#include <string>

#include "arcium/browser/entry_claim.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/tab_binding.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class TabSpaceTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  ArciumModel model_;
  TabBinding binding_;
};

TEST_F(TabSpaceTest, ATabWithNoTagIsInTheFirstSpace) {
  AddTab(browser(), GURL("https://a.example/"));
  model_.AddSpace(u"Work");
  EXPECT_EQ(
      model_.default_space_id(),
      SpaceOfTab(model_, binding_, strip()->GetTabAtIndex(0)->GetHandle()));
}

TEST_F(TabSpaceTest, ATaggedTabIsInTheTaggedSpace) {
  AddTab(browser(), GURL("https://a.example/"));
  const SpaceId work = model_.AddSpace(u"Work");
  SetSpaceTag(strip()->GetTabAtIndex(0)->GetContents(), work);
  EXPECT_EQ(work, SpaceOfTab(model_, binding_,
                             strip()->GetTabAtIndex(0)->GetHandle()));
}

TEST_F(TabSpaceTest, ATagNamingNoSpaceFallsBackToTheFirst) {
  AddTab(browser(), GURL("https://a.example/"));
  SetSpaceTag(strip()->GetTabAtIndex(0)->GetContents(), SpaceId::Generate());
  EXPECT_EQ(
      model_.default_space_id(),
      SpaceOfTab(model_, binding_, strip()->GetTabAtIndex(0)->GetHandle()));
}

// The entry wins over the tag, which is what lets "move a pin to another
// space" carry its open tab without touching the tab at all.
TEST_F(TabSpaceTest, AClaimedTabIsInItsEntrysSpaceWhateverItsTagSays) {
  AddTab(browser(), GURL("https://a.example/"));
  const SpaceId work = model_.AddSpace(u"Work");
  const EntryId id = model_.AddEntry(work, EntryKind::kPinned,
                                     GURL("https://a.example/"), u"A");
  tabs::TabInterface* tab = strip()->GetTabAtIndex(0);
  SetSpaceTag(tab->GetContents(), model_.default_space_id());
  binding_.Bind(id, tab->GetHandle());
  EXPECT_EQ(work, SpaceOfTab(model_, binding_, tab->GetHandle()));
}

TEST_F(TabSpaceTest, ATabsKeyIsGeneratedOnceAndKept) {
  AddTab(browser(), GURL("https://a.example/"));
  content::WebContents* contents = strip()->GetTabAtIndex(0)->GetContents();
  const TabKey first = KeyOf(contents);
  EXPECT_TRUE(first.is_valid());
  EXPECT_EQ(first, KeyOf(contents));
}

// ExistingKeyOf never generates: a fresh tab reads as no key at all, and only
// after something calls the generating KeyOf does the two agree.
TEST_F(TabSpaceTest, ExistingKeyOfNeverGenerates) {
  AddTab(browser(), GURL("https://a.example/"));
  content::WebContents* contents = strip()->GetTabAtIndex(0)->GetContents();
  EXPECT_FALSE(ExistingKeyOf(contents).is_valid());
  const TabKey key = KeyOf(contents);
  EXPECT_EQ(key, ExistingKeyOf(contents));
}

// A tab bound to an entry of another space is claimed. Stage 2's predicate
// said otherwise because one window only ever drew one space; a window now
// draws the space it is in, and SpaceOfTab is what decides where the tab is
// drawn.
TEST_F(TabSpaceTest, AnEntryOfAnotherSpaceStillClaimsItsTab) {
  AddTab(browser(), GURL("https://a.example/"));
  const SpaceId work = model_.AddSpace(u"Work");
  const EntryId id = model_.AddEntry(work, EntryKind::kPinned,
                                     GURL("https://a.example/"), u"A");
  tabs::TabInterface* tab = strip()->GetTabAtIndex(0);
  binding_.Bind(id, tab->GetHandle());
  EXPECT_TRUE(IsClaimedByEntry(model_, binding_, tab->GetHandle()));
}

// Restore creates a tab before the model file has been read, so the space
// id alone could not say which storage the tab belongs in.
TEST_F(TabSpaceTest, TheSessionRecordsTheProfileOfTheTabsSpace) {
  AddTab(browser(), GURL("https://a.example/"));
  const ProfileId work_profile = model_.AddProfile(u"Work", 1);
  const SpaceId work = model_.AddSpace(u"Work", work_profile);
  SetSpaceTag(strip()->GetTabAtIndex(0)->GetContents(), work);
  std::map<std::string, std::string> extra_data;
  PopulateTabSpaceExtraData(strip()->GetTabAtIndex(0), model_, binding_,
                            &extra_data);
  EXPECT_EQ(work_profile.value(), extra_data[kProfileIdExtraDataKey]);
  EXPECT_EQ(work_profile, ProfileIdFromExtraData(extra_data));
}

// A session written before profiles existed, or a hand-edited one.
TEST_F(TabSpaceTest, ASessionWithoutAReadableProfileMeansDefault) {
  EXPECT_EQ(DefaultProfileId(), ProfileIdFromExtraData({}));
  EXPECT_EQ(DefaultProfileId(),
            ProfileIdFromExtraData({{kProfileIdExtraDataKey, "Work"}}));
}

}  // namespace
}  // namespace arcium
