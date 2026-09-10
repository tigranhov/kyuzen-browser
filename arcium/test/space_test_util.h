// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_TEST_SPACE_TEST_UTIL_H_
#define ARCIUM_TEST_SPACE_TEST_UTIL_H_

#include <memory>
#include <utility>

#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/tab_space.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

// Shared across Task 4's own test and later ones (5, 8, 9): kept general and
// header-only so nobody re-derives BrowserWithTestWindowTest::AddTab's
// append-at-index-0-in-the-foreground quirk on their own.
//
// BrowserWithTestWindowTest::AddTab inserts at index 0 in the foreground, so
// a test built on it cannot both append tabs in a known order and tag them
// only after they land -- the latter also bypasses SpaceSwitcher's adoption
// rule, which only sees a tag that was already there when the tab arrived.
// Both helpers here tag or set the opener before insertion instead, and
// insert with AddWebContents/AddTab directly so the strip ends up in the
// order the caller wrote its calls, not reversed.
namespace arcium::test {

// Creates a tab already tagged with `space`, the same shape a restored tab
// arrives in, and appends it to `strip`. Tagging before insertion means
// SpaceSwitcher's never-overwrite-a-tag rule keeps this tag rather than
// retagging the tab into whichever space happens to be active.
inline tabs::TabInterface* AddTabInSpace(TabStripModel* strip,
                                         Profile* profile,
                                         const GURL& url,
                                         arcium::SpaceId space) {
  std::unique_ptr<content::WebContents> contents =
      content::WebContentsTester::CreateTestWebContents(profile, nullptr);
  arcium::SetSpaceTag(contents.get(), space);
  content::WebContents* raw_contents = contents.get();
  strip->AddWebContents(std::move(contents), -1, ui::PAGE_TRANSITION_LINK,
                        AddTabTypes::ADD_NONE);
  content::WebContentsTester::For(raw_contents)->NavigateAndCommit(url);
  return tabs::TabInterface::MaybeGetFromContents(raw_contents);
}

// Creates a tab whose strip opener is `opener` and appends it to `strip`.
// TabStripModel::AddTab treats a PAGE_TRANSITION_LINK insertion specially:
// unless ADD_FORCE_INDEX is set, it always overwrites the tab's opener with
// whichever tab is active at the moment of insertion (tab_strip_model.cc,
// InsertTabAtImpl) -- link clicks are assumed to come from the tab the user
// is looking at. ADD_FORCE_INDEX is the escape hatch the comment beside that
// code names for exactly this case ("callers aren't really handling link
// clicks"), and it is the only way to land a tab's opener outside the space
// currently on screen -- what a Cmd+click from a background space's tab
// would look like.
inline tabs::TabInterface* AddTabWithOpener(TabStripModel* strip,
                                            Profile* profile,
                                            const GURL& url,
                                            tabs::TabInterface* opener) {
  std::unique_ptr<content::WebContents> contents =
      content::WebContentsTester::CreateTestWebContents(profile, nullptr);
  content::WebContents* raw_contents = contents.get();
  auto tab = std::make_unique<tabs::TabModel>(std::move(contents), strip);
  tab->set_opener(opener);
  strip->AddTab(std::move(tab), -1, ui::PAGE_TRANSITION_LINK,
                AddTabTypes::ADD_FORCE_INDEX);
  content::WebContentsTester::For(raw_contents)->NavigateAndCommit(url);
  return tabs::TabInterface::MaybeGetFromContents(raw_contents);
}

}  // namespace arcium::test

#endif  // ARCIUM_TEST_SPACE_TEST_UTIL_H_
