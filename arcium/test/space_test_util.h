// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_TEST_SPACE_TEST_UTIL_H_
#define ARCIUM_TEST_SPACE_TEST_UTIL_H_

#include <memory>
#include <utility>

#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/tab_space.h"
#include "base/functional/callback.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/unload_controller.h"
#include "components/tabs/public/tab_interface.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"  // nogncheck
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "third_party/blink/public/mojom/frame/sudden_termination_disabler_type.mojom.h"
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

// The production seam a close can be declined at: UnloadController asks
// every registered TabUnloadHandler before it lets a tab go, and one that
// puts up its own confirmation keeps the tab open until the user answers.
// It stands in for the beforeunload dialog, which cannot be driven in these
// fixtures -- that path reaches PerformanceManager::GetGraph(), which CHECKs
// here. Shared by ArchiveServiceTest's own Clear tests and by
// SpaceSwitcherTest's HoldTabOpen, which both need a close to be declined
// without a real renderer to decline it.
// Out-of-line definitions below: a header-declared class with virtual
// methods defined inline in the class body is exactly what the Chromium
// style plugin's header-hygiene check exists to catch, since every
// translation unit that includes this header would otherwise get its own
// copy of the bodies.
class DecliningUnloadHandler : public UnloadController::TabUnloadHandler {
 public:
  DecliningUnloadHandler();
  ~DecliningUnloadHandler() override;

  void set_intercept(bool intercept) { intercept_ = intercept; }

  bool ShouldSkipBeforeUnload(content::WebContents* contents) override;
  bool ShouldShowCustomConfirmation(content::WebContents* contents) override;
  bool ShowCustomConfirmation(
      content::WebContents* contents,
      base::OnceCallback<void(bool)> on_closed) override;

 private:
  bool intercept_ = true;
  base::OnceCallback<void(bool)> on_closed_;
};

inline DecliningUnloadHandler::DecliningUnloadHandler() = default;
inline DecliningUnloadHandler::~DecliningUnloadHandler() = default;

inline bool DecliningUnloadHandler::ShouldSkipBeforeUnload(
    content::WebContents* contents) {
  return false;
}

inline bool DecliningUnloadHandler::ShouldShowCustomConfirmation(
    content::WebContents* contents) {
  return intercept_;
}

inline bool DecliningUnloadHandler::ShowCustomConfirmation(
    content::WebContents* contents,
    base::OnceCallback<void(bool)> on_closed) {
  if (!intercept_) {
    return false;
  }
  // Held, not answered. The tab stays until something runs this.
  on_closed_ = std::move(on_closed);
  return true;
}

// content exposes no public way to give a test page a beforeunload handler:
// the only seam is the mojo call a live renderer makes, which lands on
// RenderFrameHostImpl. This is what content's own tests do instead (see
// render_frame_host_impl_browsertest.cc). The cast is sound because the
// frame of a TestWebContents really is a TestRenderFrameHost.
inline void SetBeforeUnloadHandler(content::WebContents* web_contents,
                                   bool present) {
  static_cast<content::RenderFrameHostImpl*>(
      web_contents->GetPrimaryMainFrame())
      ->SuddenTerminationDisablerChanged(
          present,
          blink::mojom::SuddenTerminationDisablerType::kBeforeUnloadHandler);
}

}  // namespace arcium::test

#endif  // ARCIUM_TEST_SPACE_TEST_UTIL_H_
