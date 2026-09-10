// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/session_tab_entry.h"

#include <optional>
#include <utility>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/entry_claim.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "components/sessions/core/command_storage_manager.h"
#include "components/sessions/core/session_service_commands.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"

namespace arcium {

namespace {

// The parking place between the two halves of a restore. Deliberately a
// WebContents-scoped value and not a member of anything longer-lived: the two
// halves are separated by an insertion into a tab strip, and nothing else
// spans that gap for a tab that does not exist yet.
class RestoredEntryId : public content::WebContentsUserData<RestoredEntryId> {
 public:
  ~RestoredEntryId() override = default;

  EntryId id() const { return id_; }

 private:
  friend class content::WebContentsUserData<RestoredEntryId>;

  RestoredEntryId(content::WebContents* web_contents, EntryId id)
      : content::WebContentsUserData<RestoredEntryId>(*web_contents),
        id_(std::move(id)) {}

  EntryId id_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(RestoredEntryId);

ArciumProfileState* StateFor(content::WebContents* web_contents) {
  return ArciumProfileState::GetForBrowserContext(
      web_contents->GetBrowserContext());
}

// For the two save paths, which are only ever passing through. Chromium walks
// every tab of every window on a session command rebuild and calls the other
// one on every tab close; neither has anything to say about a profile the
// sidebar has not opened, and constructing the state to find that out would
// post an archive open and a model load from inside a tab-strip walk.
ArciumProfileState* ExistingStateFor(content::WebContents* web_contents) {
  return ArciumProfileState::GetForBrowserContextIfExists(
      web_contents->GetBrowserContext());
}

// The id of an entry that *still exists* and claims `handle`, or nullopt.
//
// IsClaimedByEntry rather than TabBinding::IsBound: a binding whose entry the
// model has dropped is exactly the stale case, and writing its id down would
// give a dead entry another life on the next restore.
std::optional<EntryId> LiveEntryFor(ArciumProfileState* state,
                                    tabs::TabHandle handle) {
  if (!IsClaimedByEntry(*state->model(), *state->binding(), handle)) {
    return std::nullopt;
  }
  return state->binding()->EntryForTab(handle);
}

}  // namespace

void StashRestoredEntryId(
    content::WebContents* web_contents,
    const std::map<std::string, std::string>& extra_data) {
  // Before the entry-id lookup below, and unconditionally: a tab with no
  // entry at all is still a tab with a space and a key, and this is the only
  // seam a restore passes through with both the contents and the extra_data
  // in hand.
  RestoreTabSpaceData(web_contents, extra_data);

  auto it = extra_data.find(kEntryIdExtraDataKey);
  if (it == extra_data.end()) {
    return;
  }
  // EntryId::FromString rejects anything that is not a lowercase UUID, so a
  // truncated or hand-edited session file parks nothing rather than inventing
  // an id that could collide with a generated one.
  const EntryId id = EntryId::FromString(it->second);
  if (!id.is_valid()) {
    return;
  }
  RestoredEntryId::CreateForWebContents(web_contents, id);
}

void BindStashedEntryId(content::WebContents* web_contents) {
  RestoredEntryId* stash = RestoredEntryId::FromWebContents(web_contents);
  if (!stash) {
    return;
  }
  const EntryId id = stash->id();
  // Cleared before any decision, so every path below leaves nothing behind. A
  // stash that outlived its one use would rebind a tab the user had since
  // detached, at whatever later moment something called in again.
  web_contents->RemoveUserData(RestoredEntryId::UserDataKey());

  // MaybeGetFromContents, not GetFromContents: the latter dereferences its
  // lookup unconditionally. This call sits in a patch, immediately after the
  // tab-strip insertion in AddRestoredTabImpl, and a rebase that moves the
  // hook line above that insertion is exactly the change a reviewer would
  // wave through. Null here has to mean a cold entry, not a crashed browser.
  tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(web_contents);
  if (!tab) {
    return;
  }
  ArciumProfileState* state = StateFor(web_contents);
  // The model may not have finished loading, or the entry may simply be gone.
  // Either way the tab is a Today tab: binding it to an entry the model does
  // not hold is the invisible-tab bug, not a recovery.
  if (!state->model()->GetEntry(id)) {
    return;
  }
  // Suppressed: session restore binds every warm entry in a row and
  // Chromium rebuilds the session command list when the restore finishes, so
  // asking for a rebuild per restored pin would be startup work for nothing.
  TabBinding::ScopedChangeSuppression suppress(state->binding());
  state->binding()->Bind(id, tab->GetHandle());
}

void AppendTabEntryCommand(
    sessions::CommandStorageManager* command_storage_manager,
    SessionID tab_id,
    content::WebContents* web_contents) {
  // MaybeGetFromContents for the same reason as above: this one is called
  // from a walk over a tab strip, so a null is not reachable today, and
  // GetFromContents would turn a future seam that is into a crash.
  tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(web_contents);
  if (!tab) {
    return;
  }
  ArciumProfileState* state = ExistingStateFor(web_contents);
  if (!state) {
    return;
  }
  // Every tab gets its space and key; only a claimed one also gets an entry
  // id.
  AppendTabSpaceCommands(command_storage_manager, tab_id, web_contents,
                         *state->model(), *state->binding());
  const std::optional<EntryId> id = LiveEntryFor(state, tab->GetHandle());
  if (!id.has_value()) {
    return;
  }
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(tab_id, kEntryIdExtraDataKey,
                                             id->value()));
}

void PopulateTabEntryExtraData(tabs::TabInterface* tab,
                               std::map<std::string, std::string>* extra_data) {
  // Guarded because the caller's neighbour guards it: BrowserLiveTabContext
  // hands both of us whatever GetTabAtIndex returned.
  if (!tab) {
    return;
  }
  ArciumProfileState* state = ExistingStateFor(tab->GetContents());
  if (!state) {
    return;
  }
  // Every tab contributes its space and key; only a claimed one also
  // contributes an entry id.
  PopulateTabSpaceExtraData(tab, *state->model(), *state->binding(),
                            extra_data);
  const std::optional<EntryId> id = LiveEntryFor(state, tab->GetHandle());
  if (!id.has_value()) {
    return;
  }
  (*extra_data)[kEntryIdExtraDataKey] = id->value();
}

}  // namespace arcium
