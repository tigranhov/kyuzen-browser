// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The space menu's profile commands. Naming and colouring are the model's
// alone; moving a space, clearing and deleting reach tabs and storage in
// every window, so they go to profile_actions.h, which owns that walk.

#include <utility>

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/ui/browser/profile_actions.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"

namespace arcium {

std::vector<SidebarProfile> SidebarTabModel::profiles() const {
  std::vector<SidebarProfile> result;
  for (const ArciumProfile& profile : arcium_model_->profiles()) {
    result.push_back(
        {.id = profile.id, .name = profile.name, .color = profile.color});
  }
  return result;
}

void SidebarTabModel::CreateProfileForSpace(SpaceId space,
                                            const std::u16string& name,
                                            int color) {
  if (!arcium_model_->GetSpace(space)) {
    return;
  }
  const ProfileId id = arcium_model_->AddProfile(name, color);
  SetSpaceProfile(space, id);
  // SetSpaceProfile routes through MoveSpaceToProfile, which refuses off the
  // record and a handful of other cases this layer cannot see from here. A
  // refusal must not leave a profile in the list that nothing points at and
  // the user cannot explain, so undo the add rather than guess at every
  // reason the attach could have failed.
  if (arcium_model_->ProfileOfSpace(space) != id) {
    arcium_model_->RemoveProfile(id);
  }
}

void SidebarTabModel::SetSpaceProfile(SpaceId space, ProfileId profile) {
  MoveSpaceToProfile(tab_strip_model_->profile(), space, profile);
}

void SidebarTabModel::RenameProfile(ProfileId id, const std::u16string& name) {
  arcium_model_->RenameProfile(id, name);
}

void SidebarTabModel::SetProfileColor(ProfileId id, int color) {
  arcium_model_->SetProfileColor(id, color);
}

void SidebarTabModel::ClearProfileData(ProfileId id) {
  ClearArciumProfileData(tab_strip_model_->profile(), id, base::DoNothing());
}

void SidebarTabModel::DeleteProfile(ProfileId id) {
  DeleteArciumProfile(tab_strip_model_->profile(), id);
}

}  // namespace arcium
