// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/tab_space.h"

#include <optional>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "components/sessions/core/command_storage_manager.h"
#include "components/sessions/core/session_service_commands.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"

namespace arcium {

namespace {

// The two facts a tab carries about itself. On the WebContents rather than
// on the TabInterface because the restore reads them in
// chrome::CreateRestoredTab, where the contents exists and the tab does not —
// the same gap RestoredEntryId parks in.
class TabSpaceData : public content::WebContentsUserData<TabSpaceData> {
 public:
  ~TabSpaceData() override = default;

  SpaceId space_tag;
  TabKey key;

 private:
  friend class content::WebContentsUserData<TabSpaceData>;
  explicit TabSpaceData(content::WebContents* web_contents)
      : content::WebContentsUserData<TabSpaceData>(*web_contents) {}

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(TabSpaceData);

}  // namespace

SpaceId SpaceTagOf(content::WebContents* web_contents) {
  TabSpaceData* data = TabSpaceData::FromWebContents(web_contents);
  return data ? data->space_tag : SpaceId();
}

void SetSpaceTag(content::WebContents* web_contents, SpaceId id) {
  TabSpaceData::CreateForWebContents(web_contents);
  TabSpaceData::FromWebContents(web_contents)->space_tag = id;
}

TabKey KeyOf(content::WebContents* web_contents) {
  TabSpaceData::CreateForWebContents(web_contents);
  TabSpaceData* data = TabSpaceData::FromWebContents(web_contents);
  // Generated on first ask rather than at creation: there is no one seam
  // every tab passes through, and a key nothing ever reads costs a UUID.
  if (!data->key.is_valid()) {
    data->key = TabKey::Generate();
  }
  return data->key;
}

TabKey ExistingKeyOf(content::WebContents* web_contents) {
  TabSpaceData* data = TabSpaceData::FromWebContents(web_contents);
  return data ? data->key : TabKey();
}

void SetTabKey(content::WebContents* web_contents, TabKey key) {
  TabSpaceData::CreateForWebContents(web_contents);
  TabSpaceData::FromWebContents(web_contents)->key = key;
}

SpaceId SpaceOfTab(const ArciumModel& model,
                   const TabBinding& binding,
                   tabs::TabHandle handle) {
  // The entry's space first, and not as an optimisation: moving a pin to
  // another space must carry its open tab, and it does exactly because the
  // tab is never asked.
  if (const std::optional<EntryId> id = binding.EntryForTab(handle)) {
    if (const TabEntry* entry = model.GetEntry(*id)) {
      return entry->space_id;
    }
  }
  tabs::TabInterface* tab = handle.Get();
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  const SpaceId tag = contents ? SpaceTagOf(contents) : SpaceId();
  // An unknown tag is a space that was deleted, or a session file from
  // another profile. Either way the tab is somewhere the user can see it.
  return model.GetSpace(tag) ? tag : model.default_space_id();
}

void RestoreTabSpaceData(content::WebContents* web_contents,
                         const std::map<std::string, std::string>& extra_data) {
  if (auto it = extra_data.find(kSpaceIdExtraDataKey); it != extra_data.end()) {
    // FromString refuses anything that is not a lowercase UUID, so a
    // hand-edited session file tags nothing rather than inventing an id.
    if (const SpaceId id = SpaceId::FromString(it->second); id.is_valid()) {
      SetSpaceTag(web_contents, id);
    }
  }
  if (auto it = extra_data.find(kTabKeyExtraDataKey); it != extra_data.end()) {
    if (const TabKey key = TabKey::FromString(it->second); key.is_valid()) {
      SetTabKey(web_contents, key);
    }
  }
}

void AppendTabSpaceCommands(
    sessions::CommandStorageManager* command_storage_manager,
    SessionID tab_id,
    content::WebContents* web_contents,
    const ArciumModel& model,
    const TabBinding& binding) {
  tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(web_contents);
  if (!tab) {
    return;
  }
  // Production never reaches this writer for an incognito window — the
  // rebuild path only ever walks a regular profile's tab strips. But nothing
  // about an incognito tab, not even a space id and a random key, may reach a
  // regular session file if some future rebase moves the seam that keeps it
  // away; this guard is what makes that true regardless.
  if (web_contents->GetBrowserContext()->IsOffTheRecord()) {
    return;
  }
  // The resolved space, not the raw tag: a tab whose entry has since moved
  // spaces would otherwise come back where it used to be.
  const SpaceId space = SpaceOfTab(model, binding, tab->GetHandle());
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(tab_id, kSpaceIdExtraDataKey,
                                             space.value()));
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(tab_id, kTabKeyExtraDataKey,
                                             KeyOf(web_contents).value()));
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(
          tab_id, kProfileIdExtraDataKey, model.ProfileOfSpace(space).value()));
}

void PopulateTabSpaceExtraData(tabs::TabInterface* tab,
                               const ArciumModel& model,
                               const TabBinding& binding,
                               std::map<std::string, std::string>* extra_data) {
  // Same reason as AppendTabSpaceCommands's guard: this in-session-close path
  // is not known to run for an incognito tab today, but nothing incognito may
  // reach a regular extra_data map if that ever changes.
  if (tab->GetContents()->GetBrowserContext()->IsOffTheRecord()) {
    return;
  }
  const SpaceId space = SpaceOfTab(model, binding, tab->GetHandle());
  (*extra_data)[kSpaceIdExtraDataKey] = space.value();
  (*extra_data)[kTabKeyExtraDataKey] = KeyOf(tab->GetContents()).value();
  (*extra_data)[kProfileIdExtraDataKey] = model.ProfileOfSpace(space).value();
}

ProfileId ProfileIdFromExtraData(
    const std::map<std::string, std::string>& extra_data) {
  auto it = extra_data.find(kProfileIdExtraDataKey);
  if (it == extra_data.end()) {
    return DefaultProfileId();
  }
  // FromString refuses anything that is not a lowercase UUID.
  const ProfileId id = ProfileId::FromString(it->second);
  return id.is_valid() ? id : DefaultProfileId();
}

}  // namespace arcium
