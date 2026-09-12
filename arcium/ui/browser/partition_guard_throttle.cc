// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/partition_guard_throttle.h"

#include <memory>
#include <utility>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace arcium {

namespace {

// The mark a reopened tab carries until its own navigation is seen. A
// defect in a creation hook can then cost one extra tab, never an endless
// chain of them.
class ReopenedByGuard : public content::WebContentsUserData<ReopenedByGuard> {
 public:
  ~ReopenedByGuard() override = default;

  GURL url;

 private:
  friend class content::WebContentsUserData<ReopenedByGuard>;
  explicit ReopenedByGuard(content::WebContents* web_contents)
      : content::WebContentsUserData<ReopenedByGuard>(*web_contents) {}

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(ReopenedByGuard);

// Whether this navigation is the one a reopen asked for. Clears the mark
// either way: it answers for one navigation only.
bool WasJustReopened(content::WebContents* web_contents, const GURL& url) {
  ReopenedByGuard* mark = ReopenedByGuard::FromWebContents(web_contents);
  if (!mark) {
    return false;
  }
  const bool matches = mark->url == url;
  web_contents->RemoveUserData(ReopenedByGuard::UserDataKey());
  return matches;
}

void MarkReopened(const GURL& url, content::NavigationHandle& handle) {
  ReopenedByGuard::CreateForWebContents(handle.GetWebContents());
  ReopenedByGuard::FromWebContents(handle.GetWebContents())->url = url;
}

// A tab that never showed anything -- the popup content created for the
// navigation the guard just cancelled -- has nothing left to show. Posted,
// because a throttle may not close its own tab while it is answering, and
// checked again because anything may have happened in between.
void CloseTabIfStillEmpty(base::WeakPtr<content::WebContents> web_contents) {
  if (!web_contents) {
    return;
  }
  content::NavigationEntry* entry =
      web_contents->GetController().GetLastCommittedEntry();
  if (entry && !entry->IsInitialEntry()) {
    return;
  }
  if (tabs::TabInterface* tab =
          tabs::TabInterface::MaybeGetFromContents(web_contents.get())) {
    tab->Close();
  }
}

// The relocation itself, always from a task of its own so it never runs
// inside the navigation it is replacing, nor inside whatever started that
// navigation. Anything may have happened in between, so the tab is checked
// again before it is used.
void ReopenInRightStorage(base::WeakPtr<content::WebContents> web_contents,
                          content::OpenURLParams params,
                          GURL url,
                          bool close_after) {
  if (!web_contents) {
    return;
  }
  web_contents->OpenURL(std::move(params), base::BindOnce(&MarkReopened, url));
  if (close_after) {
    CloseTabIfStillEmpty(web_contents);
  }
}

}  // namespace

// static
void PartitionGuardThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  if (!handle.IsInPrimaryMainFrame()) {
    return;
  }
  content::WebContents* web_contents = handle.GetWebContents();
  // Incognito ignores profiles: its tabs use incognito's own storage.
  if (!web_contents || web_contents->GetBrowserContext()->IsOffTheRecord()) {
    return;
  }
  tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(web_contents);
  if (!tab) {
    return;
  }
  BrowserWindowInterface* window = tab->GetBrowserWindowInterface();
  // A window without a sidebar has no spaces, and the creation hooks leave
  // its tabs to Chromium; the guard has to leave them alone too, or the two
  // would argue over every tab in it.
  if (!window ||
      !SpaceSwitcher::FromTabStripModel(window->GetTabStripModel())) {
    return;
  }
  // IfExists, never GetForBrowserContext: the constructing variant posts an
  // archive open and a model load, and this runs for every navigation.
  ArciumProfileState* state = ArciumProfileState::GetForBrowserContextIfExists(
      web_contents->GetBrowserContext());
  // Until the model file has been read every space is unknown and every
  // profile reads as Default, which would reopen every restored tab of a
  // profile in shared storage. The session's answer created these tabs and
  // is the better one.
  if (!state || !state->model_load_finished()) {
    return;
  }
  if (WasJustReopened(web_contents, handle.GetURL())) {
    return;
  }
  const SpaceId space =
      SpaceOfTab(*state->model(), *state->binding(), tab->GetHandle());
  registry.AddThrottle(std::make_unique<PartitionGuardThrottle>(
      registry, state->model()->ProfileOfSpace(space)));
}

PartitionGuardThrottle::PartitionGuardThrottle(
    content::NavigationThrottleRegistry& registry,
    ProfileId space_profile)
    : content::NavigationThrottle(registry), space_profile_(space_profile) {}

PartitionGuardThrottle::~PartitionGuardThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
PartitionGuardThrottle::WillStartRequest() {
  return CheckStorage();
}

content::NavigationThrottle::ThrottleCheckResult
PartitionGuardThrottle::WillRedirectRequest() {
  // A redirect can change what kind of page this is -- an extension can
  // redirect a web page to one of its own -- and the kind is what decides
  // the storage.
  return CheckStorage();
}

const char* PartitionGuardThrottle::GetNameForLogging() {
  return "PartitionGuardThrottle";
}

content::NavigationThrottle::ThrottleCheckResult
PartitionGuardThrottle::CheckStorage() {
  content::NavigationHandle* handle = navigation_handle();
  content::WebContents* web_contents = handle->GetWebContents();
  const GURL url = handle->GetURL();
  if (IsInRightStorage(url, PartitionDomainOfTab(web_contents),
                       space_profile_)) {
    return content::NavigationThrottle::PROCEED;
  }
  content::OpenURLParams params =
      content::OpenURLParams::FromNavigationHandle(handle);
  // In a new tab rather than in this frame, so it is built by the same hook
  // as every other new tab and lands in the storage it belongs in.
  params.frame_tree_node_id = content::FrameTreeNodeId();
  params.disposition =
      web_contents->GetVisibility() == content::Visibility::VISIBLE
          ? WindowOpenDisposition::NEW_FOREGROUND_TAB
          : WindowOpenDisposition::NEW_BACKGROUND_TAB;
  // Chromium's popup blocker treats any NEW_FOREGROUND_TAB/NEW_BACKGROUND_TAB
  // open from an existing tab as a popup unless it carries a user gesture
  // (components/blocked_content/popup_blocker.cc,
  // ShouldBlockPopup: no gesture is an unconditional block). The navigation
  // this is relocating may have had none of its own -- a typed URL loaded
  // through NavigationController::LoadURL carries no gesture, and neither
  // would an extension's tabs.update -- so without forcing one here the
  // guard's own reopen would itself get silently blocked, leaving the
  // cancelled page open nowhere. This reopen is Arcium's own trusted
  // relocation of an already-permitted navigation, never an untrusted popup,
  // so it is never a candidate for that block.
  params.user_gesture = true;

  content::NavigationEntry* entry =
      web_contents->GetController().GetLastCommittedEntry();
  const bool close_after = !entry || entry->IsInitialEntry();

  // Posted, never done from here: a throttle runs inside whatever started the
  // navigation, and that can be a TabStripModel operation holding its
  // reentrancy guard -- showing a space reloads the discarded tabs it
  // contains, from inside the strip's own selection change. Opening a tab
  // there CHECK-crashes the browser on TabStripModel's ReentrancyCheck.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&ReopenInRightStorage, web_contents->GetWeakPtr(),
                     std::move(params), url, close_after));
  return content::NavigationThrottle::CANCEL_AND_IGNORE;
}

}  // namespace arcium
