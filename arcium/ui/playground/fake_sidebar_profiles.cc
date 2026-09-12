// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The fake's half of profiles: an in-memory stand-in for ArciumModel's
// profiles and the operations that reopen tabs and clear storage, kept simple
// enough for a view test. There are no tabs to reopen and no storage to
// clear, so those parts are the model changes alone.

#include <algorithm>
#include <utility>

#include "arcium/ui/playground/fake_sidebar_model.h"

namespace arcium {

SidebarProfile* FakeSidebarModel::FindProfile(ProfileId id) {
  for (SidebarProfile& profile : profiles_) {
    if (profile.id == id) {
      return &profile;
    }
  }
  return nullptr;
}

ProfileId FakeSidebarModel::AddProfileForTesting(const std::u16string& name,
                                                 int color) {
  SidebarProfile profile;
  profile.id = ProfileId::Generate();
  profile.name = name;
  profile.color = color;
  profiles_.push_back(profile);
  Notify();
  return profile.id;
}

std::vector<SidebarProfile> FakeSidebarModel::profiles() const {
  return profiles_;
}

void FakeSidebarModel::CreateProfileForSpace(SpaceId space,
                                             const std::u16string& name,
                                             int color) {
  if (!FindSpace(space)) {
    return;
  }
  const ProfileId id = AddProfileForTesting(name, color);
  SetSpaceProfile(space, id);
}

void FakeSidebarModel::SetSpaceProfile(SpaceId space_id, ProfileId profile) {
  SidebarSpace* space = FindSpace(space_id);
  if (!space || !FindProfile(profile) || space->profile_id == profile) {
    return;
  }
  space->profile_id = profile;
  Notify();
}

void FakeSidebarModel::RenameProfile(ProfileId id, const std::u16string& name) {
  if (SidebarProfile* profile = FindProfile(id)) {
    profile->name = name;
    Notify();
  }
}

void FakeSidebarModel::SetProfileColor(ProfileId id, int color) {
  if (SidebarProfile* profile = FindProfile(id)) {
    profile->color = color;
    Notify();
  }
}

void FakeSidebarModel::ClearProfileData(ProfileId id) {
  if (FindProfile(id)) {
    cleared_profiles_.push_back(id);
  }
}

void FakeSidebarModel::DeleteProfile(ProfileId id) {
  if (id == DefaultProfileId() || !FindProfile(id)) {
    return;
  }
  std::erase_if(profiles_,
                [id](const SidebarProfile& p) { return p.id == id; });
  for (SidebarSpace& space : spaces_) {
    if (space.profile_id == id) {
      space.profile_id = DefaultProfileId();
    }
  }
  Notify();
}

}  // namespace arcium
