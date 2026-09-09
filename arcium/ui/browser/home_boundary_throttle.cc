// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/home_boundary_throttle.h"

#include <memory>
#include <optional>
#include <utility>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/home_boundary.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/common/arcium_features.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"

namespace arcium {

HomeBoundaryThrottle::HomeBoundaryThrottle(
    content::NavigationThrottleRegistry& registry,
    GURL home)
    : content::NavigationThrottle(registry), home_(std::move(home)) {}

HomeBoundaryThrottle::~HomeBoundaryThrottle() = default;

const char* HomeBoundaryThrottle::GetNameForLogging() {
  return "HomeBoundaryThrottle";
}

// static
void HomeBoundaryThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  if (!features::IsHomeBoundaryEnabled()) {
    return;
  }
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  // A link click in an iframe navigates the iframe, and the boundary has
  // nothing to say about it.
  if (!handle.IsInPrimaryMainFrame()) {
    return;
  }
  // Belt-and-braces: Blink classifies a reload separately, so it never
  // arrives as a link click anyway. Chrome's own tabbed-web-app throttle
  // guards this explicitly and matching it costs a line.
  if (handle.GetReloadType() != content::ReloadType::NONE) {
    return;
  }
  content::WebContents* web_contents = handle.GetWebContents();
  if (!web_contents) {
    return;
  }
  // Null for a prerender, a WebContents with no tab, or a tab mid-detach.
  tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(web_contents);
  if (!tab) {
    return;
  }
  // IfExists, never GetForBrowserContext: the constructing variant posts an
  // archive open and a model load, and this runs for navigations in profiles
  // the sidebar has never opened.
  ArciumProfileState* state = ArciumProfileState::GetForBrowserContextIfExists(
      web_contents->GetBrowserContext());
  if (!state) {
    return;
  }
  const std::optional<EntryId> id =
      state->binding()->EntryForTab(tab->GetHandle());
  if (!id.has_value()) {
    return;
  }
  // Both kinds, deliberately: an entry is an entry. Zen reaches the same
  // place by making an essential a pinned tab.
  const TabEntry* entry = state->model()->GetEntry(*id);
  if (!entry) {
    return;
  }
  registry.AddThrottle(
      std::make_unique<HomeBoundaryThrottle>(registry, entry->url));
}

content::NavigationThrottle::ThrottleCheckResult
HomeBoundaryThrottle::WillStartRequest() {
  content::NavigationHandle* handle = navigation_handle();
  // The entire policy on which navigations count as leaving. Blink sets this
  // only for a click on an <a>, an SVG <a> or a MathML <a>; a redirect, a
  // <meta> refresh, a location.href assignment, a form POST, a typed URL and
  // a session restore all arrive false. That is why sign-in survives a rule
  // stricter than registrable domain, with no allow-list of auth hosts.
  if (!handle->WasInitiatedByLinkClick()) {
    return content::NavigationThrottle::PROCEED;
  }
  // A programmatic anchorElement.click() is still a link click by Blink's
  // classification above, but it carries no user gesture. Diverting it would
  // still cancel the navigation and hand the open to
  // WebContents::OpenURL(..., NEW_FOREGROUND_TAB), which
  // components/blocked_content/popup_blocker.cc's ShouldBlockPopup() rejects
  // outright for a gesture-less open. The visible result would not be "stays
  // in the tab": it would be a navigation that simply never happens, since
  // this feature would have already cancelled the one that would otherwise
  // have proceeded. Requiring a gesture here costs nothing for a real click,
  // which always carries one.
  if (!handle->HasUserGesture()) {
    return content::NavigationThrottle::PROCEED;
  }
  // A traversal is not a fresh click, even when it replays one.
  if ((handle->GetPageTransition() & ui::PAGE_TRANSITION_FORWARD_BACK) != 0) {
    return content::NavigationThrottle::PROCEED;
  }
  content::WebContents* web_contents = handle->GetWebContents();
  if (!LinkLeavesHome(web_contents->GetLastCommittedURL(), handle->GetURL(),
                      home_)) {
    return content::NavigationThrottle::PROCEED;
  }
  content::OpenURLParams params =
      content::OpenURLParams::FromNavigationHandle(handle);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  // The new navigation is in a new tab, not in the frame that asked. Clearing
  // this is also what leaves the new tab without an opener, so the page it
  // loads cannot script the entry's tab.
  params.frame_tree_node_id = content::FrameTreeNodeId();
  web_contents->OpenURL(std::move(params),
                        /*navigation_handle_callback=*/{});
  return content::NavigationThrottle::CANCEL_AND_IGNORE;
}

}  // namespace arcium
