// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// What reaches the session file, and what a launch keeps. A tab's space tag
// and key are written only when the session service rebuilds its command
// list, so the switcher has to ask for a rebuild whenever it gives a live tab
// a tag or a key the file cannot have yet. And the first window's switcher is
// built before the model file has been read, so the fallback that runs once
// it has must keep the tab session restore selected.

#include <memory>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/test/space_test_util.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/test/bind.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/unload_controller.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class SpaceSessionTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  // A switcher whose rebuild requests are counted rather than sent.
  std::unique_ptr<SpaceSwitcher> MakeSwitcher() {
    auto switcher =
        std::make_unique<SpaceSwitcher>(strip(), &model_, &binding_);
    switcher->SetSessionRebuildRequestForTesting(
        base::BindLambdaForTesting([this] { ++requests_; }));
    return switcher;
  }

  tabs::TabInterface* AddTabInSpace(const GURL& url, SpaceId space) {
    return arcium::test::AddTabInSpace(strip(), profile(), url, space);
  }

  // A tab with no tag and no opener, appended behind the active one: a link
  // from another application, before the switcher has said where it goes.
  content::WebContents* AppendUntaggedTab() {
    std::unique_ptr<content::WebContents> contents =
        content::WebContentsTester::CreateTestWebContents(profile(), nullptr);
    content::WebContents* raw = contents.get();
    strip()->AppendWebContents(std::move(contents), /*foreground=*/false);
    return raw;
  }

  // Declines closes of `tab` only, the way a page behind an unanswered
  // beforeunload dialog stays open. See SpaceSwitcherTest's own HoldTabOpen.
  arcium::test::DecliningUnloadHandler* HoldTabOpen(tabs::TabInterface* tab) {
    auto handler = std::make_unique<arcium::test::DecliningUnloadHandler>();
    handler->set_target(tab->GetContents());
    arcium::test::DecliningUnloadHandler* handler_ptr = handler.get();
    UnloadController::From(browser())->AddTabUnloadHandler(std::move(handler));
    return handler_ptr;
  }

  ArciumModel model_;
  TabBinding binding_;
  int requests_ = 0;
};

TEST_F(SpaceSessionTest, MovingATabToAnotherSpaceAsksForARebuild) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://a2.example/"), first);
  auto switcher = MakeSwitcher();
  requests_ = 0;

  switcher->MoveTabToSpace(1, work);
  EXPECT_EQ(1, requests_);
}

// An earlier rebuild may have written this tab as another space, and a tab
// written that way restores there unless its tag is written again -- so a
// move into the first space asks as well, unlike a first-space insert.
TEST_F(SpaceSessionTest, MovingATabIntoTheFirstSpaceAsksForARebuildToo) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  auto switcher = MakeSwitcher();
  requests_ = 0;

  switcher->MoveTabToSpace(1, first);
  EXPECT_EQ(1, requests_);
}

TEST_F(SpaceSessionTest, MovingAnEntrysTabAsksForARebuild) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  tabs::TabInterface* a2 = AddTabInSpace(GURL("https://a2.example/"), first);
  const EntryId pin = model_.AddEntry(first, EntryKind::kPinned,
                                      GURL("https://a2.example/"), u"A2");
  binding_.Bind(pin, a2->GetHandle());
  auto switcher = MakeSwitcher();
  requests_ = 0;

  switcher->MoveEntryToSpace(pin, work);
  EXPECT_EQ(1, requests_);
}

TEST_F(SpaceSessionTest, ATabADeleteRetagsAsksForARebuild) {
  const SpaceId first = model_.default_space_id();
  const SpaceId doomed = model_.AddSpace(u"Doomed");
  AddTabInSpace(GURL("https://a1.example/"), first);
  tabs::TabInterface* d1 = AddTabInSpace(GURL("https://d1.example/"), doomed);
  auto switcher = MakeSwitcher();
  arcium::test::DecliningUnloadHandler* handler = HoldTabOpen(d1);
  arcium::test::ScopedUnloadHandlerRelease release(handler);
  requests_ = 0;

  switcher->DeleteSpace(doomed);
  ASSERT_EQ(2, strip()->count());  // d1 refused to close.
  EXPECT_EQ(first, SpaceTagOf(d1->GetContents()));
  EXPECT_EQ(1, requests_);
}

TEST_F(SpaceSessionTest, InsertingATabIntoAnotherSpaceAsksForARebuild) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  auto switcher = MakeSwitcher();
  switcher->SwitchTo(work);
  requests_ = 0;

  content::WebContents* added = AppendUntaggedTab();
  EXPECT_EQ(work, SpaceTagOf(added));
  EXPECT_EQ(1, requests_);
}

// A tab no rebuild has written restores into the first space already, so
// tagging a new tab with the first space leaves nothing to write.
TEST_F(SpaceSessionTest, InsertingATabIntoTheFirstSpaceAsksForNothing) {
  const SpaceId first = model_.default_space_id();
  model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  auto switcher = MakeSwitcher();
  requests_ = 0;

  content::WebContents* added = AppendUntaggedTab();
  EXPECT_EQ(first, SpaceTagOf(added));
  EXPECT_EQ(0, requests_);
}

// Both tabs arrive before the switcher exists, so neither has been given a
// key; activating one is what gives it one.
TEST_F(SpaceSessionTest, MintingATabsKeyAsksForARebuild) {
  const SpaceId first = model_.default_space_id();
  AddTabInSpace(GURL("https://a1.example/"), first);
  tabs::TabInterface* a2 = AddTabInSpace(GURL("https://a2.example/"), first);
  auto switcher = MakeSwitcher();
  ASSERT_FALSE(ExistingKeyOf(a2->GetContents()).is_valid());

  strip()->ActivateTabAt(1);
  EXPECT_TRUE(ExistingKeyOf(a2->GetContents()).is_valid());
  EXPECT_EQ(1, requests_);
}

TEST_F(SpaceSessionTest, ReactivatingATabThatHasAKeyAsksForNothing) {
  const SpaceId first = model_.default_space_id();
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://a2.example/"), first);
  auto switcher = MakeSwitcher();
  strip()->ActivateTabAt(1);
  strip()->ActivateTabAt(0);
  requests_ = 0;

  strip()->ActivateTabAt(1);
  EXPECT_EQ(0, requests_);
}

// The launch order. The first window's switcher is built on the placeholder
// model, because the model file is still being read; session restore then
// inserts its tabs and selects one; only then does the file replace the
// placeholder -- ReplaceAll, then the space that was active at quit, exactly
// as the serializer does it. Restore's selection is where the user was at
// quit, so the fallback that follows keeps it rather than switching, which
// would land on whatever the space remembers -- here nothing, as in a
// profile's first launch after the upgrade, so its first open tab.
TEST_F(SpaceSessionTest, ALaunchKeepsTheTabSessionRestoreSelected) {
  auto switcher = MakeSwitcher();
  Space first;
  first.id = SpaceId::Generate();
  first.name = u"First";
  first.position = 0;
  Space work;
  work.id = SpaceId::Generate();
  work.name = u"Work";
  work.position = 1;
  AddTabInSpace(GURL("https://w1.example/"), work.id);
  AddTabInSpace(GURL("https://w2.example/"), work.id);
  strip()->ActivateTabAt(1);

  model_.ReplaceAll({first, work}, {}, {});
  model_.SetLastActiveSpace(work.id);
  task_environment()->RunUntilIdle();

  EXPECT_EQ(work.id, switcher->active_space());
  EXPECT_EQ(2, strip()->count());
  EXPECT_EQ(1, strip()->active_index());
}

}  // namespace
}  // namespace arcium
