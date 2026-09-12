// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/new_tab_storage.h"

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace arcium {

namespace {

// The window's sidebar state and the space the new tab joins, or an invalid
// space when this window has no sidebar or the profile no state.
SpaceId SpaceAndState(BrowserWindowInterface* browser,
                      content::WebContents* source,
                      ArciumProfileState** state_out) {
  if (!browser) {
    return SpaceId();
  }
  // IfExists, never GetForBrowserContext: the constructing variant posts an
  // archive open and a model load, and this runs for every window, sidebar
  // or not.
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(browser->GetProfile());
  const SpaceSwitcher* switcher =
      SpaceSwitcher::FromTabStripModel(browser->GetTabStripModel());
  if (!state || !switcher) {
    return SpaceId();
  }
  *state_out = state;
  return SpaceForNewTab(*state->model(), *state->binding(), switcher, source);
}

}  // namespace

SpaceId SpaceForNewTab(const ArciumModel& model,
                       const TabBinding& binding,
                       const SpaceSwitcher* switcher,
                       content::WebContents* source) {
  if (!switcher) {
    return SpaceId();
  }
  tabs::TabInterface* source_tab =
      source ? tabs::TabInterface::MaybeGetFromContents(source) : nullptr;
  // A navigation from something that is not a tab -- devtools, an extension
  // page -- joins the space on screen, as a tab from another application
  // does.
  return source_tab ? SpaceOfTab(model, binding, source_tab->GetHandle())
                    : switcher->active_space();
}

scoped_refptr<content::SiteInstance> SiteInstanceForNewTab(
    BrowserWindowInterface* browser,
    content::WebContents* source,
    const GURL& url,
    scoped_refptr<content::SiteInstance> chromium_choice) {
  ArciumProfileState* state = nullptr;
  const SpaceId space = SpaceAndState(browser, source, &state);
  if (!space.is_valid()) {
    return chromium_choice;
  }
  scoped_refptr<content::SiteInstance> fixed = SiteInstanceForProfile(
      browser->GetProfile(), state->model()->ProfileOfSpace(space), url);
  return fixed ? fixed : chromium_choice;
}

void TagNewTab(BrowserWindowInterface* browser,
               content::WebContents* source,
               content::WebContents* contents) {
  ArciumProfileState* state = nullptr;
  const SpaceId space = SpaceAndState(browser, source, &state);
  if (space.is_valid()) {
    SetSpaceTag(contents, space);
  }
}

}  // namespace arcium
