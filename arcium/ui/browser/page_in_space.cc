// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/page_in_space.h"

#include <memory>
#include <utility>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/browser/tab_space.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace arcium {

content::WebContents* OpenPageInSpace(Browser* browser,
                                      SpaceId space,
                                      const content::OpenURLParams& params,
                                      LoosePageKind kind) {
  if (!browser) {
    return nullptr;
  }
  Profile* profile = browser->GetProfile();
  // IfExists: a window with a sidebar has already built this, and one
  // without has no business constructing it from here.
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(profile);
  if (!state || !state->model()->GetSpace(space)) {
    return nullptr;
  }
  // Null for the default profile, a browser page or off the record, and then
  // content picks the default partition, which is right for all three.
  scoped_refptr<content::SiteInstance> site_instance = SiteInstanceForProfile(
      profile, state->model()->ProfileOfSpace(space), params.url);
  content::WebContents::CreateParams create_params(profile,
                                                   std::move(site_instance));
  create_params.initially_hidden = true;
  std::unique_ptr<content::WebContents> owned =
      content::WebContents::Create(create_params);
  content::WebContents* contents = owned.get();
  SetSpaceTag(contents, space);
  SetLoosePageKind(contents, kind);

  TabStripModel* strip = browser->tab_strip_model();
  // TabModel attaches the tab helpers on insertion, as it does for every tab:
  // the password manager, extension identity and dialogs all come from that.
  strip->InsertWebContentsAt(strip->count(), std::move(owned),
                             AddTabTypes::ADD_NONE);
  content::NavigationController::LoadURLParams load_params(params);
  contents->GetController().LoadURLWithParams(load_params);
  return contents;
}

void BringPageToScreen(Browser* browser, content::WebContents* contents) {
  if (!browser || !contents) {
    return;
  }
  TabStripModel* strip = browser->tab_strip_model();
  int index = strip->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  // Cleared first: the move and the activation below are what the sidebar
  // listens to, and it has to see an ordinary tab when they happen.
  SetLoosePageKind(contents, LoosePageKind::kNone);
  const int last = strip->count() - 1;
  if (index != last) {
    index = strip->MoveWebContentsAt(index, last, /*select_after_move=*/false);
  }
  // The switch lands on the space's last tab first; the page is one of the
  // space's open tabs by now, so no blank tab is made for the landing.
  if (SpaceSwitcher* switcher = SpaceSwitcher::FromTabStripModel(strip)) {
    switcher->SwitchTo(SpaceTagOf(contents));
  }
  index = strip->GetIndexOfWebContents(contents);
  if (index != TabStripModel::kNoTab) {
    strip->ActivateTabAt(index);
  }
}

void OpenUrlInRoutedSpace(Browser* browser, const GURL& url) {
  if (!browser) {
    return;
  }
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(browser->GetProfile());
  const SpaceSwitcher* switcher =
      SpaceSwitcher::FromTabStripModel(browser->tab_strip_model());
  const SpaceId routed = state ? state->model()->SpaceForUrl(url) : SpaceId();
  if (!routed.is_valid() || !switcher || routed == switcher->active_space()) {
    chrome::AddSelectedTabWithURL(browser, url, ui::PAGE_TRANSITION_TYPED);
    return;
  }
  // Built in the rule's space rather than switched to first: switching to a
  // space with no tabs opens a blank tab, which the page would then sit
  // beside.
  content::OpenURLParams params(url, content::Referrer(),
                                WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                ui::PAGE_TRANSITION_TYPED,
                                /*is_renderer_initiated=*/false);
  if (content::WebContents* contents =
          OpenPageInSpace(browser, routed, params, LoosePageKind::kNone)) {
    BringPageToScreen(browser, contents);
  }
}

void ClosePage(Browser* browser, content::WebContents* contents) {
  if (!browser || !contents) {
    return;
  }
  TabStripModel* strip = browser->tab_strip_model();
  const int index = strip->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  strip->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
}

}  // namespace arcium
