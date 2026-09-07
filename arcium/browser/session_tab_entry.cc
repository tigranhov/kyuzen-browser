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

}  // namespace

void StashRestoredEntryId(
    content::WebContents* web_contents,
    const std::map<std::string, std::string>& extra_data) {
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

  tabs::TabInterface* tab = tabs::TabInterface::GetFromContents(web_contents);
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
  tabs::TabInterface* tab = tabs::TabInterface::GetFromContents(web_contents);
  if (!tab) {
    return;
  }
  ArciumProfileState* state = StateFor(web_contents);
  const tabs::TabHandle handle = tab->GetHandle();
  // IsClaimedByEntry rather than TabBinding::IsBound: a binding whose entry
  // the model has dropped is exactly the stale case, and writing its id down
  // would give it another life on the next restart.
  if (!IsClaimedByEntry(*state->model(), *state->binding(), handle)) {
    return;
  }
  const std::optional<EntryId> id = state->binding()->EntryForTab(handle);
  if (!id.has_value()) {
    return;
  }
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(tab_id, kEntryIdExtraDataKey,
                                             id->value()));
}

}  // namespace arcium
