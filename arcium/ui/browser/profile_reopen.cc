// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/profile_reopen.h"

#include <memory>
#include <utility>
#include <vector>

#include "arcium/browser/profile_partition.h"
#include "arcium/browser/tab_space.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/content_serialized_navigation_builder.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/restore_type.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace arcium {

void ReopenTabInProfile(TabStripModel* strip,
                        int index,
                        const ProfileId& profile) {
  if (!strip || index < 0 || index >= strip->count()) {
    return;
  }
  content::WebContents* old_contents = strip->GetWebContentsAt(index);
  content::BrowserContext* context = old_contents->GetBrowserContext();
  if (context->IsOffTheRecord()) {
    return;
  }

  // Serialised and rebuilt, never CopyStateFrom: that clones each entry's
  // SiteInstance, which belongs to the storage this tab is leaving, and the
  // old session-storage map with it.
  std::vector<sessions::SerializedNavigationEntry> navigations;
  int selected = 0;
  content::NavigationController& old_controller = old_contents->GetController();
  for (int i = 0; i < old_controller.GetEntryCount(); ++i) {
    content::NavigationEntry* entry = old_controller.GetEntryAtIndex(i);
    if (entry->IsInitialEntry()) {
      continue;
    }
    if (i == old_controller.GetLastCommittedEntryIndex()) {
      selected = static_cast<int>(navigations.size());
    }
    navigations.push_back(
        sessions::ContentSerializedNavigationBuilder::FromNavigationEntry(
            static_cast<int>(navigations.size()), entry));
  }

  const GURL url = navigations.empty() ? GURL(url::kAboutBlankURL)
                                       : navigations[selected].virtual_url();
  content::WebContents::CreateParams params(
      context, SiteInstanceForProfile(context, profile, url));
  params.initially_hidden =
      old_contents->GetVisibility() == content::Visibility::HIDDEN;
  // Nothing loads until the tab is on screen, exactly as a restored tab.
  params.desired_renderer_state =
      content::WebContents::CreateParams::kNoRendererProcess;
  params.last_active_time = old_contents->GetLastActiveTime();
  params.last_active_time_ticks = old_contents->GetLastActiveTimeTicks();
  std::unique_ptr<content::WebContents> new_contents =
      content::WebContents::Create(params);

  if (!navigations.empty()) {
    std::vector<std::unique_ptr<content::NavigationEntry>> entries =
        sessions::ContentSerializedNavigationBuilder::ToNavigationEntries(
            navigations, context);
    new_contents->GetController().Restore(
        selected, content::RestoreType::kRestored, &entries);
  }

  // The tag and the key live on the WebContents and do not follow it.
  SetSpaceTag(new_contents.get(), SpaceTagOf(old_contents));
  if (const TabKey key = ExistingKeyOf(old_contents); key.is_valid()) {
    SetTabKey(new_contents.get(), key);
  }

  const bool was_on_screen = strip->active_index() == index;
  // Inside the tab: the handle, the entry binding and the selection all
  // stay, and the tab strip reports a replacement rather than a close.
  std::unique_ptr<content::WebContents> discarded =
      strip->DiscardWebContentsAt(index, std::move(new_contents));
  discarded.reset();
  if (was_on_screen) {
    strip->GetWebContentsAt(index)->GetController().LoadIfNecessary();
  }
}

}  // namespace arcium
